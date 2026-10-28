//! 線の上に置いて押す編集の人の道(HP-TR、トリム。Inventor の手順)。
//!
//! 帯の トリム を押す → 何も選ばずに線の上に置くと消える区間が下見に出る → 押すと消える →
//! 道具は構えたまま → Esc でやめる。円は交点の間が消えて円弧になり、1 回の取り消しで戻る。
//! 置く・押すは実際の画面の道(HoverAt / ClickAt)。
#include "V2SelfTest.h"

#include "V2HoverEditTool.h"
#include "V2MainWindow.h"
#include "V2Ribbon.h"
#include "V2Viewport.h"
#include "V2TrimSourcePolicy.h"
#include "kachakacha/document/Commands.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>
#include <QCheckBox>
#include <QDialog>
#include <QListWidget>
#include <QTimer>
#include <QObject>
#include <QApplication>
#include <QDir>
#include <QPixmap>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::DrawingTool;

[[nodiscard]] EntityId NewestWire(V2MainWindow& window)
{
    EntityId newest;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Wire) {
            newest = entity.id;
        }
    }
    return newest;
}

//! 上から見た画面に、道具で 2 か所を押して 1 本引く(直線・円は中心と円周の点)。
[[nodiscard]] EntityId DrawTwoClicks(V2MainWindow& window, DrawingTool tool, double x0, double y0,
    double x1, double y1)
{
    auto& viewport = window.Viewport();
    const EntityId before = NewestWire(window);
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter(Vector3{});
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(tool);
    viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(viewport.width() * x0, viewport.height() * y0));
    viewport.HoverAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.ClickAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.SetSnapSuppressed(false);
    window.SelectTool(DrawingTool::Select);
    const EntityId after = NewestWire(window);
    return after == before ? EntityId{} : after;
}

//! その線の線分(1 本目)。
[[nodiscard]] std::optional<CurveSegment> FirstSegmentOf(V2MainWindow& window, const EntityId& id)
{
    for (const auto& curve : window.Session().Scene().curves) {
        if (curve.entityId == id) {
            return curve.segment;
        }
    }
    return std::nullopt;
}

[[nodiscard]] int SegmentCountOf(V2MainWindow& window, const EntityId& id)
{
    int count = 0;
    for (const auto& curve : window.Session().Scene().curves) {
        if (curve.entityId == id) {
            ++count;
        }
    }
    return count;
}

//! 3D の点の上へ置く / 押す。
[[nodiscard]] bool HoverWorld(V2MainWindow& window, const Vector3& point)
{
    const auto screen = window.Viewport().Mapping().Project(point);
    if (!screen.has_value()) {
        return false;
    }
    window.Viewport().HoverAt(QPointF(screen->x, screen->y));
    return true;
}

[[nodiscard]] bool ClickWorld(V2MainWindow& window, const Vector3& point)
{
    const auto screen = window.Viewport().Mapping().Project(point);
    if (!screen.has_value()) {
        return false;
    }
    window.Viewport().ClickAt(QPointF(screen->x, screen->y));
    return true;
}

