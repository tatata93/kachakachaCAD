// 治具(app/SurfaceJig.h)。V1 の body_surface_jig を、離した面 + 厚みへ翻訳する。
#include "kachakacha/app/SurfaceJig.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::PlanSurfaceJig;
using kachakacha::v2::fabrication::ThicknessPlacement;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

KACHA_V2_TEST(surface_jig, 表側の治具はすき間だけ離して外側へ厚みを付ける)
{
    const auto plan = PlanSurfaceJig(0.5, 3.0, 1);
    Require(plan.HasValue(), "作れる");
    RequireNear(plan.Value().offsetDistanceMm, 0.5, 1e-12, "表側へ 0.5 離す");
    RequireNear(plan.Value().thicknessMm, 3.0, 1e-12, "厚みは 3");
    Require(plan.Value().placement == ThicknessPlacement::Outside, "外側へ伸ばす");
    Require(plan.Value().NeedsOffsetSurface(), "離した面が要る");
}

KACHA_V2_TEST(surface_jig, 厚みが負なら裏側の治具になる)
{
    // V1 の JigSide::Negative。すき間も同じ側へ離す。
    const auto plan = PlanSurfaceJig(0.5, -3.0, 1);
    Require(plan.HasValue(), "作れる");
    RequireNear(plan.Value().offsetDistanceMm, -0.5, 1e-12, "裏側へ 0.5 離す");
    RequireNear(plan.Value().thicknessMm, 3.0, 1e-12, "厚みは大きさだけ");
    Require(plan.Value().placement == ThicknessPlacement::Inside, "内側へ伸ばす");
}

KACHA_V2_TEST(surface_jig, すき間0なら離さず元の面に当てる)
{
    const auto plan = PlanSurfaceJig(0.0, 2.0, 1);
    Require(plan.HasValue(), "作れる");
    Require(!plan.Value().NeedsOffsetSurface(), "離した面は要らない");
    RequireNear(plan.Value().offsetDistanceMm, 0.0, 1e-12, "離さない");
}

KACHA_V2_TEST(surface_jig, 負のすき間と厚み0と面の数が1でないものは断る)
{
    RequireEqual(PlanSurfaceJig(-0.5, 3.0, 1).Diagnostics().front().code,
        std::string("JIG-E001"), "負のすき間");
    RequireEqual(PlanSurfaceJig(0.5, 0.0, 1).Diagnostics().front().code,
        std::string("JIG-E002"), "厚み 0");
    RequireEqual(PlanSurfaceJig(0.5, 3.0, 0).Diagnostics().front().code,
        std::string("JIG-E003"), "面を選んでいない");
    RequireEqual(PlanSurfaceJig(0.5, 3.0, 2).Diagnostics().front().code,
        std::string("JIG-E003"), "面が 2 枚");
}

KACHA_V2_TEST_MAIN("surface_jig")
