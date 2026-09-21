#include "kachakacha/geometry/ChainTrim.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kachakacha::v2::geometry {
namespace {

constexpr double kEndEpsilon = 1.0e-9;

//! t より後ろ(t..1)。t がほぼ 0 なら曲線そのもの。
[[nodiscard]] base::Result<CurveSegment> TailFrom(const CurveSegment& segment, double t)
{
    if (t <= kEndEpsilon) {
        return base::Result<CurveSegment>::Success(segment);
    }
    const auto split = segment.Split(t);
    if (!split.HasValue() || !split.Value().second) {
        return base::Result<CurveSegment>::Failure(base::MakeError("GEO-T001",
            "線を途中で切れませんでした。", "t = " + std::to_string(t)));
    }
    return base::Result<CurveSegment>::Success(*split.Value().second);
}

//! t より前(0..t)。t がほぼ 1 なら曲線そのもの。
[[nodiscard]] base::Result<CurveSegment> HeadTo(const CurveSegment& segment, double t)
{
    if (t >= 1.0 - kEndEpsilon) {
        return base::Result<CurveSegment>::Success(segment);
    }
    const auto split = segment.Split(t);
    if (!split.HasValue() || !split.Value().first) {
        return base::Result<CurveSegment>::Failure(base::MakeError("GEO-T001",
            "線を途中で切れませんでした。", "t = " + std::to_string(t)));
    }
    return base::Result<CurveSegment>::Success(*split.Value().first);
}

} // namespace

ChainPosition ClosestPositionOnChain(const std::vector<CurveSegment>& chain,
    const Vector3& point)
{
    ChainPosition best;
    best.distance = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < chain.size(); ++index) {
        const ClosestPointResult closest = chain[index].ClosestPoint(point);
        if (closest.distance < best.distance) {
            best.segment = index;
            best.parameter = closest.parameter;
            best.point = closest.point;
            best.distance = closest.distance;
        }
    }
    return best;
}

bool ChainPositionBefore(const ChainPosition& a, const ChainPosition& b) noexcept
{
    return a.segment != b.segment ? a.segment < b.segment : a.parameter < b.parameter;
}

base::Result<std::vector<CurveSegment>> TrimChainBetween(const std::vector<CurveSegment>& chain,
    const Vector3& from, const Vector3& to, double toleranceMm)
{
    using Out = base::Result<std::vector<CurveSegment>>;
    if (chain.empty()) {
        return Out::Failure(base::MakeError("GEO-T002", "切り出す線がありません。", {}));
    }
    ChainPosition a = ClosestPositionOnChain(chain, from);
    ChainPosition b = ClosestPositionOnChain(chain, to);
    if (ChainPositionBefore(b, a)) {
        std::swap(a, b);
    }
    std::vector<CurveSegment> out;
    if (a.segment == b.segment) {
        const CurveSegment& segment = chain[a.segment];
        const auto head = HeadTo(segment, b.parameter);
        if (!head.HasValue()) {
            return Out::Failure(head.Diagnostics());
        }
        // head は 0..b。その中の a の位置は、点から測り直す(分けた曲線の t が
        // 元の t に比例するとは限らない)。
        const double local = head.Value().ClosestPoint(a.point).parameter;
        const auto piece = TailFrom(head.Value(), local);
        if (!piece.HasValue()) {
            return Out::Failure(piece.Diagnostics());
        }
        out.push_back(piece.Value());
    } else {
        const auto tail = TailFrom(chain[a.segment], a.parameter);
        if (!tail.HasValue()) {
            return Out::Failure(tail.Diagnostics());
        }
        out.push_back(tail.Value());
        for (std::size_t index = a.segment + 1; index < b.segment; ++index) {
            out.push_back(chain[index]);
        }
        const auto head = HeadTo(chain[b.segment], b.parameter);
        if (!head.HasValue()) {
            return Out::Failure(head.Diagnostics());
        }
        out.push_back(head.Value());
    }
    double length = 0.0;
    for (const CurveSegment& segment : out) {
        length += segment.TotalLength(std::max(toleranceMm * 0.1, 1.0e-6));
    }
    if (!(length > toleranceMm)) {
        return Out::Failure(base::MakeError("GEO-T003", "切り出した部分が短すぎます。",
            std::to_string(length) + " mm"));
    }
    return Out::Success(std::move(out));
}

} // namespace kachakacha::v2::geometry