//! HP-TR-01。円と、それを横切る直線。トリムを持って円の下側に置くと消える区間が見え、押すと
//! 上半分の円弧になる。道具は構えたまま。1 回の取り消しで円に戻る。
[[nodiscard]] bool CaseTrimCircleBecomesArc(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    const EntityId circle = DrawTwoClicks(window, DrawingTool::Circle, 0.50, 0.50, 0.65, 0.50);
    const EntityId line = DrawTwoClicks(window, DrawingTool::Line, 0.20, 0.50, 0.80, 0.50);
    const auto ring = circle.IsNil() ? std::nullopt : FirstSegmentOf(window, circle);
    if (!Explain("円と横切る直線を手で引ける", !line.IsNil() && ring.has_value()
            && ring->Kind() == CurveKind::Circle)) {
        return false;
    }
    auto& ribbon = window.Ribbon();
    if (!Explain("帯の「編集」を押せる", ribbon.ClickCategory(QStringLiteral("編集")))
        || !Explain("帯の「トリム」を押せる(何も選んでいなくてよい)",
            ribbon.ClickTool(QStringLiteral("トリム")))
        || !Explain("トリムの道具になる", window.Session().CurrentTool() == DrawingTool::Trim)) {
        return false;
    }
    // 円の下側(中心から -y へ半径)に置く。
    const Vector3 bottom = ring->Center() + Vector3{0.0, -ring->Radius(), 0.0};
    const Vector3 top = ring->Center() + Vector3{0.0, ring->Radius(), 0.0};
    auto& tool = window.HoverEditTool();
    const auto* keep = window.findChild<QCheckBox*>(QStringLiteral("trimKeepOriginal"));
    if (!Explain("元の線を残すはトリムで表示され既定オフ", keep && keep->isVisible() && !keep->isChecked())) return false;
    if (!Explain("円の下側に置ける", HoverWorld(window, bottom))
        || !Explain((std::string("消える区間の下見が出る(") + window.ToolFooterTextJa().toStdString()
                        + ")").c_str(),
            !window.Viewport().CurrentEditPreview().lines.empty()
                && window.Viewport().CurrentEditPreview().removing)
        || !Explain("下見の一言が「円弧になる」",
            tool.Outcome().has_value() && tool.Outcome()->summaryJa.find("円弧") != std::string::npos)) {
        return false;
    }
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    if (!Explain("下側を押せる", ClickWorld(window, bottom))) {
        return false;
    }
    // 円だった線は消え、円弧が 1 本できる。線の数は変わらない。
    EntityId arcWire;
    std::optional<CurveSegment> arc;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind != EntityKind::Wire || entity.id == line) {
            continue;
        }
        const auto segment = FirstSegmentOf(window, entity.id);
        if (segment.has_value() && segment->Kind() == CurveKind::CircularArc) {
            arcWire = entity.id;
            arc = segment;
        }
    }
    if (!Explain("円が円弧になる", arc.has_value())
        || !Explain("元の円形状は残らず同じ線が円弧になる", window.Session().GetDocument().FindEntity(circle) && arcWire == circle)
        || !Explain("線の数は変わらない", CountOfKind(window, EntityKind::Wire) == wiresBefore)
        || !Explain("残った円弧は半周", std::abs(std::abs(arc->SweepAngleRad()) - 3.14159265358979323846) < 1.0e-6)
        || !Explain("残ったのは上半分", geometry::Distance(arc->Evaluate(0.5), top) < 1.0e-3)
        || !Explain("押したあとも道具は構えたまま", window.Session().CurrentTool() == DrawingTool::Trim)) {
        return false;
    }
    window.RunCommand("edit.undo");
    const auto restored = FirstSegmentOf(window, circle);
    if (!Explain("1 回の取り消しで円に戻る", restored.has_value() && restored->Kind() == CurveKind::Circle
            && CountOfKind(window, EntityKind::Wire) == wiresBefore)) {
        return false;
    }
    (void)window.Viewport().PressEscape();
    return Explain("Esc でトリムを維持する", window.Session().CurrentTool() == DrawingTool::Trim);
}

