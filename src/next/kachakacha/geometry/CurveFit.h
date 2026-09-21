#pragma once

//! 曲線の値を、core の一様 3 次 B-spline(CurveSegment の CubicBSpline)へ写す。
//!
//! core の B-spline は「節点が等間隔で、端を重ねない」一様 3 次で、区間ごとに
//! 制御点 4 つを使う(CurveSegment::Evaluate)。端の制御点は通らない。
//! 核(OCCT)の B-spline は節点の間隔も端の重ね方も自由なので、**制御点をそのまま
//! 写すと形が変わる。** ここで曲線の値に最小二乗で合わせ、外れが許容以内になるまで
//! 制御点を増やす。両端は必ず通す(線どうしのつながりを崩さない)。
//!
//! 逆向き(core → 核)は、区間ごとの 3 次ベジェにすれば厳密に写せる
//! (UniformBSplineBezierSpans)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

namespace kachakacha::v2::geometry {

inline constexpr const char* kCurveFitFailed = "GEO-F001";

struct CurveFitResult {
    CurveSegment curve;
    //! 合わせた曲線と元の曲線の、同じ t での距離の最大(mm)。
    double maximumErrorMm = 0.0;
    std::size_t controlPoints = 0;
};

//! 0..1 の t で点を返す曲線を、許容以内の一様 3 次 B-spline にする。
//!
//! preferredControlPoints が 4 以上なら、まずその数で合わせる(元が同じ間隔の
//! B-spline なら、その数で厳密に一致する)。許容に届かなければ増やしていき、
//! maximumControlPoints でも届かなければ GEO-F001 で断る(折れ線へは落とさない)。
[[nodiscard]] base::Result<CurveFitResult> FitUniformCubicBSpline(
    const std::function<Vector3(double)>& curve, double toleranceMm,
    std::size_t preferredControlPoints = 0, std::size_t maximumControlPoints = 256);

//! 一様 3 次 B-spline の区間ごとの 3 次ベジェ(区間の数 = 制御点 − 3)。厳密。
[[nodiscard]] std::vector<std::array<Vector3, 4>> UniformBSplineBezierSpans(
    const std::vector<Vector3>& controlPoints);

} // namespace kachakacha::v2::geometry
