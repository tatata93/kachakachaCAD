#include "kachakacha/modeling/WorkPlane.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace kachakacha::v2::modeling {

using base::MakeError;
using base::Result;
using geometry::Cross;
using geometry::Dot;
using geometry::Normalized;

namespace {

constexpr const char* kBadInput = "GEO-P001";       //!< 材料が足りない・数でない
constexpr const char* kDegenerate = "GEO-P002";     //!< つぶれている(長さ0、一直線)
constexpr const char* kNotParallel = "GEO-P003";    //!< 平行でない2面
constexpr const char* kNotCylinder = "GEO-P004";    //!< 円筒(円錐・球)でない面
constexpr const char* kNoCurvature = "GEO-P005";    //!< 曲率法線が定義できない
constexpr const char* kNotCoplanar = "GEO-P006";    //!< 同一平面上にない2辺
constexpr const char* kUnknownMethod = "GEO-P007";  //!< 知らない作り方

[[nodiscard]] bool AllFinite(const std::vector<Vector3>& points)
{
    for (const Vector3& point : points) {
        if (!point.IsFinite()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool FrameIsFinite(const WorkPlaneFrame& frame)
{
    return frame.origin.IsFinite() && frame.uAxis.IsFinite() && frame.vAxis.IsFinite()
        && frame.normal.IsFinite();
}

//! 与えた向きに一番そぐわない座標軸。u 軸の種を決めるのに使う。
//! 同じ法線からは必ず同じ軸が出るので、結果は決定的になる。
[[nodiscard]] Vector3 LeastAlignedAxis(const Vector3& direction)
{
    const double ax = std::abs(direction.x);
    const double ay = std::abs(direction.y);
    const double az = std::abs(direction.z);
    if (ax <= ay && ax <= az) {
        return Vector3{1.0, 0.0, 0.0};
    }
    if (ay <= az) {
        return Vector3{0.0, 1.0, 0.0};
    }
    return Vector3{0.0, 0.0, 1.0};
}

//! 曲線の指定位置での接線。長さ0なら零ベクトル。
[[nodiscard]] Vector3 TangentAt(const CurveSegment& segment, double parameter)
{
    const Vector3 derivative = segment.FirstDerivative(parameter);
    return Normalized(derivative);
}

//! 直線とみなせる曲線か。曲率法線が定義できるかの判定に使う。
[[nodiscard]] bool LooksStraight(const CurveSegment& segment, double parameter,
    double toleranceMm)
{
    if (segment.Kind() == geometry::CurveKind::Line) {
        return true;
    }
    const Vector3 tangent = TangentAt(segment, parameter);
    const Vector3 second = segment.SecondDerivative(parameter);
    // 接線方向の成分を除いた残りが曲率法線。これが消えていれば直線と同じ。
    const Vector3 curvature = second - tangent * Dot(second, tangent);
    const double scale = std::max(segment.FirstDerivative(parameter).Length(), 1.0);
    return curvature.Length() <= std::max(toleranceMm, 1.0e-9) * scale;
}

} // namespace

WorkPlaneFrame StandardPlane(StandardPlaneKind kind)
{
    WorkPlaneFrame frame;
    switch (kind) {
    case StandardPlaneKind::XY:
        frame.uAxis = {1.0, 0.0, 0.0};
        frame.vAxis = {0.0, 1.0, 0.0};
        frame.normal = {0.0, 0.0, 1.0};
        break;
    case StandardPlaneKind::YZ:
        frame.uAxis = {0.0, 1.0, 0.0};
        frame.vAxis = {0.0, 0.0, 1.0};
        frame.normal = {1.0, 0.0, 0.0};
        break;
    case StandardPlaneKind::ZX:
        frame.uAxis = {0.0, 0.0, 1.0};
        frame.vAxis = {1.0, 0.0, 0.0};
        frame.normal = {0.0, 1.0, 0.0};
        break;
    }
    return frame;
}

bool IsOrthonormalRightHanded(const WorkPlaneFrame& frame, double tolerance)
{
    if (!FrameIsFinite(frame)) {
        return false;
    }
    const double lengthU = frame.uAxis.Length();
    const double lengthV = frame.vAxis.Length();
    const double lengthN = frame.normal.Length();
    if (std::abs(lengthU - 1.0) > tolerance || std::abs(lengthV - 1.0) > tolerance
        || std::abs(lengthN - 1.0) > tolerance) {
        return false;
    }
    if (std::abs(Dot(frame.uAxis, frame.vAxis)) > tolerance
        || std::abs(Dot(frame.vAxis, frame.normal)) > tolerance
        || std::abs(Dot(frame.normal, frame.uAxis)) > tolerance) {
        return false;
    }
    // 右手系: u × v = n
    const Vector3 expected = Cross(frame.uAxis, frame.vAxis);
    return (expected - frame.normal).Length() <= tolerance * 4.0;
}

Result<WorkPlaneFrame> FrameFromNormalAndU(const Vector3& origin, const Vector3& normal,
    const Vector3& uHint, const GeometryTolerance& tolerance)
{
    if (!origin.IsFinite() || !normal.IsFinite() || !uHint.IsFinite()) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kBadInput,
            "座標に有限でない数が入っています。", {}));
    }
    const Vector3 unitNormal = Normalized(normal, tolerance.numericEpsilon);
    if (unitNormal.LengthSquared() <= 0.0) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "平面の向きが決まりません。", "法線の長さが0です。"));
    }
    // u の種から、法線方向の成分を抜く。
    const Vector3 projected = uHint - unitNormal * Dot(uHint, unitNormal);
    const Vector3 unitU = Normalized(projected, tolerance.numericEpsilon);
    if (unitU.LengthSquared() <= 0.0) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "平面の向きが決まりません。",
            "u軸に指定した向きが法線と平行です。別の向きを選んでください。"));
    }
    WorkPlaneFrame frame;
    frame.origin = origin;
    frame.normal = unitNormal;
    frame.uAxis = unitU;
    frame.vAxis = Normalized(Cross(unitNormal, unitU), tolerance.numericEpsilon);
    // 数値誤差を落として、直交をきっちり合わせる。
    frame.uAxis = Normalized(Cross(frame.vAxis, frame.normal), tolerance.numericEpsilon);
    if (!IsOrthonormalRightHanded(frame, 1.0e-9)) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "平面の基底を作れませんでした。", {}));
    }
    return Result<WorkPlaneFrame>::Success(frame);
}