//! HP-TR-02。縦 2 本と横 1 本。横の真ん中を押すと 2 本に分かれ、残った左の線(交点が無い)を
//! 押すと線ごと消える。
[[nodiscard]] bool CaseTrimMiddleMakesTwoThenWholeGoes(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    const EntityId left = DrawTwoClicks(window, DrawingTool::Line, 0.40, 0.30, 0.40, 0.70);
    const EntityId right = DrawTwoClicks(window, DrawingTool::Line, 0.60, 0.30, 0.60, 0.70);
    const EntityId across = DrawTwoClicks(window, DrawingTool::Line, 0.20, 0.50, 0.80, 0.50);
    const auto bar = across.IsNil() ? std::nullopt : FirstSegmentOf(window, across);
    if (!Explain("縦 2 本と横 1 本を手で引ける", !left.IsNil() && !right.IsNil() && bar.has_value())) {
        return false;
    }
    window.RunCommand("wire.trim");
    const Vector3 middle = bar->Evaluate(0.5);
    const Vector3 leftPart = bar->Evaluate(0.1);
    if (!Explain("横の真ん中に置ける", HoverWorld(window, middle))
        || !Explain("下見は 2 本になると言う", window.HoverEditTool().Outcome().has_value()
                && window.HoverEditTool().Outcome()->chains.size() == 2)
        || !Explain("真ん中を押せる", ClickWorld(window, middle))
        || !Explain("横の線が 2 本に分かれる(全部で 4 本)", CountOfKind(window, EntityKind::Wire) == 4)) {
        return false;
    }
    if (!Explain("左の残りに置ける", HoverWorld(window, leftPart))
        || !Explain("交点が無いので線ごと消えると言う", window.HoverEditTool().Outcome().has_value()
                && window.HoverEditTool().Outcome()->chains.empty())
        || !Explain("左の残りを押せる", ClickWorld(window, leftPart))) {
        return false;
    }
    return Explain("線ごと消える(全部で 3 本)", CountOfKind(window, EntityKind::Wire) == 3);
}

