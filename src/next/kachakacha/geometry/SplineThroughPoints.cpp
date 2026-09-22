#include "kachakacha/geometry/SplineThroughPoints.h"

#include <string>

namespace kachakacha::v2::geometry {

using base::MakeError;
using base::Result;

namespace {

constexpr const char* kDegenerate = "GEO-C022";

} // namespace

Result<CurveSegment> CubicBSplineThroughPoints(const std::vector<Vector3>& points)
{
    if (points.size() < 3) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "点を通るスプラインには、点が 3 つ以上要ります。",
            "いまは " + std::to_string(points.size()) + " 点です。"));
    }
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (!points[index].IsFinite()) {
            return Result<CurveSegment>::Failure(MakeError(kDegenerate,
                "点の座標に数値でない値が入っています。", {}));
        }
        if (index > 0 && Distance(points[index - 1], points[index]) <= 1.0e-9) {
            return Result<CurveSegment>::Failure(MakeError(kDegenerate,
                "続けて同じ場所の点があるので、通る向きが決まりません。",
                std::to_string(index) + " 番目と " + std::to_string(index + 1)
                    + " 番目の点を離してください。"));
        }
    }
    const std::size_t m = points.size() - 1;   // 区切りの数
    std::vector<Vector3> control(m + 3);
    control[1] = points.front();
    control[m + 1] = points.back();
    // 未知は P_2 .. P_m(m - 1 個)。式 j(= 2..m): P_{j-1} + 4 P_j + P_{j+1} = 6 Q_{j-1}。
    const std::size_t unknowns = m - 1;
    std::vector<double> upper(unknowns, 0.0);
    std::vector<Vector3> right(unknowns);
    for (std::size_t row = 0; row < unknowns; ++row) {
        const std::size_t j = row + 2;
        Vector3 value = points[j - 1] * 6.0;
        if (row == 0) {
            value = value - control[1];
        }
        if (row + 1 == unknowns) {
            value = value - control[m + 1];
        }
        right[row] = value;
    }
    // 三重対角(下 1・対角 4・上 1)を前進消去して後退代入する(Thomas)。
    std::vector<double> diagonal(unknowns, 4.0);
    for (std::size_t row = 0; row < unknowns; ++row) {
        upper[row] = row + 1 < unknowns ? 1.0 : 0.0;
    }
    for (std::size_t row = 1; row < unknowns; ++row) {
        const double factor = 1.0 / diagonal[row - 1];
        diagonal[row] -= factor * upper[row - 1];
        right[row] = right[row] - right[row - 1] * factor;
    }
    std::vector<Vector3> solved(unknowns);
    for (std::size_t back = unknowns; back > 0; --back) {
        const std::size_t row = back - 1;
        Vector3 value = right[row];
        if (row + 1 < unknowns) {
            value = value - solved[row + 1] * upper[row];
        }
        solved[row] = value * (1.0 / diagonal[row]);
    }
    for (std::size_t row = 0; row < unknowns; ++row) {
        control[row + 2] = solved[row];
    }
    // 自然な終わり方: P_0 = 2 P_1 - P_2、P_{m+2} = 2 P_{m+1} - P_m。
    control[0] = control[1] * 2.0 - control[2];
    control[m + 2] = control[m + 1] * 2.0 - control[m];
    return CurveSegment::MakeCubicBSpline(std::move(control));
}

} // namespace kachakacha::v2::geometry