Result<WorkPlaneFrame> FrameFromNormal(const Vector3& origin, const Vector3& normal,
    const GeometryTolerance& tolerance)
{
    const Vector3 unitNormal = Normalized(normal, tolerance.numericEpsilon);
    if (unitNormal.LengthSquared() <= 0.0) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "平面の向きが決まりません。", "法線の長さが0です。"));
    }
    return FrameFromNormalAndU(origin, unitNormal, LeastAlignedAxis(unitNormal),
        tolerance);
}

namespace {

// ---------------------------------------------------------------- 材料の確認

[[nodiscard]] Result<WorkPlaneFrame> RequirePoints(const WorkPlaneRequest& request,
    std::size_t count)
{
    if (request.points.size() < count) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kBadInput,
            "点が足りません。",
            std::to_string(count) + " 点必要ですが "
                + std::to_string(request.points.size()) + " 点しかありません。"));
    }
    if (!AllFinite(request.points)) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kBadInput,
            "座標に有限でない数が入っています。", {}));
    }
    return Result<WorkPlaneFrame>::Success(WorkPlaneFrame{});
}

[[nodiscard]] Result<WorkPlaneFrame> RequireEdges(const WorkPlaneRequest& request,
    std::size_t count)
{
    if (request.edges.size() < count) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kBadInput,
            "辺が足りません。",
            std::to_string(count) + " 本必要ですが "
                + std::to_string(request.edges.size()) + " 本しかありません。"));
    }
    return Result<WorkPlaneFrame>::Success(WorkPlaneFrame{});
}

[[nodiscard]] Result<WorkPlaneFrame> RequireReferencePlane(const WorkPlaneFrame& plane,
    const char* label)
{
    if (!IsOrthonormalRightHanded(plane, 1.0e-6)) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kBadInput,
            "元にする平面が正しくありません。",
            std::string(label) + " の基底が右手系の正規直交になっていません。"));
    }
    return Result<WorkPlaneFrame>::Success(WorkPlaneFrame{});
}

//! 辺の向きと長さ。長さ0は断る材料になる。
struct EdgeLine {
    Vector3 start{};
    Vector3 end{};
    Vector3 direction{};
    double lengthMm = 0.0;
};

