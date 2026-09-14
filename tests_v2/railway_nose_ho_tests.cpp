// HO(日本型 1/80・16.5mm)の流線形前頭部 総合試験モデル。TM-01〜14。
#include "kachakacha/app/GroupTree.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/RailwayNoseHoSample.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/io/DocumentFile.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::app::BuildRailwayNoseHoSampleArchive;
using kachakacha::v2::app::BuildRailwayNoseHoSampleDocument;
using kachakacha::v2::app::GroupPathJa;
using kachakacha::v2::app::HoNosePoint;
using kachakacha::v2::app::HoNoseSection;
using kachakacha::v2::app::HoNoseSectionName;
using kachakacha::v2::app::HoNoseSectionStations;
using kachakacha::v2::app::HoNoseWorkPlaneName;
using kachakacha::v2::app::kHoGaugeMm;
using kachakacha::v2::app::kHoNoseDepthMm;
using kachakacha::v2::app::kHoNoseHeightMm;
using kachakacha::v2::app::kHoNoseWidthMm;
using kachakacha::v2::app::kHoScaleDenominator;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] int CountOfKind(const kachakacha::v2::document::DocumentSnapshot& snapshot,
    EntityKind kind)
{
    int count = 0;
    for (const auto& entity : snapshot.entities) {
        if (entity.kind == kind) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool HasEntityNamed(
    const kachakacha::v2::document::DocumentSnapshot& snapshot, const std::string& name)
{
    for (const auto& entity : snapshot.entities) {
        if (entity.displayName == name) {
            return true;
        }
    }
    return false;
}

} // namespace

KACHA_V2_TEST(railway_nose_ho, 模型実寸で作ってある)
{
    // §15・§16。実車寸法で作って最後に縮めない。数字は模型の mm。
    RequireNear(kHoScaleDenominator, 80.0, 1.0e-9, "1/80");
    RequireNear(kHoGaugeMm, 16.5, 1.0e-9, "16.5mm ゲージ");
    Require(kHoNoseWidthMm > 30.0 && kHoNoseWidthMm < 40.0,
        "車体幅は 35mm 前後(模型実寸)");
    Require(kHoNoseHeightMm > 40.0 && kHoNoseHeightMm < 50.0,
        "車体高さは 45mm 前後(模型実寸)");
    Require(kHoNoseDepthMm > 14.0 && kHoNoseDepthMm < 22.0,
        "前頭部の奥行きは 18mm 前後(模型実寸)");
}

KACHA_V2_TEST(railway_nose_ho, 断面は同じ形の縮小コピーではない)
{
    // §19。X 位置で幅・高さ・肩・裾・膨らみが変わること。
    const auto& stations = HoNoseSectionStations();
    Require(stations.size() >= 5, "断面が5枚以上ある");

    // 幅と高さが前から後ろへ広がる。
    double previousHalfWidth = 0.0;
    double previousRoof = 0.0;
    for (const double station : stations) {
        const Vector3 side = HoNosePoint(1.0, station);
        const Vector3 roof = HoNosePoint(0.0, station);
        Require(std::abs(side.y) >= previousHalfWidth - 1.0e-9,
            HoNoseSectionName(station) + ": 幅が狭くならない");
        Require(roof.z >= previousRoof - 1.0e-9,
            HoNoseSectionName(station) + ": 屋根が下がらない");
        previousHalfWidth = std::abs(side.y);
        previousRoof = roof.z;
    }
    // 前端と車体側で、幅と高さの **比** が違う。相似なら比は同じになる。
    const double frontRatio = std::abs(HoNosePoint(1.0, stations.front()).y)
        / HoNosePoint(0.0, stations.front()).z;
    const double backRatio = std::abs(HoNosePoint(1.0, stations.back()).y)
        / HoNosePoint(0.0, stations.back()).z;
    Require(std::abs(frontRatio - backRatio) > 0.05,
        "前端と車体側で断面の形そのものが違う(縮小コピーではない)");
}

KACHA_V2_TEST(railway_nose_ho, 左右対称で前面中央が前へ膨らむ)
{
    // §17。
    for (const double station : HoNoseSectionStations()) {
        for (const double u : {0.25, 0.5, 0.75, 1.0}) {
            const Vector3 left = HoNosePoint(-u, station);
            const Vector3 right = HoNosePoint(u, station);
            RequireNear(left.y, -right.y, 1.0e-9, "左右対称(横)");
            RequireNear(left.z, right.z, 1.0e-9, "左右対称(高さ)");
            RequireNear(left.x, right.x, 1.0e-9, "左右対称(前後)");
        }
    }
    // 前端の断面では、中央が肩より前へ出ている(X が小さいほど前)。
    const double station = HoNoseSectionStations().front();
    Require(HoNosePoint(0.0, station).x < HoNosePoint(0.9, station).x - 0.05,
        "前面中央が前へ膨らむ");
}

KACHA_V2_TEST(railway_nose_ho, 二重曲率を含む)
{
    // §17。一方向だけの円筒では表せないこと。
    // 前後方向にも左右方向にも曲がっている点があれば二重曲率である。
    const double station = 6.0;
    const Vector3 a = HoNosePoint(0.4, station - 3.0);
    const Vector3 b = HoNosePoint(0.4, station);
    const Vector3 c = HoNosePoint(0.4, station + 3.0);
    const Vector3 alongBend = (a + c) * 0.5 - b;
    const Vector3 d = HoNosePoint(0.2, station);
    const Vector3 e = HoNosePoint(0.6, station);
    const Vector3 acrossBend = (d + e) * 0.5 - b;
    Require(alongBend.Length() > 1.0e-3, "前後方向にも曲がっている");
    Require(acrossBend.Length() > 1.0e-3, "左右方向にも曲がっている");
}

KACHA_V2_TEST(railway_nose_ho, 断面は折れ線ではなく曲線で持つ)
{
    for (const double station : HoNoseSectionStations()) {
        const auto section = HoNoseSection(station);
        Require(!section.empty(), HoNoseSectionName(station) + ": 線がある");
        bool hasCurve = false;
        for (const auto& segment : section) {
            if (segment.Kind() != kachakacha::v2::geometry::CurveKind::Line) {
                hasCurve = true;
            }
        }
        Require(hasCurve, HoNoseSectionName(station) + ": 折れ線に落としていない");
    }
}

KACHA_V2_TEST(railway_nose_ho, 見本を作れて中身がそろっている)
{
    // TM-01 / TM-05 / TM-06 / TM-07 / TM-08 / TM-09。
    const auto file = BuildRailwayNoseHoSampleDocument();
    const auto& snapshot = file.snapshot;
    RequireEqual(std::to_string(CountOfKind(snapshot, EntityKind::WorkPlane)),
        std::to_string(HoNoseSectionStations().size()), "作業平面がそろう");
    for (const double station : HoNoseSectionStations()) {
        Require(HasEntityNamed(snapshot, HoNoseWorkPlaneName(station)),
            HoNoseWorkPlaneName(station) + " がある");
        Require(HasEntityNamed(snapshot, HoNoseSectionName(station)),
            HoNoseSectionName(station) + " がある");
    }
    for (const char* guide : {"SkirtGuide_L", "LowerGuide", "ShoulderGuide_L",
             "RoofCenterGuide", "ShoulderGuide_R", "SkirtGuide_R"}) {
        Require(HasEntityNamed(snapshot, guide), std::string(guide) + " がある");
    }
    Require(HasEntityNamed(snapshot, "NoseSurface"), "面がある");
    Require(HasEntityNamed(snapshot, "NoseApproximation"), "近似モデルがある");
    Require(HasEntityNamed(snapshot, "FloorSolid"), "押し出し試験の立体がある");
    Require(HasEntityNamed(snapshot, "WindowProfile"), "切削用の窓がある");
    Require(HasEntityNamed(snapshot, "HeadlightProfile"), "切削用の前照灯がある");
}

KACHA_V2_TEST(railway_nose_ho, まとまりの形が指定どおり)
{
    // §10 / TM-04。
    const auto file = BuildRailwayNoseHoSampleDocument();
    const auto& snapshot = file.snapshot;
    const char* expected[] = {"RailwayNose_HO", "RailwayNose_HO/Sections",
        "RailwayNose_HO/SectionWires", "RailwayNose_HO/Guides",
        "RailwayNose_HO/SourceSurfaces", "RailwayNose_HO/ExtrudeTests",
        "RailwayNose_HO/Approximation", "RailwayNose_HO/GeneratedExamples"};
    for (const char* want : expected) {
        bool found = false;
        for (const auto& group : snapshot.groups) {
            if (GroupPathJa(snapshot, group.id) == want) {
                found = true;
            }
        }
        Require(found, std::string(want) + " がある");
    }
}

KACHA_V2_TEST(railway_nose_ho, 完成形を書き込まず作り方として持つ)
{
    // §44 の禁止事項。面も立体も、作り方(Feature)から作り直せること。
    const auto file = BuildRailwayNoseHoSampleDocument();
    for (const auto& entity : file.snapshot.entities) {
        if (entity.kind != EntityKind::GuideSurface && entity.kind != EntityKind::Part) {
            continue;
        }
        bool madeBy = false;
        for (const auto& feature : file.snapshot.features) {
            if (feature.id == entity.createdBy) {
                madeBy = true;
                Require(!feature.inputEntityIds.empty(),
                    entity.displayName + ": 元になる物がある(空から生えていない)");
            }
        }
        Require(madeBy, entity.displayName + ": 作り方がある");
    }
}

KACHA_V2_TEST(railway_nose_ho, 面の作り方が役割表へ戻せる)
{
    // ここが通らないと、面は一度も作られない。
    // 2026-09-14 に実際そうなっていた。鎖の指し先を曲線1本ごとに並べていたので、
    // 同じワイヤーを何度も入れることになり UI-R006 で断られていた。
    // 画面では「面が作れない」としか見えず、雲の試験は素通りしていた。
    //
    // 鎖の1つの指し先は「1本のワイヤー」である。曲線1本ではない。
    const auto file = BuildRailwayNoseHoSampleDocument();
    kachakacha::v2::document::Document document{kachakacha::v2::base::DocumentId{}};
    const auto problems = document.ResetTo(file.snapshot);
    for (const auto& problem : problems) {
        Require(!problem.IsError(), "見本が読めること: " + problem.code);
    }
    kachakacha::v2::modeling::SnapScene scene;   // 空でよい。作り方の線を読む。
    bool checked = false;
    for (const auto& feature : file.snapshot.features) {
        const auto* definition =
            std::get_if<kachakacha::v2::domain::CreateGuideSurfaceDefinition>(
                &feature.definition);
        if (definition == nullptr) {
            continue;
        }
        const auto table = kachakacha::v2::app::GuideTableFromDefinition(document, scene,
            *definition);
        Require(table.HasValue(),
            feature.displayName + ": 役割表へ戻せる"
                + (table.HasValue() ? std::string()
                                    : " (" + table.Diagnostics().front().code + " "
                                        + table.Diagnostics().front().summaryJa + " "
                                        + table.Diagnostics().front().detailsJa + ")"));
        RequireEqual(std::to_string(table.Value().rows.size()),
            std::to_string(definition->chains.size()),
            feature.displayName + ": 行の数が鎖の数と同じ");
        checked = true;
    }
    Require(checked, "面の作り方が1つ以上ある");
}

KACHA_V2_TEST(railway_nose_ho, 案内線が面を作るのに使われている)
{
    // §20 / §44。飾りの線ではないこと。
    const auto file = BuildRailwayNoseHoSampleDocument();
    const auto& snapshot = file.snapshot;
    std::vector<kachakacha::v2::base::EntityId> guides;
    for (const auto& entity : snapshot.entities) {
        const std::string& name = entity.displayName;
        if (name == "RoofCenterGuide" || name == "ShoulderGuide_L"
            || name == "ShoulderGuide_R" || name == "LowerGuide"
            || name == "SkirtGuide_L" || name == "SkirtGuide_R") {
            guides.push_back(entity.id);
        }
    }
    RequireEqual(std::to_string(guides.size()), std::string("6"), "案内線は6本");
    for (const auto& feature : snapshot.features) {
        if (feature.displayName != "NoseSurface") {
            continue;
        }
        for (const auto& guide : guides) {
            const bool used = std::find(feature.inputEntityIds.begin(),
                                  feature.inputEntityIds.end(), guide)
                != feature.inputEntityIds.end();
            Require(used, "案内線が面の入力になっている");
        }
    }
}

KACHA_V2_TEST(railway_nose_ho, 保存して開き直しても中身が残る)
{
    // TM-02 / TM-03 / TM-04 / TM-14。
    const auto archive = BuildRailwayNoseHoSampleArchive();
    Require(archive.HasValue(), "保存できる");
    const auto loaded = kachakacha::v2::io::LoadDocument(archive.Value());
    Require(loaded.HasValue(), "開き直せる");
    const auto& before = BuildRailwayNoseHoSampleDocument().snapshot;
    const auto& after = loaded.Value().snapshot;
    RequireEqual(std::to_string(after.entities.size()),
        std::to_string(before.entities.size()), "物の数が残る");
    RequireEqual(std::to_string(after.groups.size()),
        std::to_string(before.groups.size()), "まとまりの数が残る");
    // 入れ子も残る。
    int nested = 0;
    for (const auto& group : after.groups) {
        if (group.parentId.has_value()) {
            ++nested;
        }
    }
    RequireEqual(std::to_string(nested), std::string("7"), "入れ子が残る");
    Require(loaded.Value().metadata.description.find("1/80") != std::string::npos,
        "縮尺を書いてある");
    Require(loaded.Value().metadata.description.find("16.5mm") != std::string::npos,
        "ゲージを書いてある");
}

KACHA_V2_TEST_MAIN("railway_nose_ho_tests")
