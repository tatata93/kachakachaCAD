// 文書から画面の場面を作る(WP-11/WP-12)。
#include "kachakacha/app/SampleDocument.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Commands.h"

#include <array>
#include <cstdint>
#include <string>

using kachakacha::v2::app::BuildSampleDocument;
using kachakacha::v2::app::BuildSceneFromDocument;
using kachakacha::v2::app::RebuildSceneKeepingView;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::document::DocumentSnapshot;
using kachakacha::v2::domain::Visibility;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::modeling::SnapScene;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

[[nodiscard]] DocumentSnapshot Sample()
{
    return BuildSampleDocument().snapshot;
}

} // namespace

KACHA_V2_TEST(scene_builder, 見本の線がすべて場面へ出る)
{
    DeterministicIdGenerator ids{3};
    const SnapScene scene = BuildSceneFromDocument(Sample(), ids);
    // 外形4本 + 腰1本 + 窓6枚x4本 + 前照灯1本 = 30本。
    RequireEqual(std::to_string(scene.curves.size()), std::string("30"), "線の数");
}

KACHA_V2_TEST(scene_builder, 線はどのEntityのものか分かる)
{
    DeterministicIdGenerator ids{3};
    const SnapScene scene = BuildSceneFromDocument(Sample(), ids);
    for (const auto& curve : scene.curves) {
        Require(!curve.entityId.IsNil(), "EntityId がある");
        Require(!curve.segmentId.IsNil(), "SegmentId がある");
    }
}

KACHA_V2_TEST(scene_builder, SegmentのIDは開くたびに変わらない)
{
    // 変わると、保存して開くたびに別のものを指すことになる。
    DeterministicIdGenerator first{3};
    DeterministicIdGenerator second{99};
    const SnapScene a = BuildSceneFromDocument(Sample(), first);
    const SnapScene b = BuildSceneFromDocument(Sample(), second);
    RequireEqual(std::to_string(a.curves.size()), std::to_string(b.curves.size()), "数");
    for (std::size_t index = 0; index < a.curves.size(); ++index) {
        Require(a.curves[index].segmentId == b.curves[index].segmentId, "同じ ID");
        Require(a.curves[index].entityId == b.curves[index].entityId, "同じ Entity");
    }
}

KACHA_V2_TEST(scene_builder, 補助線の印が残る)
{
    DeterministicIdGenerator ids{3};
    const SnapScene scene = BuildSceneFromDocument(Sample(), ids);
    int construction = 0;
    for (const auto& curve : scene.curves) {
        if (curve.construction) {
            ++construction;
        }
    }
    RequireEqual(std::to_string(construction), std::string("1"), "腰の高さだけ");
}

KACHA_V2_TEST(scene_builder, 円は円のまま出る)
{
    DeterministicIdGenerator ids{3};
    const SnapScene scene = BuildSceneFromDocument(Sample(), ids);
    bool found = false;
    for (const auto& curve : scene.curves) {
        if (curve.segment.Kind() == CurveKind::Circle) {
            found = true;
        }
    }
    Require(found, "前照灯が円のまま");
}

KACHA_V2_TEST(scene_builder, 消しているものは出さない)
{
    // 出すと、消したつもりのものが吸着の相手になる。
    DocumentSnapshot snapshot = Sample();
    for (auto& entity : snapshot.entities) {
        if (entity.displayName == "窓1") {
            entity.visibility = Visibility::Hidden;
        }
    }
    DeterministicIdGenerator ids{3};
    const SnapScene scene = BuildSceneFromDocument(snapshot, ids);
    RequireEqual(std::to_string(scene.curves.size()), std::string("26"), "窓1の4本が減る");
}

KACHA_V2_TEST(scene_builder, 止めたFeatureは出さない)
{
    DocumentSnapshot snapshot = Sample();
    for (auto& feature : snapshot.features) {
        if (feature.displayName == "前照灯") {
            feature.enabled = false;
        }
    }
    DeterministicIdGenerator ids{3};
    const SnapScene scene = BuildSceneFromDocument(snapshot, ids);
    RequireEqual(std::to_string(scene.curves.size()), std::string("29"), "前照灯が減る");
}

KACHA_V2_TEST(scene_builder, 何も無い文書からは空の場面が出る)
{
    DeterministicIdGenerator ids{3};
    const SnapScene scene = BuildSceneFromDocument(DocumentSnapshot{}, ids);
    Require(scene.curves.empty(), "線が無い");
    Require(scene.points.empty(), "点が無い");
}

KACHA_V2_TEST(scene_builder, 開いても見ている場所は変わらない)
{
    // ファイルを開いたからといって、作業平面やグリッドまで変えない。
    SnapScene current;
    current.grid.visible = true;
    current.grid.majorSpacingMm = 25.0;
    current.workPlane.active = true;
    current.workPlane.origin = kachakacha::v2::geometry::Vector3{5.0, 6.0, 7.0};
    DeterministicIdGenerator ids{3};
    const SnapScene rebuilt = RebuildSceneKeepingView(current, Sample(), ids);
    Require(rebuilt.grid.majorSpacingMm == 25.0, "グリッドはそのまま");
    Require(rebuilt.workPlane.origin.z == 7.0, "作業平面もそのまま");
    RequireEqual(std::to_string(rebuilt.curves.size()), std::string("30"), "線は入れ替わる");
}

KACHA_V2_TEST(scene_builder, 開き直しても場面は増えない)
{
    // 二重に並べると、同じ線が2本ずつ出て、吸着も選択も壊れる。
    DeterministicIdGenerator ids{3};
    SnapScene scene = BuildSceneFromDocument(Sample(), ids);
    scene = RebuildSceneKeepingView(scene, Sample(), ids);
    scene = RebuildSceneKeepingView(scene, Sample(), ids);
    RequireEqual(std::to_string(scene.curves.size()), std::string("30"), "30本のまま");
}

KACHA_V2_TEST(scene_builder, 開くと文書が入れ替わり履歴は捨てられる)
{
    // 開く前の文書へ「元に戻す」で帰れると、どのファイルを見ているのか分からなくなる。
    using kachakacha::v2::document::Document;
    using kachakacha::v2::document::SetVisibilityCommand;
    Document document(kachakacha::v2::base::DocumentId(
        kachakacha::v2::base::Uuid(std::array<std::uint8_t, 16>{})));
    const auto problems = document.ResetTo(Sample());
    for (const auto& diagnostic : problems) {
        Require(!diagnostic.IsError(), "見本は壊れていない: " + diagnostic.code);
    }
    RequireEqual(std::to_string(document.Snapshot().entities.size()), std::string("9"),
        "入れ替わっている");
    Require(!document.CanUndo(), "履歴は捨てられている");
    Require(!document.CanRedo(), "やり直しも無い");
}

KACHA_V2_TEST(scene_builder, 壊れた文書は入れない)
{
    using kachakacha::v2::document::Document;
    Document document(kachakacha::v2::base::DocumentId(
        kachakacha::v2::base::Uuid(std::array<std::uint8_t, 16>{})));
    DocumentSnapshot broken = Sample();
    // 同じ ID を2つにする。構造の検証が落とすはずである。
    broken.entities.push_back(broken.entities.front());
    const auto problems = document.ResetTo(broken);
    bool hasError = false;
    for (const auto& diagnostic : problems) {
        hasError = hasError || diagnostic.IsError();
    }
    Require(hasError, "断る");
    Require(document.Snapshot().entities.empty(), "いまの文書はそのまま");
}

KACHA_V2_TEST_MAIN("scene_builder_tests")