[[nodiscard]] EdgeLine LineOf(const CurveSegment& segment)
{
    EdgeLine line;
    line.start = segment.StartPoint();
    line.end = segment.EndPoint();
    const Vector3 span = line.end - line.start;
    line.lengthMm = span.Length();
    line.direction = Normalized(span);
    if (line.direction.LengthSquared() <= 0.0) {
        // 円のように始点と終点が同じでも、接線があれば向きは取れる。
        line.direction = Normalized(segment.FirstDerivative(0.0));
    }
    return line;
}

// ---------------------------------------------------------------- 作り方ごと

[[nodiscard]] Result<WorkPlaneFrame> BuildOffset(const WorkPlaneRequest& request,
    const GeometryTolerance& tolerance)
{
    auto check = RequireReferencePlane(request.referencePlane, "元の平面");
    if (!check.HasValue()) {
        return check;
    }
    if (!geometry::IsFinite(request.offsetMm)) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kBadInput,
            "距離が数になっていません。", {}));
    }
    (void)tolerance;
    WorkPlaneFrame frame = request.referencePlane;
    frame.origin = request.referencePlane.origin
        + request.referencePlane.normal * request.offsetMm;
    return Result<WorkPlaneFrame>::Success(frame);
}

[[nodiscard]] Result<WorkPlaneFrame> BuildThroughPointParallel(
    const WorkPlaneRequest& request, const GeometryTolerance& tolerance)
{
    auto check = RequireReferencePlane(request.referencePlane, "元の平面");
    if (!check.HasValue()) {
        return check;
    }
    auto points = RequirePoints(request, 1);
    if (!points.HasValue()) {
        return points;
    }
    (void)tolerance;
    WorkPlaneFrame frame = request.referencePlane;
    frame.origin = request.points.front();
    return Result<WorkPlaneFrame>::Success(frame);
}

[[nodiscard]] Result<WorkPlaneFrame> BuildMidBetweenPlanes(const WorkPlaneRequest& request,
    const GeometryTolerance& tolerance)
{
    auto first = RequireReferencePlane(request.referencePlane, "1つめの平面");
    if (!first.HasValue()) {
        return first;
    }
    auto second = RequireReferencePlane(request.secondPlane, "2つめの平面");
    if (!second.HasValue()) {
        return second;
    }
    const Vector3 n1 = request.referencePlane.normal;
    const Vector3 n2 = request.secondPlane.normal;
    const double alignment = Dot(n1, n2);
    const double cross = Cross(n1, n2).Length();
    // 平行でない2面の中間は定義できない。近い平面を作って誤魔化さない。
    if (cross > std::max(tolerance.modelAngularRad, 1.0e-9) * 1.0e3) {
        const double angleRad = std::atan2(cross, alignment);
        return Result<WorkPlaneFrame>::Failure(MakeError(kNotParallel,
            "2つの面が平行ではないので、中間の面は決まりません。",
            "2面のなす角 "
                + std::to_string(angleRad * 180.0 / geometry::kPi) + " 度。"));
    }
    // 向きが逆でも同じ面として扱う。
    const Vector3 normal = alignment >= 0.0 ? n1 : -n1;
    const double distance1 = Dot(request.referencePlane.origin, normal);
    const double distance2 = Dot(request.secondPlane.origin, normal);
    const double middle = 0.5 * (distance1 + distance2);
    WorkPlaneFrame frame = request.referencePlane;
    frame.normal = normal;
    if (alignment < 0.0) {
        // 法線を反転したぶん、右手系を保つために v も反転する。
        frame.vAxis = -frame.vAxis;
    }
    frame.origin = request.referencePlane.origin
        + normal * (middle - Dot(request.referencePlane.origin, normal));
    if (!IsOrthonormalRightHanded(frame, 1.0e-6)) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "中間の面の基底を作れませんでした。", {}));
    }
    return Result<WorkPlaneFrame>::Success(frame);
}

[[nodiscard]] Result<WorkPlaneFrame> BuildCylinderAxis(const WorkPlaneRequest& request,
    const GeometryTolerance& tolerance)
{
    const auto kind = request.surface.kind;
    if (kind != fabrication::AnalyticSurfaceKind::Cylinder
        && kind != fabrication::AnalyticSurfaceKind::Cone) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kNotCylinder,
            "選んだ面に中心軸がありません。",
            "円筒面か円錐面を選んでください。"));
    }
    const Vector3 axis = Normalized(request.surface.axis, tolerance.numericEpsilon);
    if (axis.LengthSquared() <= 0.0) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "中心軸の向きが決まりません。", {}));
    }
    // 軸を u に取り、基準方向を v に取る。法線は u × v。
    return FrameFromNormalAndU(request.surface.origin,
        Cross(axis, Normalized(request.surface.reference, tolerance.numericEpsilon)),
        axis, tolerance);
}

