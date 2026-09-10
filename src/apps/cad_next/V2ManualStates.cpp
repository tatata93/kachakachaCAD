//! 手順書の図を撮るための「状態」の作り方(V2MainWindow の一部)。
//! docs/manual の図は、ここで作った状態を撮ったもの。人が操作しなくても同じ画面になる。
//! 分けたのは、ファイルの長さの門(1500行)を守るため。

#include "V2MainWindow.h"

#include "V2ExportDock.h"
#include "V2ParameterDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/app/ProcessSteps.h"
#include "kachakacha/app/SampleDocument.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/SnapEngine.h"

#include <QPointF>
#include <QString>

#include <string>
#include <vector>

using kachakacha::v2::app::UiMode;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::modeling::SnapCurve;
using kachakacha::v2::modeling::SnapScene;

namespace {

[[nodiscard]] CurveSegment MakeLine(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    return made.Value();
}

} // namespace

bool V2MainWindow::ApplyStepsState(const QString& name)
{
    // 手順の並び。モードごとに、途中まで進んだ形を作る。
    kachakacha::v2::app::ProcessContext context;
    if (name == QStringLiteral("steps-part")) {
        SetMode(UiMode::Part);
        context.extrudeProfileCount = 2;
    } else if (name == QStringLiteral("steps-fabrication")) {
        SetMode(UiMode::Fabrication);
        context.selectedPartCount = 1;
        context.fabricationBuilt = true;
        context.panelCount = 4;
    } else if (name == QStringLiteral("steps-output")) {
        SetMode(UiMode::Output);
        context.exportTargetChosen = true;
    } else {
        SetMode(UiMode::Drawing);
    }
    SetProcessContext(context);
    return true;
}

bool V2MainWindow::ApplyDrawingState(const QString& name)
{
    if (name == QStringLiteral("draw-line")) {
        // 道具を選んで2点置く。文書が変わり、一覧に出ることを確かめる。
        SelectTool(DrawingTool::Line);
        viewport_->SetViewDirection(ViewDirection::Top);
        viewport_->SetVisibleWidthMm(200.0);
        viewport_->ClickAt(QPointF(viewport_->width() * 0.3, viewport_->height() * 0.6));
        viewport_->HoverAt(QPointF(viewport_->width() * 0.7, viewport_->height() * 0.4));
        viewport_->ClickAt(QPointF(viewport_->width() * 0.7, viewport_->height() * 0.4));
        RefreshEntityList();
        return true;
    }
    if (name == QStringLiteral("snap")) {
        // 既にある線の端点へ吸着させる。吸着の印と名前が出る。
        (void)ApplyManualState(QStringLiteral("curves"));
        SelectTool(DrawingTool::Line);
        const auto screen = viewport_->Mapping().Project(Vector3{-20, -30, 0});
        if (screen.has_value()) {
            viewport_->HoverAt(QPointF(screen->x, screen->y));
        }
        return true;
    }
    // isometric
    (void)ApplyManualState(QStringLiteral("curves"));
    viewport_->SetViewDirection(ViewDirection::Isometric);
    viewport_->FitToDocument();
    return true;
}

bool V2MainWindow::ApplySelectionState(const QString& name)
{
    if (name == QStringLiteral("sample")) {
        // 配る見本。マニュアルの手順をそのままなぞれる。
        AdoptDocument(kachakacha::v2::app::BuildSampleDocument().snapshot);
        viewport_->SetViewDirection(ViewDirection::Top);
        viewport_->FitToDocument();
        return true;
    }
    // 線を2本引いてから、選択の道具で選ぶ。選んだ線の色が変わる。
    // 引いた線は文書のワイヤーなので、書き出しの数にもそのまま出る。
    (void)ApplyManualState(QStringLiteral("draw-line"));
    SelectTool(DrawingTool::Line);
    viewport_->ClickAt(QPointF(viewport_->width() * 0.3, viewport_->height() * 0.35));
    viewport_->HoverAt(QPointF(viewport_->width() * 0.7, viewport_->height() * 0.25));
    viewport_->ClickAt(QPointF(viewport_->width() * 0.7, viewport_->height() * 0.25));
    SelectTool(DrawingTool::Select);
    for (const auto& curve : session_->Scene().curves) {
        const auto screen = viewport_->Mapping().Project(curve.segment.Evaluate(0.5));
        if (screen.has_value()) {
            viewport_->SelectAt(QPointF(screen->x, screen->y), Qt::ShiftModifier);
        }
    }
    if (name == QStringLiteral("export")) {
        // 台帳の export.svg / export.validate は型紙や部品を選んだときの道である。
        // ここはワイヤーを選んでいるので、棚が自分で対象と形式を決める。
        SetMode(UiMode::Output);
    }
    return true;
}

