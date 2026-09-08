// グリッド(AT-UIX-005)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/GridModel.h"

#include <cmath>
#include <string>

using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::EvaluateGrid;
using kachakacha::v2::modeling::GridDefinition;
using kachakacha::v2::modeling::MinorPointsPerCell;
using kachakacha::v2::modeling::MinorPointsPerEdge;
using kachakacha::v2::modeling::MoveGridOrigin;
using kachakacha::v2::modeling::SetGridOriginUv;
using kachakacha::v2::modeling::SetGridOriginXyz;
using kachakacha::v2::modeling::SetGridSpacing;
using kachakacha::v2::modeling::StandardPlane;
using kachakacha::v2::modeling::StandardPlaneKind;
using kachakacha::v2::modeling::WorkPlaneFrame;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] std::string FirstCode(
    const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[nodiscard]] EntityId MakePlaneId()
{
    DeterministicIdGenerator generator(1);
    return EntityId(generator.Next());
}

} // namespace

KACHA_V2_TEST(grid, 副点の数が1_2で正しい)
{
    RequireEqual(std::to_string(MinorPointsPerEdge(2)), "1", "辺の上の副点");
    RequireEqual(std::to_string(MinorPointsPerCell(2)), "3", "マスの中の副点");
}

KACHA_V2_TEST(grid, 副点の数が1_3で正しい)
{
    RequireEqual(std::to_string(MinorPointsPerEdge(3)), "2", "辺の上の副点");
    RequireEqual(std::to_string(MinorPointsPerCell(3)), "8", "マスの中の副点");
}

KACHA_V2_TEST(grid, 副点の数が1_4で正しい)
{
    RequireEqual(std::to_string(MinorPointsPerEdge(4)), "3", "辺の上の副点");
    RequireEqual(std::to_string(MinorPointsPerCell(4)), "15", "マスの中の副点");
}

KACHA_V2_TEST(grid, 副点なしなら0)
{
    RequireEqual(std::to_string(MinorPointsPerEdge(0)), "0", "辺");
    RequireEqual(std::to_string(MinorPointsPerCell(0)), "0", "マス");
}

KACHA_V2_TEST(grid, 主点と副点の間隔が出る)
{
    GridDefinition definition;
    definition.majorSpacingMm = 12.0;
    definition.subdivision = 4;
    const auto evaluated = EvaluateGrid(definition, std::nullopt, 4.0);
    Require(evaluated.HasValue(), "評価できること");
    RequireNear(evaluated.Value().majorSpacingMm, 12.0, 0.0, "主点の間隔");
    RequireNear(evaluated.Value().minorSpacingMm, 3.0, 1.0e-12, "副点の間隔");
    RequireNear(evaluated.Value().majorSpacingPx, 48.0, 1.0e-12, "画面での間隔");
}

KACHA_V2_TEST(grid, 細かすぎる副点は出さない)
{
    GridDefinition definition;
    definition.majorSpacingMm = 10.0;
    definition.subdivision = 4;   // 副点の間隔は 2.5mm
    // 1mm が 2px なら副点は 5px。6px を下回るので出さない。
    const auto tight = EvaluateGrid(definition, std::nullopt, 2.0);
    Require(tight.HasValue(), "評価できること");
    Require(!tight.Value().minorVisible, "副点は出さない");
    // 1mm が 4px なら副点は 10px。出す。
    const auto loose = EvaluateGrid(definition, std::nullopt, 4.0);
    Require(loose.HasValue(), "評価できること");
    Require(loose.Value().minorVisible, "副点を出す");
}

KACHA_V2_TEST(grid, 副点なしなら倍率に関わらず出さない)
{
    GridDefinition definition;
    definition.subdivision = 0;
    const auto evaluated = EvaluateGrid(definition, std::nullopt, 100.0);
    Require(evaluated.HasValue(), "評価できること");
    Require(!evaluated.Value().minorVisible, "出さない");
}

KACHA_V2_TEST(grid, 作業平面が動くとUVを保って付いていく)
{
    GridDefinition definition;
    definition.workPlaneId = MakePlaneId();
    definition.originUmm = 5.0;
    definition.originVmm = -3.0;

    WorkPlaneFrame plane = StandardPlane(StandardPlaneKind::XY);
    const auto before = EvaluateGrid(definition, plane, 4.0);
    Require(before.HasValue(), "評価できること");
    RequireNear(before.Value().originXyz.x, 5.0, 1.0e-12, "動く前のX");
    RequireNear(before.Value().originXyz.z, 0.0, 1.0e-12, "動く前のZ");

    // 作業平面を 20mm 持ち上げる。
    plane.origin = Vector3{0.0, 0.0, 20.0};
    const auto after = EvaluateGrid(definition, plane, 4.0);
    Require(after.HasValue(), "評価できること");
    RequireNear(after.Value().originXyz.x, 5.0, 1.0e-12, "UはそのままなのでXも同じ");
    RequireNear(after.Value().originXyz.y, -3.0, 1.0e-12, "Vもそのまま");
    RequireNear(after.Value().originXyz.z, 20.0, 1.0e-12, "平面と一緒に上がる");
}

KACHA_V2_TEST(grid, 作業平面が回ってもUVを保つ)
{
    GridDefinition definition;
    definition.workPlaneId = MakePlaneId();
    definition.originUmm = 10.0;
    definition.originVmm = 0.0;
    const WorkPlaneFrame plane = StandardPlane(StandardPlaneKind::YZ);
    const auto evaluated = EvaluateGrid(definition, plane, 4.0);
    Require(evaluated.HasValue(), "評価できること");
    // YZ 面の u は +Y。
    RequireNear(evaluated.Value().originXyz.y, 10.0, 1.0e-12, "u に沿って動く");
    RequireNear(evaluated.Value().originXyz.x, 0.0, 1.0e-12, "面の外へは出ない");
}

