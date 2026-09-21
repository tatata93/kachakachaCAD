// 曲線を core の一様 3 次 B-spline へ写す(geometry/CurveFit.h)。
//
// 核の B-spline は節点も端も自由なので、制御点をそのまま写すと形が変わる。
// 値に合わせて写し、両端は必ず通し、元が同じ間隔の B-spline なら厳密に戻る。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/CurveFit.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::FitUniformCubicBSpline;
using kachakacha::v2::geometry::UniformBSplineBezierSpans;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] Vector3 BezierAt(const std::array<Vector3, 4>& p, double u)
{
    const double v = 1.0 - u;
    return p[0] * (v * v * v) + p[1] * (3.0 * v * v * u) + p[2] * (3.0 * v * u * u)
        + p[3] * (u * u * u);
}

} // namespace

KACHA_V2_TEST(curve_fit, 同じ間隔のBsplineは同じ数の制御点で厳密に戻る)
{
    const std::vector<Vector3> control{{0, 0, 0}, {10, 5, 0}, {20, -3, 2}, {30, 8, 1},
        {40, 0, -2}, {50, 4, 0}, {60, 0, 0}};
    const CurveSegment original = CurveSegment::MakeCubicBSpline(control).Value();
    const auto fit = FitUniformCubicBSpline(
        [&original](double t) { return original.Evaluate(t); }, 1.0e-9, control.size());
    Require(fit.HasValue(), "写せる");
    Require(fit.Value().controlPoints == control.size(), "同じ数の制御点");
    Require(fit.Value().maximumErrorMm < 1.0e-9, "形が同じ");
    for (std::size_t i = 0; i < control.size(); ++i) {
        Require((fit.Value().curve.ControlPoints()[i] - control[i]).Length() < 1.0e-7,
            "制御点も同じ(" + std::to_string(i) + ")");
    }
}

KACHA_V2_TEST(curve_fit, 円弧のような曲線は許容以内で写し両端を通す)
{
    const auto arc = [](double t) {
        const double angle = 2.0 * t;   // 約 115 度
        return Vector3{30.0 * std::cos(angle), 30.0 * std::sin(angle), 5.0 * t};
    };
    const auto fit = FitUniformCubicBSpline(arc, 1.0e-4);
    Require(fit.HasValue(), "写せる");
    Require(fit.Value().maximumErrorMm <= 1.0e-4, "許容以内");
    Require((fit.Value().curve.Evaluate(0.0) - arc(0.0)).Length() < 1.0e-9, "始点を通す");
    Require((fit.Value().curve.Evaluate(1.0) - arc(1.0)).Length() < 1.0e-9, "終点を通す");
}

KACHA_V2_TEST(curve_fit, 区間ごとのベジェは元のBsplineと同じ形)
{
    const std::vector<Vector3> control{{0, 0, 0}, {10, 5, 0}, {20, -3, 2}, {30, 8, 1},
        {40, 0, -2}};
    const CurveSegment original = CurveSegment::MakeCubicBSpline(control).Value();
    const auto spans = UniformBSplineBezierSpans(control);
    Require(spans.size() == control.size() - 3, "区間の数");
    for (std::size_t j = 0; j < spans.size(); ++j) {
        for (int k = 0; k <= 10; ++k) {
            const double u = k / 10.0;
            const double t = (static_cast<double>(j) + u) / static_cast<double>(spans.size());
            Require((BezierAt(spans[j], u) - original.Evaluate(t)).Length() < 1.0e-9,
                "区間 " + std::to_string(j) + " の u=" + std::to_string(u));
        }
    }
}

KACHA_V2_TEST(curve_fit, 届かない許容は折れ線へ落とさず断る)
{
    // 鋭く折れた線(角)は、なめらかな B-spline では 1e-6 mm に届かない。
    const auto kinked = [](double t) {
        return t < 0.5 ? Vector3{t * 20.0, 0.0, 0.0} : Vector3{10.0, (t - 0.5) * 20.0, 0.0};
    };
    const auto fit = FitUniformCubicBSpline(kinked, 1.0e-6, 0, 16);
    Require(!fit.HasValue(), "断る");
    Require(fit.Diagnostics().front().code == "GEO-F001", "決まった番号");
}

KACHA_V2_TEST_MAIN("curve_fit_tests")