[[nodiscard]] Result<WorkPlaneFrame> BuildAngleAboutEdge(const WorkPlaneRequest& request,
    const GeometryTolerance& tolerance)
{
    auto check = RequireReferencePlane(request.referencePlane, "元の平面");
    if (!check.HasValue()) {
        return check;
    }
    auto edges = RequireEdges(request, 1);
    if (!edges.HasValue()) {
        return edges;
    }
    if (!geometry::IsFinite(request.angleRad)) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kBadInput,
            "角度が数になっていません。", {}));
    }
    const EdgeLine line = LineOf(request.edges.front());
    if (line.lengthMm <= std::max(tolerance.modelLinearMm, 1.0e-9)
        || line.direction.LengthSquared() <= 0.0) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "辺の長さが0なので、回す軸が決まりません。",
            "長さ " + std::to_string(line.lengthMm) + " mm。"));
    }
    const Vector3 axis = line.direction;
    // ロドリゲスの回転。軸まわりに法線を回す。
    const double cosine = std::cos(request.angleRad);
    const double sine = std::sin(request.angleRad);
    const auto rotate = [&](const Vector3& value) {
        return value * cosine + Cross(axis, value) * sine
            + axis * (Dot(axis, value) * (1.0 - cosine));
    };
    const Vector3 normal = Normalized(rotate(request.referencePlane.normal),
        tolerance.numericEpsilon);
    if (normal.LengthSquared() <= 0.0) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "回した後の法線が決まりません。", {}));
    }
    return FrameFromNormalAndU(line.start, normal, axis, tolerance);
}

[[nodiscard]] Result<WorkPlaneFrame> BuildThreePoints(const WorkPlaneRequest& request,
    const GeometryTolerance& tolerance)
{
    auto points = RequirePoints(request, 3);
    if (!points.HasValue()) {
        return points;
    }
    const Vector3 a = request.points[0];
    const Vector3 b = request.points[1];
    const Vector3 c = request.points[2];
    const Vector3 ab = b - a;
    const Vector3 ac = c - a;
    const double lengthAB = ab.Length();
    const double lengthAC = ac.Length();
    const double limit = std::max(tolerance.modelLinearMm, 1.0e-9);
    if (lengthAB <= limit || lengthAC <= limit) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "同じ位置の点があるので、平面が決まりません。",
            "3点は互いに離れている必要があります。"));
    }
    const Vector3 cross = Cross(ab, ac);
    // 三角形の高さで見る。長さの積で割ると、大きい図形でも小さい図形でも同じ判断になる。
    const double heightMm = cross.Length() / lengthAB;
    if (heightMm <= limit * 10.0) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "3点が一直線に並んでいるので、平面が決まりません。",
            "直線からの外れ " + std::to_string(heightMm) + " mm。"));
    }
    return FrameFromNormalAndU(a, cross, ab, tolerance);
}

[[nodiscard]] Result<WorkPlaneFrame> BuildTwoEdges(const WorkPlaneRequest& request,
    const GeometryTolerance& tolerance)
{
    auto edges = RequireEdges(request, 2);
    if (!edges.HasValue()) {
        return edges;
    }
    const EdgeLine first = LineOf(request.edges[0]);
    const EdgeLine second = LineOf(request.edges[1]);
    const double limit = std::max(tolerance.modelLinearMm, 1.0e-9);
    if (first.lengthMm <= limit || second.lengthMm <= limit) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "長さが0の辺があります。", {}));
    }
    const Vector3 cross = Cross(first.direction, second.direction);
    if (cross.Length() <= 1.0e-9) {
        // 平行な2辺。2本を結ぶ向きで平面を作る。
        const Vector3 between = second.start - first.start;
        const Vector3 offset = between - first.direction * Dot(between, first.direction);
        if (offset.Length() <= limit) {
            return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
                "2辺が同じ直線の上にあるので、平面が決まりません。", {}));
        }
        return FrameFromNormalAndU(first.start, Cross(first.direction, offset),
            first.direction, tolerance);
    }
    // ねじれの位置にある2辺からは、1つの平面は決まらない。
    const Vector3 between = second.start - first.start;
    const double skew = std::abs(Dot(between, Normalized(cross)));
    if (skew > std::max(limit * 10.0, 1.0e-7)) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kNotCoplanar,
            "2つの辺が同じ平面の上にありません。",
            "ねじれ " + std::to_string(skew) + " mm。"));
    }
    return FrameFromNormalAndU(first.start, cross, first.direction, tolerance);
}

