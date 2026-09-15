// 出来た面が、指定した線からどれだけ外れてよいか
// (modeling/SurfaceDeviationLimit.h、CODEX_REVIEW_REQUIRED の答え)。
//
// これまでは作り方によらず 0.0001mm 固定だった。数値誤差の桁である。
// 案内付きロフトと曲線網は作りからして近似なので、この値では
// **どんな形でも断られる。**命令の一覧にあるのに使えない状態だった。
#include "kachakacha/modeling/SurfaceDeviationLimit.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::modeling::FidelityOf;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::modeling::kFabricationDeviationMm;
using kachakacha::v2::modeling::SurfaceDeviationIsWorthSaying;
using kachakacha::v2::modeling::SurfaceDeviationLimitMm;
using kachakacha::v2::modeling::SurfaceDeviationNoteJa;
using kachakacha::v2::modeling::SurfaceFidelity;
using kachakacha::v2::test::Require;

KACHA_V2_TEST(surface_deviation, 通す作り方の許容はこれまでどおり)
{
    // 平面・ルールド・ロフト・回転体は、指定した線をそのまま通る面を張る。
    // 外れたら本当に壊れている。ここを緩めてはいけない。
    const GeometryTolerance tolerance;
    const GuideSurfaceMethod exact[] = {GuideSurfaceMethod::PlanarBoundary,
        GuideSurfaceMethod::RuledSections, GuideSurfaceMethod::LoftSections,
        GuideSurfaceMethod::Revolve};
    for (const auto method : exact) {
        Require(FidelityOf(method) == SurfaceFidelity::Interpolating, "通す作り方");
        Require(SurfaceDeviationLimitMm(method, tolerance) == 1.0e-4,
            "許容は 0.0001mm のまま");
    }
}

KACHA_V2_TEST(surface_deviation, 近づける作り方は後の工程が許す量まで)
{
    // 板材の曲げ近似は既定で面から 0.25mm 外れることを許している。
    // その次の工程が 0.25mm 許しているのに、手前の面に 0.0001mm を求めるのは
    // 2500 倍ちぐはぐである。
    const GeometryTolerance tolerance;
    const GuideSurfaceMethod near[] = {GuideSurfaceMethod::GuidedLoft,
        GuideSurfaceMethod::GordonNetwork, GuideSurfaceMethod::BoundaryFill};
    for (const auto method : near) {
        Require(FidelityOf(method) == SurfaceFidelity::Approximating, "近づける作り方");
        Require(SurfaceDeviationLimitMm(method, tolerance) == kFabricationDeviationMm,
            "許容は板材の曲げ近似と同じ 0.25mm");
    }
    // HO の前頭部で実際に測った 0.19mm は通る。0.30mm は通らない。
    const double limit = SurfaceDeviationLimitMm(GuideSurfaceMethod::GuidedLoft, tolerance);
    Require(0.19 < limit, "実測 0.19mm は通る");
    Require(0.30 > limit, "0.30mm は通らない");
}

KACHA_V2_TEST(surface_deviation, 通ったときも外れた量は必ず言う)
{
    // 黙って通さない。できないことを、できたことにしない。
    const GeometryTolerance tolerance;
    const auto note = SurfaceDeviationNoteJa(GuideSurfaceMethod::GuidedLoft, 0.19, tolerance);
    Require(!note.empty(), "言う");
    Require(note.find("0.190") != std::string::npos,
        std::string("量をそのまま出す: ") + note);
    Require(note.find("近づけて") != std::string::npos, "近似であることも言う");
}

KACHA_V2_TEST(surface_deviation, 数値誤差の桁は言わない)
{
    // 毎回出ると読まなくなる。読まれない知らせは、無いのと同じである。
    const GeometryTolerance tolerance;
    Require(!SurfaceDeviationIsWorthSaying(GuideSurfaceMethod::GuidedLoft, 1.0e-9,
                tolerance),
        "誤差の桁");
    Require(SurfaceDeviationNoteJa(GuideSurfaceMethod::GuidedLoft, 1.0e-9, tolerance).empty(),
        "言わない");
    // 通す作り方は、そもそも外れたら断られるので、通ったときに言うことはない。
    Require(!SurfaceDeviationIsWorthSaying(GuideSurfaceMethod::LoftSections, 0.19,
                tolerance),
        "通す作り方では言わない");
}

KACHA_V2_TEST_MAIN("surface_deviation_limit_tests")
