//! 押し出しの依存関係(Codex P1-EXTRUDE-R2 の指摘 B1・B2 の回帰試験)。
//!
//! ここで見るのは「立体が出たか」ではなく **作り方の記録が現実と合っているか** である。
//!   - 面の押し引きで作った縁の線が、押し出しの入力として記録されている。
//!   - 足す・引くの相手の立体も、押し出しの入力として記録されている。
//!   - だから縁の線を直せば、押し出しが計算し直す対象に入る。
//!   - 保存して開き直しても、その記録が残る。
//!   - 足す・複数輪郭・線の出力・面の押し引きが、まとめて1回で戻り、1回でやり直せる。

#include "V2SelfTest.h"

#include "V2ExtrudeDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/document/FeatureReevaluation.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/ExtrudeInput.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::FeatureId;
using kachakacha::v2::document::DocumentSnapshot;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::ExtrudeDefinition;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::Visibility;

//! 閉じた矩形を1つ引いて選ぶ。押し出しの相手になる。
[[nodiscard]] bool DrawClosedRectangle(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter(kachakacha::v2::geometry::Vector3{});
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Rectangle);
    viewport.ClickAt(QPointF(viewport.width() * 0.35, viewport.height() * 0.35));
    viewport.HoverAt(QPointF(viewport.width() * 0.65, viewport.height() * 0.65));
    viewport.ClickAt(QPointF(viewport.width() * 0.65, viewport.height() * 0.65));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::Wire));
    return !viewport.Selection().entityIds.empty();
}

[[nodiscard]] EntityId LastOfKind(V2MainWindow& window, EntityKind kind)
{
    EntityId found;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kind) {
            found = entity.id;
        }
    }
    return found;
}

[[nodiscard]] std::vector<EntityId> AllOfKind(V2MainWindow& window, EntityKind kind)
{
    std::vector<EntityId> found;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kind) {
            found.push_back(entity.id);
        }
    }
    return found;
}

//! いちばん後に作られた押し出しの Feature。
[[nodiscard]] const Feature* LastExtrude(const DocumentSnapshot& snapshot)
{
    const Feature* found = nullptr;
    for (const auto& feature : snapshot.features) {
        if (std::get_if<ExtrudeDefinition>(&feature.definition) != nullptr) {
            found = &feature;
        }
    }
    return found;
}

//! その Entity を作った Feature。
[[nodiscard]] FeatureId MakerOf(const DocumentSnapshot& snapshot, EntityId id)
{
    for (const auto& entity : snapshot.entities) {
        if (entity.id == id) {
            return entity.createdBy;
        }
    }
    return FeatureId{};
}

[[nodiscard]] bool Contains(const std::vector<EntityId>& list, EntityId id)
{
    return std::find(list.begin(), list.end(), id) != list.end();
}

[[nodiscard]] int VisibleParts(V2MainWindow& window)
{
    int count = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part && entity.visibility == Visibility::Visible) {
            ++count;
        }
    }
    return count;
}

//! 立体を1つ作って、その面を1枚選んだところまで進める。
[[nodiscard]] bool MakeSolidAndPickFace(V2MainWindow& window, EntityId& part)
{
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    window.RunCommand("part.extrude");
    part = LastOfKind(window, EntityKind::Part);
    if (!Explain("立体ができる", !part.IsNil())) {
        return false;
    }
    kachakacha::v2::app::SelectionSet faceSelection;
    kachakacha::v2::app::SelectionRef face;
    face.entityId = part;
    face.kind = kachakacha::v2::app::SelectionElementKind::Face;
    face.pickedFaceIndex = 0;
    faceSelection.ordered.push_back(face);
    faceSelection.entityIds.push_back(part);
    window.Viewport().SetSelection(faceSelection);
    return true;
}