//! 曲面上の点における外向き法線。分からない面では零ベクトルを返す。
[[nodiscard]] Vector3 SurfaceNormalAt(const AnalyticSurfaceInfo& surface,
    const Vector3& point, double epsilon)
{
    const Vector3 axis = Normalized(surface.axis, epsilon);
    switch (surface.kind) {
    case fabrication::AnalyticSurfaceKind::Plane:
        return axis;
    case fabrication::AnalyticSurfaceKind::Cylinder: {
        const Vector3 offset = point - surface.origin;
        const Vector3 radial = offset - axis * Dot(offset, axis);
        return Normalized(radial, epsilon);
    }
    case fabrication::AnalyticSurfaceKind::Sphere:
        return Normalized(point - surface.origin, epsilon);
    case fabrication::AnalyticSurfaceKind::Cone: {
        const Vector3 offset = point - surface.origin;
        const Vector3 radial = Normalized(offset - axis * Dot(offset, axis), epsilon);
        if (radial.LengthSquared() <= 0.0) {
            return {0.0, 0.0, 0.0};
        }
        // 母線方向は (radial*cos - axis*sin) ではなく、法線は radial*cos - axis*sin。
        const double half = surface.halfAngleRad;
        return Normalized(radial * std::cos(half) - axis * std::sin(half), epsilon);
    }
    case fabrication::AnalyticSurfaceKind::Torus: {
        const Vector3 offset = point - surface.origin;
        const Vector3 radial = Normalized(offset - axis * Dot(offset, axis), epsilon);
        if (radial.LengthSquared() <= 0.0) {
            return {0.0, 0.0, 0.0};
        }
        const Vector3 tubeCenter = surface.origin + radial * surface.radiusMm;
        return Normalized(point - tubeCenter, epsilon);
    }
    case fabrication::AnalyticSurfaceKind::Unknown:
    default:
        return {0.0, 0.0, 0.0};
    }
}

[[nodiscard]] Result<WorkPlaneFrame> BuildTangentThroughEdge(
    const WorkPlaneRequest& request, const GeometryTolerance& tolerance)
{
    auto edges = RequireEdges(request, 1);
    if (!edges.HasValue()) {
        return edges;
    }
    if (request.surface.kind == fabrication::AnalyticSurfaceKind::Unknown) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kNotCylinder,
            "接する相手の面が分かりません。",
            "円筒・円錐・球・トーラス・平面のいずれかを選んでください。"));
    }
    const EdgeLine line = LineOf(request.edges.front());
    if (line.lengthMm <= std::max(tolerance.modelLinearMm, 1.0e-9)) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "辺の長さが0です。", {}));
    }
    const Vector3 middle = request.edges.front().Evaluate(0.5);
    const Vector3 normal =
        SurfaceNormalAt(request.surface, middle, tolerance.numericEpsilon);
    if (normal.LengthSquared() <= 0.0) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "その位置では面の法線が決まりません。",
            "中心軸の上では接する平面は一通りに決まりません。"));
    }
    if (std::abs(Dot(normal, line.direction)) > 1.0e-6) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kNotCoplanar,
            "その辺は面に接していません。",
            "辺が面の母線になっていることを確かめてください。"));
    }
    return FrameFromNormalAndU(line.start, normal, line.direction, tolerance);
}

[[nodiscard]] Result<WorkPlaneFrame> BuildTangentThroughPoint(
    const WorkPlaneRequest& request, const GeometryTolerance& tolerance)
{
    auto points = RequirePoints(request, 1);
    if (!points.HasValue()) {
        return points;
    }
    if (request.surface.kind == fabrication::AnalyticSurfaceKind::Unknown) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kNotCylinder,
            "接する相手の面が分かりません。", {}));
    }
    const Vector3 point = request.points.front();
    const Vector3 normal =
        SurfaceNormalAt(request.surface, point, tolerance.numericEpsilon);
    if (normal.LengthSquared() <= 0.0) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "その点では面の法線が決まりません。",
            "中心や中心軸の上では、接する平面は一通りに決まりません。"));
    }
    // u 軸は決定的に決める。軸を平面へ落とせるならそれを使う。
    const Vector3 axis = Normalized(request.surface.axis, tolerance.numericEpsilon);
    const Vector3 projected = axis - normal * Dot(axis, normal);
    const Vector3 hint = projected.Length() > 1.0e-9 ? projected
                                                     : LeastAlignedAxis(normal);
    return FrameFromNormalAndU(point, normal, hint, tolerance);
}

