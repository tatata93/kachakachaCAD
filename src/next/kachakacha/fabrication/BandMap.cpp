//! 3D 点を帯メッシュの最近三角形へ対応付ける(BandFold.h の MapPointToBandState)。
//! V1 の MapPointToPartMeshState をそのまま移す。接続部分の変形に使う。

#include "kachakacha/fabrication/BandFold.h"

#include <cmath>
#include <limits>

namespace kachakacha::v2::fabrication {
namespace {

using base::MakeError;
using base::Result;

//! 三角形 (a,b,c) への最近点の重心座標 (v,w)。標準的な閉形式(Ericson)。
struct Barycentric {
    double v = 0.0;
    double w = 0.0;
};

[[nodiscard]] Barycentric ClosestOnTriangle(const Vector3& point, const Vector3& a,
    const Vector3& b, const Vector3& c)
{
    Barycentric result;
    const Vector3 ab = b - a;
    const Vector3 ac = c - a;
    const Vector3 ap = point - a;
    const double d1 = Dot(ab, ap);
    const double d2 = Dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) {
        return result;
    }
    const Vector3 bp = point - b;
    const double d3 = Dot(ab, bp);
    const double d4 = Dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) {
        result.v = 1.0;
        return result;
    }
    const Vector3 cp = point - c;
    const double d5 = Dot(ab, cp);
    const double d6 = Dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) {
        result.w = 1.0;
        return result;
    }
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        result.v = d1 / (d1 - d3);
        return result;
    }
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        result.w = d2 / (d2 - d6);
        return result;
    }
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        const double t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        result.v = 1.0 - t;
        result.w = t;
        return result;
    }
    const double denominator = va + vb + vc;
    if (std::abs(denominator) > 1.0e-18) {
        result.v = vb / denominator;
        result.w = vc / denominator;
    }
    return result;
}

} // namespace

Result<BandMappedPoint> MapPointToBandState(const BandMesh& mesh,
    const std::vector<std::vector<Vector3>>& state, const Vector3& point)
{
    using Out = Result<BandMappedPoint>;
    if (mesh.rows < 2 || mesh.columns < 2 || static_cast<int>(state.size()) != mesh.rows) {
        return Out::Failure(MakeError(kBandBadSamples,
            "近似メッシュと状態の位相が一致していません。", {}));
    }
    BandMappedPoint best;
    best.distanceMm = std::numeric_limits<double>::max();
    const auto consider = [&](int band, const Vector3& a3, const Vector3& b3,
                              const Vector3& c3, const Vector3& aT, const Vector3& bT,
                              const Vector3& cT) {
        const Barycentric bc = ClosestOnTriangle(point, a3, b3, c3);
        const Vector3 closest = a3 + (b3 - a3) * bc.v + (c3 - a3) * bc.w;
        const double distance = (point - closest).Length();
        if (distance < best.distanceMm) {
            best.distanceMm = distance;
            best.band = band;
            best.point = aT + (bT - aT) * bc.v + (cT - aT) * bc.w;
        }
    };
    for (int band = 0; band + 1 < mesh.rows; ++band) {
        const auto& bottom3 = mesh.world[static_cast<std::size_t>(band)];
        const auto& top3 = mesh.world[static_cast<std::size_t>(band) + 1];
        const auto& bottomT = state[static_cast<std::size_t>(band)];
        const auto& topT = state[static_cast<std::size_t>(band) + 1];
        for (std::size_t column = 0; column + 1 < static_cast<std::size_t>(mesh.columns);
             ++column) {
            consider(band, bottom3[column], top3[column], bottom3[column + 1],
                bottomT[column], topT[column], bottomT[column + 1]);
            consider(band, top3[column], bottom3[column + 1], top3[column + 1],
                topT[column], bottomT[column + 1], topT[column + 1]);
        }
    }
    return Out::Success(best);
}

} // namespace kachakacha::v2::fabrication