//! R2 B2。面の押し引きで作った縁の線が、押し出しの入力として記録されること。
//! 記録されていないと、あとで縁を直しても押し出しが計算し直されない。
[[nodiscard]] bool CaseFaceProfileWiresAreRecordedAsInputs(V2MainWindow& window)
{
    EntityId part;
    if (!MakeSolidAndPickFace(window, part)) {
        return false;
    }
    const auto wiresBefore = AllOfKind(window, EntityKind::Wire);
    window.RunCommand("part.extrude");
    window.ExtrudeDock().TypeDistanceMm(2.0);
    window.RunCommand("part.extrude");

    const auto& snapshot = window.Session().GetDocument().Snapshot();
    const auto wiresAfter = AllOfKind(window, EntityKind::Wire);
    std::vector<EntityId> made;
    for (const auto& id : wiresAfter) {
        if (!Contains(wiresBefore, id)) {
            made.push_back(id);
        }
    }
    if (!Explain((std::string("面の縁が線になる(") + std::to_string(made.size())
                     + " 本)").c_str(),
            !made.empty())) {
        return false;
    }
    const Feature* extrude = LastExtrude(snapshot);
    if (!Explain("押し出しの作り方が残る", extrude != nullptr)) {
        return false;
    }
    for (const auto& id : made) {
        if (!Explain("作った縁の線が、押し出しの入力に入っている",
                Contains(extrude->inputEntityIds, id))) {
            return false;
        }
    }
    return Explain("相手の立体も入力に入っている",
        Contains(extrude->inputEntityIds, part));
}

//! R2 B2 の続き。入力に入っているなら、縁の線を直せば押し出しが計算し直す対象に入る。
[[nodiscard]] bool CaseEditingTheFaceProfileMakesTheExtrudeStale(V2MainWindow& window)
{
    EntityId part;
    if (!MakeSolidAndPickFace(window, part)) {
        return false;
    }
    const auto wiresBefore = AllOfKind(window, EntityKind::Wire);
    window.RunCommand("part.extrude");
    window.ExtrudeDock().TypeDistanceMm(2.0);
    window.RunCommand("part.extrude");

    const auto& snapshot = window.Session().GetDocument().Snapshot();
    EntityId madeWire;
    for (const auto& id : AllOfKind(window, EntityKind::Wire)) {
        if (!Contains(wiresBefore, id)) {
            madeWire = id;
        }
    }
    if (!Explain("縁の線ができる", !madeWire.IsNil())) {
        return false;
    }
    const Feature* extrude = LastExtrude(snapshot);
    if (!Explain("押し出しの作り方が残る", extrude != nullptr)) {
        return false;
    }
    const FeatureId wireMaker = MakerOf(snapshot, madeWire);
    if (!Explain("縁の線にも作り方がある", !wireMaker.IsNil())) {
        return false;
    }
    const auto plan = kachakacha::v2::document::PlanReevaluation(snapshot, wireMaker);
    if (!Explain("計算し直す順番が出せる", plan.HasValue())) {
        return false;
    }
    const bool downstream = std::find(plan.Value().order.begin(), plan.Value().order.end(),
                                extrude->id)
        != plan.Value().order.end();
    return Explain("縁の線を直すと、押し出しが計算し直す対象に入る", downstream);
}