[[nodiscard]] Result<WorkPlaneFrame> BuildNormalToCurve(const WorkPlaneRequest& request,
    const GeometryTolerance& tolerance)
{
    auto edges = RequireEdges(request, 1);
    if (!edges.HasValue()) {
        return edges;
    }
    const double parameter = request.curveParameter;
    if (!geometry::IsFinite(parameter) || parameter < 0.0 || parameter > 1.0) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kBadInput,
            "曲線上の位置が 0〜1 の外にあります。",
            "指定 " + std::to_string(parameter) + "。"));
    }
    const CurveSegment& segment = request.edges.front();
    const Vector3 tangent = TangentAt(segment, parameter);
    if (tangent.LengthSquared() <= 0.0) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kDegenerate,
            "その位置では曲線の向きが決まりません。", {}));
    }
    const Vector3 origin = segment.Evaluate(parameter);
    if (!request.useCurvatureNormal) {
        return FrameFromNormal(origin, tangent, tolerance);
    }
    if (LooksStraight(segment, parameter, tolerance.modelLinearMm)) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kNoCurvature,
            "直線には曲率法線がないので、u軸の向きが決まりません。",
            "曲率法線を使わない設定にするか、曲線を選んでください。"));
    }
    const Vector3 second = segment.SecondDerivative(parameter);
    const Vector3 curvature =
        Normalized(second - tangent * Dot(second, tangent), tolerance.numericEpsilon);
    if (curvature.LengthSquared() <= 0.0) {
        return Result<WorkPlaneFrame>::Failure(MakeError(kNoCurvature,
            "その位置では曲率法線が決まりません。", {}));
    }
    return FrameFromNormalAndU(origin, tangent, curvature, tolerance);
}

} // namespace

Result<WorkPlaneFrame> BuildWorkPlane(const WorkPlaneRequest& request,
    const GeometryTolerance& tolerance)
{
    switch (request.method) {
    case WorkPlaneMethod::Standard:
        return Result<WorkPlaneFrame>::Success(StandardPlane(request.standard));
    case WorkPlaneMethod::OffsetFromPlane:
        return BuildOffset(request, tolerance);
    case WorkPlaneMethod::ThroughPointParallel:
        return BuildThroughPointParallel(request, tolerance);
    case WorkPlaneMethod::MidBetweenPlanes:
        return BuildMidBetweenPlanes(request, tolerance);
    case WorkPlaneMethod::CylinderAxis:
        return BuildCylinderAxis(request, tolerance);
    case WorkPlaneMethod::AngleAboutEdge:
        return BuildAngleAboutEdge(request, tolerance);
    case WorkPlaneMethod::ThreePoints:
        return BuildThreePoints(request, tolerance);
    case WorkPlaneMethod::TwoEdges:
        return BuildTwoEdges(request, tolerance);
    case WorkPlaneMethod::TangentThroughEdge:
        return BuildTangentThroughEdge(request, tolerance);
    case WorkPlaneMethod::TangentThroughPoint:
        return BuildTangentThroughPoint(request, tolerance);
    case WorkPlaneMethod::NormalToCurveAtPoint:
        return BuildNormalToCurve(request, tolerance);
    case WorkPlaneMethod::PointNormal:
        // 数値で直接。u 軸が法線と平行なら FrameFromNormalAndU が断る。
        return FrameFromNormalAndU(request.origin, request.normal, request.uHint,
            tolerance);
    }
    return Result<WorkPlaneFrame>::Failure(MakeError(kUnknownMethod,
        "知らない作り方です。", {}));
}

} // namespace kachakacha::v2::modeling

