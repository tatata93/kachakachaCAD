#include "kachakacha/geometry/PolylineCorners.h"

#include "kachakacha/geometry/WireEdit.h"

#include <cstddef>
#include <string>

namespace kachakacha::v2::geometry {

using base::MakeError;
using base::Result;

namespace {

constexpr const char* kNoCorner = "GEO-E021";

[[nodiscard]] bool Touches(const CurveSegment& a, const CurveSegment& b, double toleranceMm)
{
    return (a.EndPoint() - b.StartPoint()).Length() <= toleranceMm;
}

} // namespace

Result<std::vector<CurveSegment>> ProcessPolylineCorners(
    const std::vector<CurveSegment>& segments, CornerStyle style, double sizeMm,
    double toleranceMm)
{
    using Out = Result<std::vector<CurveSegment>>;
    if (segments.size() < 2) {
        return Out::Failure(MakeError(kNoCorner, "落とせる角がありません。",
            "角の加工には、つながった直線が2本以上要ります。"));
    }
    std::vector<CurveSegment> work = segments;
    std::vector<CurveSegment> made;
    int processed = 0;
    const bool closed = Touches(work.back(), work.front(), toleranceMm)
        && work.size() >= 3;
    const std::size_t corners = closed ? work.size() : work.size() - 1;
    for (std::size_t index = 0; index < corners; ++index) {
        CurveSegment& first = work[index];
        CurveSegment& second = work[(index + 1) % work.size()];
        const bool lines = first.Kind() == CurveKind::Line && second.Kind() == CurveKind::Line;
        if (!lines || !Touches(first, second, toleranceMm)) {
            made.push_back(first);
            continue;   // 直線どうしでない角、離れた辺は触らない。
        }
        const auto corner = style == CornerStyle::Chamfer
            ? geometry::ChamferLines(first, second, sizeMm, toleranceMm)
            : geometry::FilletLines(first, second, sizeMm, toleranceMm);
        if (!corner.HasValue()) {
            return Out::Failure(corner.Diagnostics());
        }
        // 短くなった1本目も書き戻す。閉じた並びでは、最初の辺が最後の角でも短くなるので、
        // 両端の変更を1本に持たせるためである。
        first = corner.Value().first;
        made.push_back(first);
        made.push_back(corner.Value().corner);
        // 短くなった2本目を次の角へ渡す。渡さないと、次の角が元の長さで計算される。
        second = corner.Value().second;
        ++processed;
    }
    if (!closed) {
        made.push_back(work.back());
    } else if (processed > 0) {
        // 閉じた並びでは最初の辺も最後の角で短くなっている。先頭を差し替える。
        made.front() = work.front();
    }
    if (processed == 0) {
        return Out::Failure(MakeError(kNoCorner, "落とせる角がありません。",
            "つながった直線どうしの角が1つもありません。"));
    }
    return Out::Success(std::move(made));
}

} // namespace kachakacha::v2::geometry