bool V2MainWindow::ApplyActiveGroupState()
{
    using kachakacha::v2::document::AddGroupCommand;
    using kachakacha::v2::document::Group;
    Group body;
    body.id = kachakacha::v2::base::GroupId(ids_->Next());
    body.displayName = "車体";
    Group derived;
    derived.id = kachakacha::v2::base::GroupId(ids_->Next());
    derived.displayName = "派生";
    derived.parentId = body.id;
    if (!session_->GetDocument().Run(AddGroupCommand(body)).committed) {
        return false;
    }
    if (!session_->GetDocument().Run(AddGroupCommand(derived)).committed) {
        return false;
    }
    if (!SetActiveGroup(body.id)) {
        return false;
    }
    SelectTool(DrawingTool::Line);
    viewport_->SetViewDirection(ViewDirection::Top);
    viewport_->SetVisibleWidthMm(200.0);
    viewport_->ClickAt(QPointF(viewport_->width() * 0.3, viewport_->height() * 0.6));
    viewport_->ClickAt(QPointF(viewport_->width() * 0.7, viewport_->height() * 0.4));
    RefreshEntityList();
    return true;
}

bool V2MainWindow::ApplyGuideTableState()
{
    // 形状ガイドの役割テーブル。外形U2本と断面2枚を入れた形。
    using kachakacha::v2::modeling::AddSelectionAsNewRow;
    using kachakacha::v2::modeling::ChainRole;
    using kachakacha::v2::modeling::GuideSurfaceMethod;
    using kachakacha::v2::modeling::GuideTableSelection;
    guideTable_ = kachakacha::v2::modeling::GuideTable{};
    guideTable_.method = GuideSurfaceMethod::GuidedLoft;
    const auto make = [&](const char* label, Vector3 from, Vector3 to) {
        GuideTableSelection selection;
        selection.sourceWireId = kachakacha::v2::base::EntityId(ids_->Next());
        selection.label = label;
        selection.segments.push_back(CurveSegment::MakeLine(from, to).Value());
        return selection;
    };
    struct Entry {
        ChainRole role;
        const char* label;
        Vector3 from;
        Vector3 to;
    };
    const Entry entries[] = {
        {ChainRole::GuideU, "guide_lower", {0, 0, 0}, {100, 0, 0}},
        {ChainRole::GuideU, "guide_upper", {0, 0, 40}, {100, 0, 40}},
        {ChainRole::Section, "sec_left", {0, 0, 0}, {0, 0, 40}},
        {ChainRole::Section, "sec_right", {100, 0, 0}, {100, 0, 40}},
    };
    for (const Entry& entry : entries) {
        if (!SetGuideTable(AddSelectionAsNewRow(guideTable_, entry.role,
                make(entry.label, entry.from, entry.to)))) {
            return false;
        }
    }
    viewport_->SetViewDirection(ViewDirection::Isometric);
    SetMode(UiMode::Part);   // 役割テーブルは部品モードの道具である。
    return true;
}

bool V2MainWindow::ApplyStaticState(const QString& name)
{
    ClearDiagnostics();
    if (name == QStringLiteral("empty")) {
        return true;
    }
    if (name == QStringLiteral("grid")) {
        viewport_->SetViewDirection(ViewDirection::Top);
        viewport_->SetVisibleWidthMm(120.0);
        return true;
    }
    if (name == QStringLiteral("tools")) {
        SelectTool(DrawingTool::Arc);
        return true;
    }
    if (name == QStringLiteral("curves") || name == QStringLiteral("curves-win95")) {
        // 直線・円弧・円・ベジェ・B-spline を1つずつ置く。
        // 曲線が曲線のまま描けているかを、画面で確かめるための状態。
        SnapScene scene = session_->Scene();
        // SnapCurve は既定で作れない(CurveSegment を必ず伴うため)。
        // その場で全部そろえて作る。
        // IDは1本ずつ別にする。同じにすると、1本選んだだけで全部が選ばれて見える。
        const auto add = [&](const CurveSegment& segment, bool construction) {
            scene.curves.push_back(SnapCurve{
                ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>(),
                ids_->NextTyped<kachakacha::v2::base::IdKind::Segment>(), segment,
                construction});
        };
        add(MakeLine({-60, -30, 0}, {-20, -30, 0}), false);
        add(MakeLine({-60, -30, 0}, {-60, 10, 0}), true);
        add(CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 25.0, 0.0,
                3.14159265358979323846).Value(),
            false);
        add(CurveSegment::MakeCircle({50, 0, 0}, {0, 0, 1}, {1, 0, 0}, 18.0).Value(),
            false);
        add(CurveSegment::MakeCubicBezier(
                {{-60, 30, 0}, {-40, 60, 0}, {0, 60, 0}, {20, 30, 0}})
                .Value(),
            false);
        add(CurveSegment::MakeCubicBSpline(
                {{30, 40, 0}, {45, 65, 0}, {65, 20, 0}, {85, 55, 0}, {100, 35, 0}})
                .Value(),
            false);
        session_->SetScene(std::move(scene));
        viewport_->SetViewDirection(ViewDirection::Top);
        viewport_->FitToDocument();
        if (name.endsWith(QStringLiteral("win95"))) {
            ApplyTheme(UiTheme::Windows95);
        }
        return true;
    }
    return false;
}

