#include "kachakacha/fabrication/SurfaceProjection.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <cmath>
#include <limits>
#include <optional>

namespace kachakacha::v2::fabrication {

using base::MakeError;
using base::Result;
using geometry::CurveSegment;
using geometry::Vector3;

namespace {

//! 直線(origin + t·direction、t は正負どちらも)と三角形の交点の t。無ければ空。
[[nodiscard]] std::optional<double> RayTriangle(const Vector3& origin,
    const Vector3& direction, const Vector3& a, const Vector3& b, const Vector3& c)
{
    // Möller–Trumbore。両面とも当てる。
    const Vector3 edge1 = b - a;
    const Vector3 edge2 = c - a;
    const Vector3 p = Cross(direction, edge2);
    const double determinant = Dot(edge1, p);
    if (std::abs(determinant) < 1.0e-14) {
        return std::nullopt;
    }
    const double inverse = 1.0 / determinant;
    const Vector3 s = origin - a;
    const double u = Dot(s, p) * inverse;
    constexpr double kSlack = 1.0e-9;
    if (u < -kSlack || u > 1.0 + kSlack) {
        return std::nullopt;
    }
    const Vector3 q = Cross(s, edge1);
    const double v = Dot(direction, q) * inverse;
    if (v < -kSlack || u + v > 1.0 + kSlack) {
        return std::nullopt;
    }
    return Dot(edge2, q) * inverse;
}

} // namespace

Result<Vector3> ProjectPointOntoSampledSurface(const SurfacePatchSamples& samples,
    const Vector3& point, const Vector3& direction)
{
    using Out = Result<Vector3>;
    if (!samples.Valid()) {
        return Out::Failure(MakeError(kProjectionBadInput, "面の標本が壊れています。",
            "行と列がそれぞれ2つ以上、点の数が行×列でなければなりません。"));
    }
    const double length = direction.Length();
    if (!(length > 1.0e-12) || !std::isfinite(length)) {
        return Out::Failure(MakeError(kProjectionBadInput, "落とす向きが決まりません。",
            "向きの長さが 0 です。"));
    }
    const Vector3 unit = direction * (1.0 / length);
    // いちばん近い当たり(|t| 最小)を採る。面の裏側から当たる場合も、近い方。
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t row = 0; row + 1 < samples.rowCount; ++row) {
        for (std::size_t column = 0; column + 1 < samples.columnCount; ++column) {
            const Vector3& p00 = samples.At(row, column);
            const Vector3& p01 = samples.At(row, column + 1);
            const Vector3& p10 = samples.At(row + 1, column);
            const Vector3& p11 = samples.At(row + 1, column + 1);
            for (const auto t : {RayTriangle(point, unit, p00, p01, p11),
                     RayTriangle(point, unit, p00, p11, p10)}) {
                if (t.has_value() && std::abs(*t) < std::abs(best)) {
                    best = *t;
                }
            }
        }
    }
    if (!std::isfinite(best)) {
        return Out::Failure(MakeError(kProjectionMisses, "その線は面の上に落ちません。",
            "点 (" + std::to_string(point.x) + ", " + std::to_string(point.y) + ", "
                + std::to_string(point.z) + ") から向きに沿って進んでも面に当たりません。"));
    }
    return Out::Success(point + unit * best);
}

Result<std::vector<CurveSegment>> ProjectCurvesOntoSampledSurface(
    const SurfacePatchSamples& samples, const std::vector<CurveSegment>& curves,
    const Vector3& direction, double toleranceMm)
{
    using Out = Result<std::vector<CurveSegment>>;
    if (curves.empty()) {
        return Out::Failure(MakeError(kProjectionBadInput, "落とす線がありません。", {}));
    }
    std::vector<CurveSegment> made;
    for (const CurveSegment& curve : curves) {
        const auto points = geometry::SampleCurve(curve, toleranceMm);
        std::optional<Vector3> previous;
        for (const auto& sample : points) {
            const auto landed = ProjectPointOntoSampledSurface(samples, sample.position,
                direction);
            if (!landed.HasValue()) {
                return Out::Failure(landed.Diagnostics());
            }
            if (previous.has_value()
                && (landed.Value() - *previous).Length() > 1.0e-9) {
                const auto line = CurveSegment::MakeLine(*previous, landed.Value());
                if (line.HasValue()) {
                    made.push_back(line.Value());
                }
            }
            previous = landed.Value();
        }
    }
    if (made.empty()) {
        return Out::Failure(MakeError(kProjectionMisses, "その線は面の上に落ちません。",
            "落とした点が1か所に重なり、線になりません。"));
    }
    return Out::Success(std::move(made));
}

} // namespace kachakacha::v2::fabrication
