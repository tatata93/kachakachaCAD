#include "kachakacha/app/DirectWireEntry.h"

#include "kachakacha/geometry/ArcBuilders.h"

#include <cmath>
#include <string>

namespace kachakacha::v2::app {

using base::MakeError;
using base::Result;
using geometry::CurveSegment;
using geometry::Vector3;

namespace {

constexpr const char* kNeedPoints = "UI-D001";
constexpr const char* kBadRadius = "UI-D002";

[[nodiscard]] bool Finite(const Vector3& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

} // namespace

std::string_view DirectWireKindNameJa(DirectWireKind kind) noexcept
{
    switch (kind) {
    case DirectWireKind::PlanarLine:   return "平面上の直線";
    case DirectWireKind::SpatialLine:  return "3D 直線";
    case DirectWireKind::PlanarCircle: return "平面上の円";
    case DirectWireKind::PlanarArc:    return "平面上の円弧(3点)";
    case DirectWireKind::PlanarBezier: return "平面上のベジェ";
    }
    return "不明";
}

const std::vector<DirectWireKind>& DirectWireKinds()
{
    static const std::vector<DirectWireKind> kinds{DirectWireKind::PlanarLine,
        DirectWireKind::SpatialLine, DirectWireKind::PlanarCircle, DirectWireKind::PlanarArc,
        DirectWireKind::PlanarBezier};
    return kinds;
}

int DirectWirePointCount(DirectWireKind kind) noexcept
{
    switch (kind) {
    case DirectWireKind::PlanarLine:   return 2;
    case DirectWireKind::SpatialLine:  return 2;
    case DirectWireKind::PlanarCircle: return 1;
    case DirectWireKind::PlanarArc:    return 3;
    case DirectWireKind::PlanarBezier: return 4;
    }
    return 0;
}

bool DirectWireIsPlanar(DirectWireKind kind) noexcept
{
    return kind != DirectWireKind::SpatialLine;
}

Result<std::vector<CurveSegment>> BuildDirectWire(const DirectWireRequest& request,
    const modeling::WorkPlaneFrame& plane)
{
    using Out = Result<std::vector<CurveSegment>>;
    const int needed = DirectWirePointCount(request.kind);
    if (static_cast<int>(request.points.size()) < needed) {
        return Out::Failure(MakeError(kNeedPoints, "点が足りません。",
            std::string(DirectWireKindNameJa(request.kind)) + "には点が"
                + std::to_string(needed) + "つ要ります。"));
    }
    // 欄の値を世界座標へ。平面上のものは作業平面の (u, v)。
    std::vector<Vector3> world;
    world.reserve(static_cast<std::size_t>(needed));
    for (int index = 0; index < needed; ++index) {
        const Vector3& point = request.points[static_cast<std::size_t>(index)];
        if (!Finite(point)) {
            return Out::Failure(MakeError(kNeedPoints, "点が足りません。",
                "点" + std::to_string(index + 1) + "に数値でない値が入っています。"));
        }
        world.push_back(DirectWireIsPlanar(request.kind) ? plane.PointAt(point.x, point.y)
                                                          : point);
    }
    const auto one = [](Result<CurveSegment> made) {
        if (!made.HasValue()) {
            return Out::Failure(made.Diagnostics());
        }
        std::vector<CurveSegment> segments;
        segments.push_back(made.Value());
        return Out::Success(std::move(segments));
    };
    switch (request.kind) {
    case DirectWireKind::PlanarLine:
    case DirectWireKind::SpatialLine:
        return one(CurveSegment::MakeLine(world[0], world[1]));
    case DirectWireKind::PlanarCircle:
        if (!(request.radiusMm > 0.0) || !std::isfinite(request.radiusMm)) {
            return Out::Failure(MakeError(kBadRadius, "半径が正の数ではありません。",
                "半径は 0 より大きい数で入れてください。"));
        }
        return one(CurveSegment::MakeCircle(world[0], plane.normal, plane.uAxis,
            request.radiusMm));
    case DirectWireKind::PlanarArc:
        return one(geometry::ArcThroughThreePoints(world[0], world[1], world[2]));
    case DirectWireKind::PlanarBezier:
        return one(CurveSegment::MakeCubicBezier(world));
    }
    return Out::Failure(MakeError(kNeedPoints, "点が足りません。", "知らない種類です。"));
}

} // namespace kachakacha::v2::app
