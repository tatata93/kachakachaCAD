#include "kachakacha/app/SurfaceJig.h"

#include <cmath>
#include <string>

namespace kachakacha::v2::app {

using base::MakeError;
using base::Result;
using fabrication::ThicknessPlacement;

namespace {

constexpr const char* kBadClearance = "JIG-E001";
constexpr const char* kBadThickness = "JIG-E002";
constexpr const char* kBadSurface = "JIG-E003";

} // namespace

Result<SurfaceJigPlan> PlanSurfaceJig(double clearanceMm, double thicknessMm,
    std::size_t surfaceCount)
{
    using Out = Result<SurfaceJigPlan>;
    if (surfaceCount != 1) {
        return Out::Failure(MakeError(kBadSurface,
            "治具の元にする形状ガイドの面を1つ選んでください。",
            surfaceCount == 0 ? std::string("いま面を選んでいません。")
                              : std::to_string(surfaceCount) + " 枚選んでいます。"));
    }
    if (!std::isfinite(clearanceMm) || clearanceMm < 0.0) {
        return Out::Failure(MakeError(kBadClearance,
            "治具のすき間は 0 以上にしてください。",
            std::to_string(clearanceMm) + " mm。面にぴったり当てるなら 0 です。"));
    }
    if (!std::isfinite(thicknessMm) || thicknessMm == 0.0) {
        return Out::Failure(MakeError(kBadThickness, "治具の厚みは 0 にできません。",
            "正なら面の表側、負なら裏側に当て板を作ります。どちら側かがここで決まります。"));
    }
    SurfaceJigPlan plan;
    // 側は厚みの符号(V1 の JigSide)。すき間も同じ側へ離す。
    plan.offsetDistanceMm = thicknessMm > 0.0 ? clearanceMm : -clearanceMm;
    plan.thicknessMm = std::abs(thicknessMm);
    plan.placement = thicknessMm > 0.0 ? ThicknessPlacement::Outside
                                       : ThicknessPlacement::Inside;
    return Out::Success(plan);
}

} // namespace kachakacha::v2::app