namespace kachakacha::v2::modeling {

Result<Vector3> FollowPlane(const Vector3& point, PlanePolicy policy,
    const WorkPlaneFrame& before, const WorkPlaneFrame& after)
{
    if (!point.IsFinite()) {
        return Result<Vector3>::Failure(base::MakeError("GEO-P001",
            "作業平面の値に数値でないものが入っています。", "追従させる点です。"));
    }
    if (policy != PlanePolicy::LockedToPlane) {
        // 縛られていない点は動かない。ここで動かすと、平面を触っただけで
        // 関係のない線が動いてしまう。
        return Result<Vector3>::Success(point);
    }
    const auto valid = [](const WorkPlaneFrame& frame) {
        return frame.origin.IsFinite() && frame.uAxis.IsFinite() && frame.vAxis.IsFinite()
            && frame.normal.IsFinite() && frame.uAxis.LengthSquared() > 0.0
            && frame.vAxis.LengthSquared() > 0.0 && frame.normal.LengthSquared() > 0.0;
    };
    if (!valid(before) || !valid(after)) {
        return Result<Vector3>::Failure(base::MakeError("GEO-P001",
            "作業平面の値に数値でないものが入っています。", "動かす前後の平面です。"));
    }
    // 元の平面での位置(u, v, 面からの距離)を、新しい平面へそのまま置き直す。
    const double u = before.CoordinateU(point);
    const double v = before.CoordinateV(point);
    const double offset = before.SignedDistance(point);
    return Result<Vector3>::Success(after.PointAt(u, v) + after.normal * offset);
}

Result<geometry::CurveSegment> FollowPlaneCurve(const geometry::CurveSegment& segment,
    PlanePolicy policy, const WorkPlaneFrame& before, const WorkPlaneFrame& after)
{
    using geometry::CurveKind;
    using geometry::CurveSegment;
    if (policy != PlanePolicy::LockedToPlane) {
        return Result<CurveSegment>::Success(segment);
    }
    const auto move = [&](const Vector3& point) { return FollowPlane(point, policy, before, after); };
    const auto moveDirection = [&](const Vector3& direction) -> Result<Vector3> {
        // 向きは原点の分を差し引く。位置と同じ式で動かすと、原点の移動が二重にかかる。
        const auto head = FollowPlane(direction, policy, before, after);
        const auto tail = FollowPlane(Vector3{}, policy, before, after);
        if (!head.HasValue()) {
            return head;
        }
        if (!tail.HasValue()) {
            return tail;
        }
        return Result<Vector3>::Success(head.Value() - tail.Value());
    };
    switch (segment.Kind()) {
    case CurveKind::Line: {
        const auto start = move(segment.StartPoint());
        const auto end = move(segment.EndPoint());
        if (!start.HasValue()) {
            return Result<CurveSegment>::Failure(start.Diagnostics());
        }
        if (!end.HasValue()) {
            return Result<CurveSegment>::Failure(end.Diagnostics());
        }
        return CurveSegment::MakeLine(start.Value(), end.Value());
    }
    case CurveKind::CircularArc:
    case CurveKind::Circle: {
        const auto center = move(segment.Center());
        const auto normal = moveDirection(segment.Normal());
        const auto reference = moveDirection(segment.ReferenceDirection());
        if (!center.HasValue() || !normal.HasValue() || !reference.HasValue()) {
            return Result<CurveSegment>::Failure(base::MakeError("GEO-P001",
                "作業平面の値に数値でないものが入っています。", "円弧の基準です。"));
        }
        if (segment.Kind() == CurveKind::Circle) {
            return CurveSegment::MakeCircle(center.Value(), normal.Value(),
                reference.Value(), segment.Radius());
        }
        return CurveSegment::MakeCircularArc(center.Value(), normal.Value(),
            reference.Value(), segment.Radius(), segment.StartAngleRad(),
            segment.SweepAngleRad());
    }
    case CurveKind::CubicBezier:
    case CurveKind::CubicBSpline: {
        std::vector<Vector3> points;
        points.reserve(segment.ControlPoints().size());
        for (const Vector3& point : segment.ControlPoints()) {
            const auto moved = move(point);
            if (!moved.HasValue()) {
                return Result<CurveSegment>::Failure(moved.Diagnostics());
            }
            points.push_back(moved.Value());
        }
        return segment.Kind() == CurveKind::CubicBezier
            ? CurveSegment::MakeCubicBezier(points)
            : CurveSegment::MakeCubicBSpline(points);
    }
    }
    return Result<CurveSegment>::Failure(base::MakeError("GEO-P001",
        "作業平面の値に数値でないものが入っています。", "知らない曲線の種類です。"));
}

} // namespace kachakacha::v2::modeling