bool V2MainWindow::ApplyManualState(const QString& name)
{
    if (ApplyStaticState(name)) {
        return true;
    }
    if (name == QStringLiteral("draw-line") || name == QStringLiteral("snap")
        || name == QStringLiteral("isometric")) {
        return ApplyDrawingState(name);
    }
    if (name == QStringLiteral("sample") || name == QStringLiteral("select")
        || name == QStringLiteral("export")) {
        return ApplySelectionState(name);
    }
    if (name == QStringLiteral("win95")) {
        ApplyTheme(UiTheme::Windows95);
        return true;
    }
    if (name.startsWith(QStringLiteral("steps-"))) {
        return ApplyStepsState(name);
    }
    if (name == QStringLiteral("active-group")) {
        // 作業中グループ。切り替えたあとに作ったものがそこへ入る。
        // 派生物は派生グループへ入り、作業中グループを切り替えても動かない。
        return ApplyActiveGroupState();
    }
    if (name == QStringLiteral("guide-table")) {
        return ApplyGuideTableState();
    }
    if (name == QStringLiteral("cursor-input")) {
        // カーソル連動の数値入力。長さをロックし、角度の欄へ式を入れた形。
        SelectTool(DrawingTool::Line);
        viewport_->SetViewDirection(ViewDirection::Top);
        viewport_->SetVisibleWidthMm(200.0);
        viewport_->ClickAt(QPointF(viewport_->width() * 0.35, viewport_->height() * 0.6));
        viewport_->HoverAt(QPointF(viewport_->width() * 0.65, viewport_->height() * 0.4));
        if (!viewport_->OpenCursorInput()) {
            return false;
        }
        (void)viewport_->TypeIntoCursorField(QStringLiteral("(180/2)*3"));
        (void)viewport_->CommitCursorField();
        (void)viewport_->FocusNextCursorField(false);
        (void)viewport_->TypeIntoCursorField(QStringLiteral("30deg"));
        return true;
    }
    if (name == QStringLiteral("view-cube")) {
        // ビューキューブをドラッグした後の画面。90度へ吸着していないことを目で見る。
        (void)ApplyManualState(QStringLiteral("curves"));
        viewport_->SetViewDirection(ViewDirection::Isometric);
        viewport_->FitToDocument();
        const QRectF box = viewport_->ViewCubeRect();
        const QPointF press = box.center();
        (void)viewport_->PressViewCube(press);
        viewport_->DragViewCube(press + QPointF(37.0, -13.0));
        viewport_->ReleaseViewCube(press + QPointF(37.0, -13.0));
        return true;
    }
    if (name == QStringLiteral("mode-part")) {
        (void)ApplyManualState(QStringLiteral("curves"));
        SetMode(kachakacha::v2::app::UiMode::Part);
        return true;
    }
    if (name == QStringLiteral("mode-fabrication")) {
        (void)ApplyManualState(QStringLiteral("curves"));
        SetMode(kachakacha::v2::app::UiMode::Fabrication);
        return true;
    }
    if (name == QStringLiteral("mode-output")) {
        (void)ApplyManualState(QStringLiteral("curves"));
        SetMode(kachakacha::v2::app::UiMode::Output);
        return true;
    }
    if (name == QStringLiteral("guide")) {
        // 案内の6つがそろって出ている画面。
        (void)ApplyManualState(QStringLiteral("curves"));
        SelectTool(DrawingTool::Arc);
        return true;
    }
    return false;
}
