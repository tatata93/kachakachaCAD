#include "kachakacha/view/FacingPlan.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::view {

using base::MakeError;
using base::Result;
using geometry::Cross;
using geometry::Dot;
using geometry::Normalized;
using geometry::Vector3;

namespace {

//! 画面に収める大きさの下限。点1つや短い線でも、際限なく寄らないようにする。
constexpr double kMinimumSpanMm = 10.0;
//! 平行とみなすしきい。これ以下の外積は「一直線」と読む。
constexpr double kDegenerateCrossSquared = 1.0e-18;

[[nodiscard]] bool AllFinite(const std::vector<Vector3>& points)
{
    return std::all_of(points.begin(), points.end(),
        [](const Vector3& point) { return point.IsFinite(); });
}

//! 線の向きと直交する法線のうち、いまの視線にいちばん近いもの。
//! 一直線に並んだ点しかないときに使う。向きを勝手に変えないための逃がし方。
[[nodiscard]] Vector3 NormalAcrossLine(const Vector3& lineDirection, const Vector3& viewDirection)
{
    const Vector3 along = Normalized(lineDirection);
    Vector3 across = viewDirection - along * Dot(viewDirection, along);
    if (across.LengthSquared() > kDegenerateCrossSquared) {
        return across;
    }
    // 視線が線と重なっている。決まった順で逃がす(押すたびに変わらないため)。
    const Vector3 fallbacks[]{{0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}};
    for (const Vector3& candidate : fallbacks) {
        across = candidate - along * Dot(candidate, along);
        if (across.LengthSquared() > kDegenerateCrossSquared) {
            return across;
        }
    }
    return Vector3{0.0, 0.0, 1.0};
}

} // namespace

Result<Vector3> BestFitNormal(const std::vector<Vector3>& points,
    const Vector3& currentViewDirection)
{
    using Out = Result<Vector3>;
    if (points.empty()) {
        return Out::Failure(MakeError(kFacingNoPoints, "正対する先の形が空です。",
            "選んだものから点が1つも取れませんでした。"));
    }
    if (!AllFinite(points)) {
        return Out::Failure(MakeError("UI-V001", "視点の値に数値でないものが入っています。",
            "正対する先の点に数値でない値があります。"));
    }
    Vector3 center;
    for (const Vector3& point : points) {
        center = center + point;
    }
    center = center * (1.0 / static_cast<double>(points.size()));

    // V1 と同じ。重心から出る2本の外積がいちばん大きい組を平面とみなす。
    Vector3 normal;
    double bestSquared = 0.0;
    for (std::size_t first = 0; first < points.size(); ++first) {
        for (std::size_t second = first + 1; second < points.size(); ++second) {
            const Vector3 candidate = Cross(points[first] - center, points[second] - center);
            if (candidate.LengthSquared() > bestSquared) {
                bestSquared = candidate.LengthSquared();
                normal = candidate;
            }
        }
    }
    if (bestSquared > kDegenerateCrossSquared) {
        return Out::Success(normal);
    }
    // 一直線、または点が1つ。平面は決まらないので、いまの視線から作る。
    Vector3 direction;
    for (const Vector3& point : points) {
        if ((point - points.front()).LengthSquared() > kDegenerateCrossSquared) {
            direction = point - points.front();
            break;
        }
    }
    if (direction.LengthSquared() <= kDegenerateCrossSquared) {
        // 点1つ。向きは変えずに、いまの視線のまま寄るだけにする。
        if (currentViewDirection.LengthSquared() <= kDegenerateCrossSquared) {
            return Out::Failure(MakeError(kFacingNoPlane,
                "選んだものの平面が決まりません。",
                "点が1つで、いまの視線も決まっていません。"));
        }
        return Out::Success(currentViewDirection * -1.0);
    }
    return Out::Success(NormalAcrossLine(direction, currentViewDirection));
}

Result<FacingPlan> PlanFacingSelection(const std::vector<Vector3>& points, const Vector3& normal,
    const Vector3& uAxisHint, const Vector3& currentViewDirection)
{
    using Out = Result<FacingPlan>;
    if (points.empty()) {
        return Out::Failure(MakeError(kFacingNoPoints, "正対する先の形が空です。",
            "選んだものから点が1つも取れませんでした。"));
    }
    if (!AllFinite(points) || !normal.IsFinite()) {
        return Out::Failure(MakeError("UI-V001", "視点の値に数値でないものが入っています。",
            "正対する先の点か法線に数値でない値があります。"));
    }
    Vector3 facing = Normalized(normal);
    if (facing == Vector3{}) {
        return Out::Failure(MakeError(kFacingNoPlane, "選んだものの平面が決まりません。",
            "法線の長さが0です。"));
    }
    // いま見ている側に留まる。裏へ回り込むと、押すたびに模型が裏返ったように見える。
    if (Dot(facing, currentViewDirection) > 0.0) {
        facing = facing * -1.0;
    }
    // 上向きは面の上の「縦」。横の見当から作る。決まらなければ ViewOrientation に任せる。
    const Vector3 up = Cross(facing, uAxisHint);
    const auto orientation = OrientationFacing(facing,
        up.LengthSquared() > kDegenerateCrossSquared ? up : Vector3{0.0, 0.0, 1.0});
    if (!orientation.HasValue()) {
        return Out::Failure(orientation.Diagnostics());
    }

    // 画面の軸で囲みを測る。姿勢が決まってからでないと、収まる大きさが出せない。
    const Vector3 right = RightOf(orientation.Value());
    const Vector3 top = UpOf(orientation.Value());
    double minRight = Dot(points.front(), right);
    double maxRight = minRight;
    double minTop = Dot(points.front(), top);
    double maxTop = minTop;
    Vector3 minimum = points.front();
    Vector3 maximum = points.front();
    for (const Vector3& point : points) {
        minRight = std::min(minRight, Dot(point, right));
        maxRight = std::max(maxRight, Dot(point, right));
        minTop = std::min(minTop, Dot(point, top));
        maxTop = std::max(maxTop, Dot(point, top));
        minimum = Vector3{std::min(minimum.x, point.x), std::min(minimum.y, point.y),
            std::min(minimum.z, point.z)};
        maximum = Vector3{std::max(maximum.x, point.x), std::max(maximum.y, point.y),
            std::max(maximum.z, point.z)};
    }
    FacingPlan plan;
    plan.orientation = orientation.Value();
    plan.center = (minimum + maximum) * 0.5;
    plan.spanMm = std::max({maxRight - minRight, maxTop - minTop, kMinimumSpanMm});
    return Out::Success(plan);
}

} // namespace kachakacha::v2::view