//! R2。面の押し引きの結果が、保存して開き直しても残ること。
[[nodiscard]] bool CaseFacePushPullSurvivesSaveAndReopen(V2MainWindow& window)
{
    EntityId part;
    if (!MakeSolidAndPickFace(window, part)) {
        return false;
    }
    window.RunCommand("part.extrude");
    window.ExtrudeDock().TypeDistanceMm(2.0);
    window.RunCommand("part.extrude");

    const int partsBefore = CountOfKind(window, EntityKind::Part);
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    const int visibleBefore = VisibleParts(window);
    std::size_t inputsBefore = 0;
    if (const Feature* extrude = LastExtrude(window.Session().GetDocument().Snapshot())) {
        inputsBefore = extrude->inputEntityIds.size();
    }
    if (!Explain("入力が記録されている", inputsBefore > 0)) {
        return false;
    }

    if (!Explain("保存して開き直せる",
            window.SaveAndReopen(QStringLiteral("kacha_selftest_facepush.kcd2")))) {
        return false;
    }
    if (!Explain((std::string("立体の数が変わらない(") + std::to_string(partsBefore)
                     + " → " + std::to_string(CountOfKind(window, EntityKind::Part))
                     + ")").c_str(),
            CountOfKind(window, EntityKind::Part) == partsBefore)) {
        return false;
    }
    if (!Explain("線の数が変わらない",
            CountOfKind(window, EntityKind::Wire) == wiresBefore)) {
        return false;
    }
    if (!Explain("隠した立体は隠れたまま", VisibleParts(window) == visibleBefore)) {
        return false;
    }
    const Feature* reopened = LastExtrude(window.Session().GetDocument().Snapshot());
    if (!Explain("押し出しの作り方が残る", reopened != nullptr)) {
        return false;
    }
    return Explain((std::string("入力の記録も残る(") + std::to_string(inputsBefore)
                       + " → " + std::to_string(reopened->inputEntityIds.size())
                       + ")").c_str(),
        reopened->inputEntityIds.size() == inputsBefore);
}

//! R2 B1。足す押し出し(相手を隠す・線を出す・面の縁を作る)が、
//! まとめて1回で戻り、1回でやり直せること。
[[nodiscard]] bool CaseFacePushPullRoundTripsInOneUndo(V2MainWindow& window)
{
    EntityId part;
    if (!MakeSolidAndPickFace(window, part)) {
        return false;
    }
    const int partsBefore = CountOfKind(window, EntityKind::Part);
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    const int visibleBefore = VisibleParts(window);

    window.RunCommand("part.extrude");
    window.ExtrudeDock().TypeDistanceMm(2.0);
    window.RunCommand("part.extrude");

    const int partsAfter = CountOfKind(window, EntityKind::Part);
    const int wiresAfter = CountOfKind(window, EntityKind::Wire);
    if (!Explain("立体が増える", partsAfter > partsBefore)) {
        return false;
    }
    if (!Explain("線も増える", wiresAfter > wiresBefore)) {
        return false;
    }
    if (!Explain("足しなので見える立体は1つのまま", VisibleParts(window) == 1)) {
        return false;
    }

    window.RunCommand("edit.undo");
    if (!Explain((std::string("1回の取り消しで立体が戻る(") + std::to_string(partsAfter)
                     + " → " + std::to_string(CountOfKind(window, EntityKind::Part))
                     + ")").c_str(),
            CountOfKind(window, EntityKind::Part) == partsBefore)) {
        return false;
    }
    if (!Explain("縁の線も一緒に戻る",
            CountOfKind(window, EntityKind::Wire) == wiresBefore)) {
        return false;
    }
    if (!Explain("隠した立体も一緒に戻る", VisibleParts(window) == visibleBefore)) {
        return false;
    }

    window.RunCommand("edit.redo");
    if (!Explain("1回のやり直しで立体が戻る",
            CountOfKind(window, EntityKind::Part) == partsAfter)) {
        return false;
    }
    if (!Explain("縁の線も一緒にやり直される",
            CountOfKind(window, EntityKind::Wire) == wiresAfter)) {
        return false;
    }
    return Explain("隠したのも一緒にやり直される", VisibleParts(window) == 1);
}

} // namespace

std::vector<SelfTestCase> ExtrudeGraphCases()
{
    return {
        {"面の縁の線が押し出しの入力として記録される",
            CaseFaceProfileWiresAreRecordedAsInputs},
        {"面の縁を直すと押し出しが計算し直す対象に入る",
            CaseEditingTheFaceProfileMakesTheExtrudeStale},
        {"面の押し引きは保存して開き直しても残る", CaseFacePushPullSurvivesSaveAndReopen},
        {"面の押し引きは1回の取り消しとやり直しで往復する",
            CaseFacePushPullRoundTripsInOneUndo},
    };
}

} // namespace kachakacha::v2::selftest