struct TrimFixture {
    EntityId circle, boundary;
    Vector3 bottom;
};
std::optional<TrimFixture> PrepareTrim(V2MainWindow& w)
{
    w.RunCommand("file.new"); w.SetMode(kachakacha::v2::app::UiMode::Drawing);
    TrimFixture f;
    f.circle = DrawTwoClicks(w, DrawingTool::Circle, .50, .50, .65, .50);
    f.boundary = DrawTwoClicks(w, DrawingTool::Line, .20, .50, .80, .50);
    const auto circle = FirstSegmentOf(w, f.circle);
    if (!circle || f.boundary.IsNil()) return std::nullopt;
    f.bottom = circle->Center() + Vector3{0, -circle->Radius(), 0};
    w.RunCommand("wire.trim");
    return f;
}
void CaptureTrim(V2MainWindow& w, const QString& name)
{
    const auto path = qEnvironmentVariable("KACHACAD_TRIM_CAPTURE_DIR");
    if (path.isEmpty()) return;
    QDir dir(path); dir.mkpath("."); w.grab().save(dir.filePath(name));
}
bool TrimWithConfirmation(V2MainWindow& w, Vector3 point, bool accept,
    const std::function<void(QDialog*)>& inspect)
{
    bool seen = false; QTimer timer; timer.setInterval(5);
    QObject::connect(&timer, &QTimer::timeout, &w, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != QStringLiteral("confirmDependentDelete")) return;
        seen = true; inspect(dialog); if (accept) dialog->accept(); else dialog->reject();
    });
    timer.start(); const bool clicked = ClickWorld(w, point); timer.stop(); return clicked && seen;
}
bool CaseTrimKeepOriginal(V2MainWindow& w)
{
    const auto f = PrepareTrim(w); if (!f) return false;
    auto* keep = w.findChild<QCheckBox*>(QStringLiteral("trimKeepOriginal"));
    if (!keep || !keep->isVisible() || keep->isChecked()) return false;
    CaptureTrim(w, "trim-delete-default.png"); keep->click();
    if (!keep->isChecked() || !HoverWorld(w, f->bottom) || !ClickWorld(w, f->bottom)) return false;
    const auto* source = w.Session().GetDocument().FindEntity(f->circle);
    if (!Explain("オンでは元の円を表示したまま円弧を追加", source
        && source->visibility == kachakacha::v2::domain::Visibility::Visible && CountOfKind(w, EntityKind::Wire) == 3)) return false;
    CaptureTrim(w, "trim-keep-original.png");
    w.RunCommand("edit.undo"); if (CountOfKind(w, EntityKind::Wire) != 2) return false;
    w.RunCommand("edit.redo");
    if (!w.SaveAndReopen("trim-keep-original.kcd2") || CountOfKind(w, EntityKind::Wire) != 3
        || !w.Session().GetDocument().FindEntity(f->circle)) return false;
    w.SelectTool(DrawingTool::Line); if (keep->isVisible()) return false;
    w.RunCommand("wire.trim");
    return Explain("トリムを開始し直すと既定の削除へ戻る", keep->isVisible() && !keep->isChecked());
}
bool CaseTrimDependent(V2MainWindow& w, bool keepOriginal)
{
    using namespace kachakacha::v2;
    const auto f = PrepareTrim(w); if (!f) return false;
    base::DeterministicIdGenerator ids(990000);
    domain::Feature feature; feature.id = ids.NextTyped<base::IdKind::Feature>();
    feature.type = domain::FeatureType::CreatePoint; feature.displayName = "依存点";
    feature.inputEntityIds = {f->circle}; feature.definition = domain::CreatePointDefinition{};
    domain::Entity point; point.id = ids.NextTyped<base::IdKind::Entity>(); point.kind = EntityKind::Point; point.displayName = "依存点";
    feature.outputs = {{"point", point.id, point.kind}};
    auto& doc = w.Session().GetDocument();
    if (!doc.Run(document::AddFeatureCommand(feature, {point}, "依存点")).committed) return false;
    document::ReferenceDimension dimension; dimension.id = ids.NextTyped<base::IdKind::Dimension>();
    dimension.label = "円の寸法"; dimension.targets = {f->circle}; dimension.kind = "length"; dimension.unit = "mm";
    if (!doc.Run(document::AddReferenceDimensionCommand(dimension)).committed) return false;
    auto* keep = w.findChild<QCheckBox*>(QStringLiteral("trimKeepOriginal")); if (!keep) return false;
    keep->setChecked(keepOriginal);
    if (!HoverWorld(w, f->bottom)) return false;
    if (keepOriginal) {
        if (!ClickWorld(w, f->bottom)) return false;
        return Explain("保持オンは参照元・依存点・寸法を保ち、結果だけ追加", doc.FindEntity(f->circle)
            && doc.FindEntity(point.id) && doc.Snapshot().referenceDimensions.size() == 1 && CountOfKind(w, EntityKind::Wire) == 3);
    }
    if (!ClickWorld(w, f->bottom) || !Explain("部分トリムは同じ線・依存点・寸法を残す",
        doc.FindEntity(f->circle) && doc.FindEntity(point.id) && doc.Snapshot().referenceDimensions.size() == 1
        && CountOfKind(w, EntityKind::Wire) == 2)) return false;
    w.RunCommand("edit.undo");
    // Only an actual entire-wire deletion confirms genuine consumers.
    if (!doc.Run(document::SetVisibilityCommand({f->boundary}, domain::Visibility::Hidden)).committed) return false;
    auto scene = w.Session().Scene(); std::erase_if(scene.curves, [&](const auto& c) { return c.entityId == f->boundary; }); w.Session().SetScene(std::move(scene));
    if (!HoverWorld(w, f->bottom)) return false;
    const auto revision = doc.Revision(); bool highlighted = false, listed = false, unmodified = false;
    const auto inspect = [&](QDialog* dialog) {
        highlighted = w.Viewport().DeletionPreviewContains(f->circle) && w.Viewport().DeletionPreviewContains(point.id);
        const auto* list = dialog->findChild<QListWidget*>(); listed = list && list->count() == 3;
        unmodified = doc.Revision() == revision; CaptureTrim(w, "trim-dependent-preview.png");
    };
    if (!TrimWithConfirmation(w, f->bottom, false, inspect) || !highlighted || !listed || !unmodified
        || doc.Revision() != revision || !doc.FindEntity(point.id) || w.Viewport().DeletionPreviewContains(point.id)) return false;
    if (!HoverWorld(w, f->bottom) || !TrimWithConfirmation(w, f->bottom, true, inspect)) return false;
    if (!Explain("全線削除の承認後だけ元線・依存点・寸法を削除する", !doc.FindEntity(f->circle)
        && !doc.FindEntity(point.id) && doc.Snapshot().referenceDimensions.empty() && CountOfKind(w, EntityKind::Wire) == 1)) return false;
    w.RunCommand("edit.undo");
    if (!doc.FindEntity(f->circle) || !doc.FindEntity(point.id) || doc.Snapshot().referenceDimensions.size() != 1) return false;
    w.RunCommand("edit.redo");
    return Explain("削除と生成を1回でUndo/Redoし保存再読込できる", !doc.FindEntity(f->circle)
        && !doc.FindEntity(point.id) && w.SaveAndReopen("trim-delete-dependents.kcd2") && w.RebuildProblems().isEmpty());
}
bool CaseTrimRemovalGuards(V2MainWindow& w)
{
    using namespace kachakacha::v2;
    const auto f = PrepareTrim(w); if (!f) return false;
    auto plan = v2ui::PrepareTrimSourceRemoval(w, {f->circle}, false, [] {});
    auto& doc = w.Session().GetDocument();
    if (!plan.accepted || !plan.command) return false;
    const auto extra = w.Session().AddWire({geometry::CurveSegment::MakeLine({70,70,0},{80,70,0}).Value()}, false, "追加線");
    if (!extra.committed) return false;
    const auto revision = doc.Revision(); const auto rejected = doc.Run(*plan.command);
    if (!Explain("確認後に文書が変わった古い削除計画は適用しない", !rejected.committed
        && doc.Revision() == revision && doc.FindEntity(f->circle) && doc.FindEntity(extra.createdEntityIds.front()))) return false;
    auto fresh = v2ui::PrepareTrimSourceRemoval(w, {f->circle}, false, [] {});
    if (!fresh.command) return false;
    {
        v2ui::TrimGenerationTransaction transaction(w.Session(), "トリム生成の失敗を検査");
        if (!doc.Run(*fresh.command).committed || doc.FindEntity(f->circle)) return false;
        if (!w.Session().AddWire({geometry::CurveSegment::MakeLine({70,80,0},{80,80,0}).Value()}, false, "生成途中").committed) return false;
        // A failed generation leaves the compound transaction uncommitted.
        const auto* original = doc.FindEntity(extra.createdEntityIds.front());
        const auto* feature = doc.FindFeature(original->createdBy);
        if (doc.Run(document::AddFeatureCommand(*feature, {*original}, "重複した生成")).committed) return false;
    }
    return Explain("生成失敗時は元線・履歴・描画を戻す", doc.FindEntity(f->circle) && doc.Revision() == revision
        && std::none_of(w.Session().Scene().curves.begin(), w.Session().Scene().curves.end(), [](const auto& c) {
            return c.segment.StartPoint().y == 80 && c.segment.EndPoint().y == 80;
        }));
}

} // namespace

std::vector<SelfTestCase> HoverEditCases()
{
    return {
        {"HP-TR-03 keep original checkbox preserves visible circle and saved Undo Redo", CaseTrimKeepOriginal},
        {"HP-TR-04 default delete confirms dependents and dimensions with cancel and atomic Undo", [](auto& w) { return CaseTrimDependent(w, false); }},
        {"HP-TR-05 keep original preserves dependent objects and dimensions", [](auto& w) { return CaseTrimDependent(w, true); }},
        {"HP-TR-06 stale removal and failed generation restore the original", CaseTrimRemovalGuards},
        {"HP-TR-01 トリムは何も選ばず円の下側に置くと消える区間が見え押すと上半分の円弧になり1回で戻る",
            CaseTrimCircleBecomesArc},
        {"HP-TR-02 トリムは真ん中を押すと2本に分かれ交点の無い線は線ごと消える",
            CaseTrimMiddleMakesTwoThenWholeGoes},
    };
}

} // namespace kachakacha::v2::selftest