KACHA_V2_TEST(grid, 指している作業平面が無ければ断る)
{
    GridDefinition definition;
    definition.workPlaneId = MakePlaneId();
    const auto evaluated = EvaluateGrid(definition, std::nullopt, 4.0);
    Require(!evaluated.HasValue(), "断る");
    RequireEqual(FirstCode(evaluated.Diagnostics()), "UI-G001", "診断コード");
    Require(evaluated.Diagnostics().front().summaryJa.find("ありません")
            != std::string::npos,
        "無いと言う");
}

KACHA_V2_TEST(grid, 平面を指していなければ世界のXYで評価する)
{
    GridDefinition definition;
    definition.originUmm = 4.0;
    definition.originVmm = 7.0;
    const auto evaluated = EvaluateGrid(definition, std::nullopt, 4.0);
    Require(evaluated.HasValue(), "評価できること");
    RequireNear(evaluated.Value().originXyz.x, 4.0, 1.0e-12, "X");
    RequireNear(evaluated.Value().originXyz.y, 7.0, 1.0e-12, "Y");
    RequireNear(evaluated.Value().originXyz.z, 0.0, 1.0e-12, "Z");
}

KACHA_V2_TEST(grid, 画面で指した位置へ原点を動かせる)
{
    GridDefinition definition;
    WorkPlaneFrame plane = StandardPlane(StandardPlaneKind::XY);
    plane.origin = Vector3{0.0, 0.0, 5.0};
    const auto moved = MoveGridOrigin(definition, plane, Vector3{12.0, -4.0, 5.0});
    Require(moved.HasValue(), "動かせること");
    RequireNear(moved.Value().originUmm, 12.0, 1.0e-12, "U");
    RequireNear(moved.Value().originVmm, -4.0, 1.0e-12, "V");
}

KACHA_V2_TEST(grid, 数値でUVを入れられる)
{
    const auto moved = SetGridOriginUv(GridDefinition{}, 3.5, -2.5);
    Require(moved.HasValue(), "入れられること");
    RequireNear(moved.Value().originUmm, 3.5, 0.0, "U");
    RequireNear(moved.Value().originVmm, -2.5, 0.0, "V");
}

KACHA_V2_TEST(grid, 数値でXYZを入れられる)
{
    const WorkPlaneFrame plane = StandardPlane(StandardPlaneKind::ZX);
    const auto moved = SetGridOriginXyz(GridDefinition{}, plane, Vector3{1.0, 0.0, 6.0});
    Require(moved.HasValue(), "入れられること");
    // ZX 面の u は +Z、v は +X。
    RequireNear(moved.Value().originUmm, 6.0, 1.0e-12, "U");
    RequireNear(moved.Value().originVmm, 1.0, 1.0e-12, "V");
}

KACHA_V2_TEST(grid, 有限でない原点は断る)
{
    const auto moved = SetGridOriginUv(GridDefinition{}, std::nan(""), 0.0);
    Require(!moved.HasValue(), "断る");
    RequireEqual(FirstCode(moved.Diagnostics()), "UI-G004", "診断コード");
}

KACHA_V2_TEST(grid, 間隔と細かさを決められる)
{
    const auto changed = SetGridSpacing(GridDefinition{}, 2.5, 3);
    Require(changed.HasValue(), "決められること");
    RequireNear(changed.Value().majorSpacingMm, 2.5, 0.0, "間隔");
    RequireEqual(std::to_string(changed.Value().subdivision), "3", "細かさ");
}

KACHA_V2_TEST(grid, 間隔が0以下なら断る)
{
    Require(!SetGridSpacing(GridDefinition{}, 0.0, 2).HasValue(), "0は断る");
    Require(!SetGridSpacing(GridDefinition{}, -5.0, 2).HasValue(), "負は断る");
    RequireEqual(FirstCode(SetGridSpacing(GridDefinition{}, 0.0, 2).Diagnostics()),
        "UI-G002", "診断コード");
}

KACHA_V2_TEST(grid, 知らない細かさは断る)
{
    for (const int subdivision : {1, 5, 8, -2}) {
        const auto changed = SetGridSpacing(GridDefinition{}, 10.0, subdivision);
        Require(!changed.HasValue(),
            "1/" + std::to_string(subdivision) + " は断る");
        RequireEqual(FirstCode(changed.Diagnostics()), "UI-G003", "診断コード");
    }
}

KACHA_V2_TEST(grid, 同じ入力からは毎回同じ評価になる)
{
    GridDefinition definition;
    definition.majorSpacingMm = 7.0;
    definition.subdivision = 4;
    const WorkPlaneFrame plane = StandardPlane(StandardPlaneKind::XY);
    const auto first = EvaluateGrid(definition, plane, 3.0);
    Require(first.HasValue(), "評価できること");
    for (int attempt = 0; attempt < 5; ++attempt) {
        const auto again = EvaluateGrid(definition, plane, 3.0);
        Require(again.HasValue(), "評価できること");
        RequireNear(again.Value().minorSpacingMm, first.Value().minorSpacingMm, 0.0,
            "副点の間隔");
        Require(again.Value().minorVisible == first.Value().minorVisible, "出す出さない");
    }
}

KACHA_V2_TEST_MAIN("grid_tests")
