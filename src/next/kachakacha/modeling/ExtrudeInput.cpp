#include "kachakacha/modeling/ExtrudeInput.h"

#include <algorithm>
#include <optional>
#include <cmath>
#include <string>

namespace kachakacha::v2::modeling {

using base::MakeError;
using base::MakeWarning;
using base::Result;
using geometry::Cross;
using geometry::Dot;
using geometry::Normalized;
using geometry::Point2;

namespace {

constexpr const char* kNotPlanar = "EXT-001";
constexpr const char* kOpenForSolid = "EXT-002";
constexpr const char* kTargetNotReached = "EXT-003";
constexpr const char* kNoIntersection = "EXT-004";
constexpr const char* kWouldBeDisconnected = "EXT-005";
constexpr const char* kZeroDistance = "EXT-006";
//! 入力そのものが揃っていない場合。上の6つは「押し出しの意味」の診断なので分ける。
constexpr const char* kBadInput = "EXT-010";

[[nodiscard]] double SamplingToleranceMm(const GeometryTolerance& tolerance)
{
    return std::max(tolerance.modelLinearMm * 10.0, 1.0e-5);
}

struct SampledProfile {
    const ExtrudeProfile* profile = nullptr;
    std::vector<Vector3> points;
    std::vector<Point2> planar;
    double signedArea = 0.0;
    bool areaIsExact = true;
};

[[nodiscard]] std::string ProfileLabel(std::size_t index)
{
    return std::to_string(index + 1) + " 番目の輪郭";
}

// ---------------------------------------------------------------- 向き

[[nodiscard]] Result<Vector3> ResolveDirection(const ExtrudeRequest& request,
    const geometry::PlaneFit& plane, const GeometryTolerance& tolerance)
{
    Vector3 raw{0.0, 0.0, 1.0};
    switch (request.directionMode) {
    case ExtrudeDirectionMode::ProfileNormal:
        if (!plane.valid) {
            return Result<Vector3>::Failure(MakeError(kNotPlanar,
                "輪郭が平面に載っていないので、輪郭の法線が決まりません。", {}));
        }
        raw = plane.normal;
        break;
    case ExtrudeDirectionMode::WorkPlaneNormal:
        if (!IsOrthonormalRightHanded(request.workPlane, 1.0e-6)) {
            return Result<Vector3>::Failure(MakeError(kBadInput,
                "作業平面が正しくありません。", {}));
        }
        raw = request.workPlane.normal;
        break;
    case ExtrudeDirectionMode::WorldX: raw = {1.0, 0.0, 0.0}; break;
    case ExtrudeDirectionMode::WorldY: raw = {0.0, 1.0, 0.0}; break;
    case ExtrudeDirectionMode::WorldZ: raw = {0.0, 0.0, 1.0}; break;
    case ExtrudeDirectionMode::SelectedVector:
    case ExtrudeDirectionMode::CustomXYZ:
        raw = request.customDirection;
        break;
    }
    if (!raw.IsFinite()) {
        return Result<Vector3>::Failure(MakeError(kBadInput,
            "向きに有限でない数が入っています。", {}));
    }
    // 正規化前の長さで判定する(§8.2)。
    if (raw.Length() <= tolerance.numericEpsilon) {
        return Result<Vector3>::Failure(MakeError(kBadInput,
            "向きの長さが0です。",
            "長さ " + std::to_string(raw.Length()) + "。0でない向きを指定してください。"));
    }
    const Vector3 unit = Normalized(raw, tolerance.numericEpsilon);
    return Result<Vector3>::Success(request.reversed ? -unit : unit);
}

// ---------------------------------------------------------------- 輪郭の分類

//! 平面上へ落とした輪郭から、外周と穴を決める。
[[nodiscard]] std::vector<ExtrudeLoop> ClassifyLoops(
    const std::vector<SampledProfile>& sampled, double toleranceMm)
{
    const std::size_t count = sampled.size();
    std::vector<std::size_t> depth(count, 0);
    for (std::size_t inner = 0; inner < count; ++inner) {
        for (std::size_t outer = 0; outer < count; ++outer) {
            if (inner == outer || sampled[inner].planar.empty()
                || sampled[outer].planar.empty()) {
                continue;
            }
            if (geometry::ContainsPoint(sampled[outer].planar,
                sampled[inner].planar.front())) {
                ++depth[inner];
            }
        }
    }
    std::vector<ExtrudeLoop> loops;
    loops.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        ExtrudeLoop loop;
        loop.profileIndex = index;
        loop.isHole = (depth[index] % 2) == 1;
        loop.areaMm2 = std::abs(sampled[index].signedArea);
        loop.areaIsExact = sampled[index].areaIsExact;
        loops.push_back(loop);
    }
    // 穴を、いちばん内側の外周へ結びつける。
    for (std::size_t hole = 0; hole < count; ++hole) {
        if (!loops[hole].isHole) {
            continue;
        }
        std::size_t best = count;
        double bestArea = 0.0;
        for (std::size_t outer = 0; outer < count; ++outer) {
            if (loops[outer].isHole || outer == hole || sampled[outer].planar.empty()) {
                continue;
            }
            if (depth[outer] + 1 != depth[hole]) {
                continue;
            }
            if (!geometry::ContainsPoint(sampled[outer].planar,
                sampled[hole].planar.front())) {
                continue;
            }
            if (best == count || loops[outer].areaMm2 < bestArea) {
                best = outer;
                bestArea = loops[outer].areaMm2;
            }
        }
        if (best != count) {
            loops[best].holes.push_back(hole);
        }
    }
    (void)toleranceMm;
    return loops;
}

// ---------------------------------------------------------------- ToTarget

//! 点 p から向き d へ進んだとき、相手へ届くまでの距離。届かないなら値なし。
[[nodiscard]] std::optional<double> RayToPlane(const Vector3& point,
    const Vector3& direction, const WorkPlaneFrame& plane, double epsilon)
{
    const double denominator = Dot(direction, plane.normal);
    if (std::abs(denominator) <= epsilon) {
        return std::nullopt;
    }
    const double distance = Dot(plane.origin - point, plane.normal) / denominator;
    if (!(distance > 0.0)) {
        return std::nullopt;
    }
    return distance;
}

//! 二次方程式の、正で最小の解。
[[nodiscard]] std::optional<double> SmallestPositiveRoot(double a, double b, double c,
    double epsilon)
{
    if (std::abs(a) <= epsilon) {
        if (std::abs(b) <= epsilon) {
            return std::nullopt;
        }
        const double single = -c / b;
        return single > 0.0 ? std::optional<double>(single) : std::nullopt;
    }
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) {
        return std::nullopt;
    }
    const double root = std::sqrt(discriminant);
    const double first = (-b - root) / (2.0 * a);
    const double second = (-b + root) / (2.0 * a);
    const double low = std::min(first, second);
    const double high = std::max(first, second);
    if (low > 0.0) {
        return low;
    }
    if (high > 0.0) {
        return high;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<double> RayToSurface(const Vector3& point,
    const Vector3& direction, const AnalyticSurfaceInfo& surface, double epsilon)
{
    const Vector3 axis = Normalized(surface.axis, epsilon);
    switch (surface.kind) {
    case fabrication::AnalyticSurfaceKind::Plane: {
        WorkPlaneFrame plane;
        plane.origin = surface.origin;
        plane.normal = axis;
        return RayToPlane(point, direction, plane, epsilon);
    }
    case fabrication::AnalyticSurfaceKind::Sphere: {
        const Vector3 offset = point - surface.origin;
        return SmallestPositiveRoot(Dot(direction, direction),
            2.0 * Dot(offset, direction),
            Dot(offset, offset) - surface.radiusMm * surface.radiusMm, epsilon);
    }
    case fabrication::AnalyticSurfaceKind::Cylinder: {
        const Vector3 offset = point - surface.origin;
        const Vector3 flatOffset = offset - axis * Dot(offset, axis);
        const Vector3 flatDirection = direction - axis * Dot(direction, axis);
        return SmallestPositiveRoot(Dot(flatDirection, flatDirection),
            2.0 * Dot(flatOffset, flatDirection),
            Dot(flatOffset, flatOffset) - surface.radiusMm * surface.radiusMm, epsilon);
    }
    case fabrication::AnalyticSurfaceKind::Cone: {
        // 半頂角 α の円錐。軸方向の高さ h に対して半径は r0 + h*tan(α)。
        const double slope = std::tan(surface.halfAngleRad);
        const Vector3 offset = point - surface.origin;
        const double offsetAlong = Dot(offset, axis);
        const double directionAlong = Dot(direction, axis);
        const Vector3 flatOffset = offset - axis * offsetAlong;
        const Vector3 flatDirection = direction - axis * directionAlong;
        const double radiusAt = surface.radiusMm + offsetAlong * slope;
        const double radiusRate = directionAlong * slope;
        const double a = Dot(flatDirection, flatDirection) - radiusRate * radiusRate;
        const double b = 2.0 * (Dot(flatOffset, flatDirection) - radiusAt * radiusRate);
        const double c = Dot(flatOffset, flatOffset) - radiusAt * radiusAt;
        return SmallestPositiveRoot(a, b, c, epsilon);
    }
    case fabrication::AnalyticSurfaceKind::Torus:
    case fabrication::AnalyticSurfaceKind::Unknown:
    default:
        return std::nullopt;
    }
}

} // namespace

PlanarLoopArea SignedAreaOnPlane(const std::vector<CurveSegment>& segments,
    const geometry::PlanarFrame& frame, double samplingToleranceMm)
{
    PlanarLoopArea result;
    if (segments.empty()) {
        return result;
    }
    // 1. 線の端点だけを結んだ多角形の面積(靴ひも公式)。
    std::vector<Vector3> corners;
    corners.reserve(segments.size());
    for (const CurveSegment& segment : segments) {
        corners.push_back(segment.StartPoint());
    }
    const std::vector<Point2> flat = geometry::ProjectToFrame(corners, frame);
    double area = geometry::SignedArea(flat);

    // 2. 弦と実際の曲線のあいだの面積を、線ごとに足す。
    for (const CurveSegment& segment : segments) {
        switch (segment.Kind()) {
        case geometry::CurveKind::Line:
            break;
        case geometry::CurveKind::Circle:
        case geometry::CurveKind::CircularArc: {
            // 平面の法線から見た向きで、掃引の符号を決める。
            const double facing = Dot(segment.Normal(), frame.normal) >= 0.0 ? 1.0 : -1.0;
            const double sweep = segment.Kind() == geometry::CurveKind::Circle
                ? 2.0 * geometry::kPi * facing
                : segment.SweepAngleRad() * facing;
            const double radius = segment.Radius();
            area += 0.5 * radius * radius * (sweep - std::sin(sweep));
            break;
        }
        case geometry::CurveKind::CubicBezier:
        case geometry::CurveKind::CubicBSpline: {
            // 厳密には出せない。細かく標本化した折れ線で近似し、その旨を残す。
            result.exact = false;
            const std::vector<geometry::CurvePoint> points =
                geometry::SampleCurve(segment, samplingToleranceMm);
            std::vector<Vector3> positions;
            positions.reserve(points.size());
            for (const geometry::CurvePoint& point : points) {
                positions.push_back(point.position);
            }
            const std::vector<Point2> local = geometry::ProjectToFrame(positions, frame);
            // 弦(始点→終点)と曲線で囲まれる面積。
            for (std::size_t index = 0; index + 1 < local.size(); ++index) {
                area += 0.5 * (local[index].u * local[index + 1].v
                    - local[index + 1].u * local[index].v);
            }
            area -= 0.5 * (local.front().u * local.back().v
                - local.back().u * local.front().v);
            break;
        }
        }
    }
    result.signedAreaMm2 = area;
    return result;
}

Result<ExtrudeAnalysis> AnalyzeExtrudeRequest(const ExtrudeRequest& request,
    const GeometryTolerance& tolerance)
{
    std::vector<Diagnostic> errors;

    if (request.profiles.empty()) {
        errors.push_back(MakeError(kBadInput, "押し出す輪郭がありません。", {}));
    }
    if (!request.outputs.Any()) {
        errors.push_back(MakeError(kBadInput, "作るものを1つ以上選んでください。",
            "輪郭ワイヤー・側面の境界ワイヤー・部品のどれかを選びます。"));
    }
    if (request.outputs.part && request.booleanMode != ExtrudeBooleanMode::NewPart
        && !request.hasSelectedPart) {
        errors.push_back(MakeError(kBadInput, "足す/引く相手の部品が選ばれていません。",
            "近くにある部品を勝手に選ぶことはしません。"));
    }
    if (request.extent == ExtrudeExtentMode::ToTarget
        && request.targetKind == ExtrudeTargetKind::None) {
        errors.push_back(MakeError(kBadInput, "押し出す先が選ばれていません。", {}));
    }
    if (request.extent == ExtrudeExtentMode::ThroughAll
        && request.booleanMode != ExtrudeBooleanMode::SubtractFromPart) {
        errors.push_back(MakeError(kBadInput, "「全部貫く」は引くときだけ使えます。",
            "geometry-contract §8.3。"));
    }
    if (!errors.empty()) {
        return Result<ExtrudeAnalysis>::Failure(std::move(errors));
    }

    // ---- 輪郭を点列にする ----
    const double samplingTolerance = SamplingToleranceMm(tolerance);
    std::vector<SampledProfile> sampled;
    sampled.reserve(request.profiles.size());
    std::vector<Vector3> everyPoint;
    for (std::size_t index = 0; index < request.profiles.size(); ++index) {
        SampledProfile item;
        item.profile = &request.profiles[index];
        if (request.profiles[index].segments.empty()) {
            errors.push_back(MakeError(kBadInput, "線の入っていない輪郭があります。",
                ProfileLabel(index)));
            sampled.push_back(item);
            continue;
        }
        item.points =
            geometry::SampleChain(request.profiles[index].segments, samplingTolerance);
        for (const Vector3& point : item.points) {
            if (!point.IsFinite()) {
                errors.push_back(MakeError(kBadInput,
                    "座標に有限でない数が入っています。", ProfileLabel(index)));
                break;
            }
        }
        everyPoint.insert(everyPoint.end(), item.points.begin(), item.points.end());
        sampled.push_back(std::move(item));
    }
    if (!errors.empty()) {
        return Result<ExtrudeAnalysis>::Failure(std::move(errors));
    }

    // ---- 部品を作るなら、輪郭は閉じていなければならない(EXT-002) ----
    if (request.outputs.part) {
        for (std::size_t index = 0; index < request.profiles.size(); ++index) {
            if (!request.profiles[index].closed) {
                errors.push_back(MakeError(kOpenForSolid,
                    "開いた輪郭からは部品を作れません。",
                    ProfileLabel(index)
                        + "。輪郭ワイヤーだけなら開いたままでも作れます。"));
            }
        }
    }

    // ---- 同一平面か(EXT-001) ----
    const geometry::PlaneFit plane = geometry::FitPlane(everyPoint);
    ExtrudeAnalysis analysis;
    analysis.profilePlane = plane;
    if (request.profiles.size() > 1 || request.outputs.part) {
        const double limit = std::max(tolerance.modelLinearMm * 10.0, 1.0e-5);
        if (!plane.valid) {
            errors.push_back(MakeError(kNotPlanar,
                "輪郭が平面に載っていません。",
                "点が3つ未満か、すべて一直線に並んでいます。"));
        } else if (plane.maximumDeviationMm > limit) {
            errors.push_back(MakeError(kNotPlanar,
                "輪郭が同じ平面に載っていません。",
                "平面からの最大のずれ " + std::to_string(plane.maximumDeviationMm)
                    + " mm(許容 " + std::to_string(limit) + " mm)。"));
        }
    }
    if (!errors.empty()) {
        return Result<ExtrudeAnalysis>::Failure(std::move(errors));
    }

    // ---- 向き ----
    auto direction = ResolveDirection(request, plane, tolerance);
    if (!direction.HasValue()) {
        return Result<ExtrudeAnalysis>::Failure(direction.Diagnostics());
    }
    analysis.direction = direction.Value();

    // 向きが輪郭平面と平行だと、押し出しても厚みが出ない。
    if (plane.valid) {
        const double alignment = std::abs(Dot(analysis.direction, plane.normal));
        if (request.outputs.part && alignment <= 1.0e-9) {
            return Result<ExtrudeAnalysis>::Failure(MakeError(kBadInput,
                "押し出す向きが輪郭と同じ平面の上にあります。",
                "この向きでは厚みが出ません。"));
        }
    }

    // ---- 平面上での面積と、外周・穴の分類 ----
    if (plane.valid) {
        const geometry::PlanarFrame frame = geometry::MakeFrame(plane);
        for (SampledProfile& item : sampled) {
            std::vector<Vector3> loop = item.points;
            geometry::RemoveClosingDuplicate(loop, samplingTolerance);
            item.planar = geometry::ProjectToFrame(loop, frame);
            // 面積は標本化した折れ線ではなく、曲線そのものから出す。
            // 折れ線の面積を使うと、円が多角形へ化けても体積が合ってしまう。
            const PlanarLoopArea exact =
                SignedAreaOnPlane(item.profile->segments, frame, samplingTolerance);
            item.signedArea = exact.signedAreaMm2;
            item.areaIsExact = exact.exact;
        }
        analysis.loops = ClassifyLoops(sampled, samplingTolerance);
    }

    double outerArea = 0.0;
    double holeArea = 0.0;
    std::size_t outerCount = 0;
    std::size_t segmentCount = 0;
    for (const ExtrudeLoop& loop : analysis.loops) {
        if (!loop.areaIsExact) {
            analysis.areaIsExact = false;
        }
        if (loop.isHole) {
            holeArea += loop.areaMm2;
        } else {
            outerArea += loop.areaMm2;
            ++outerCount;
        }
        segmentCount += request.profiles[loop.profileIndex].segments.size();
    }
    analysis.predictedProfileAreaMm2 = std::max(outerArea - holeArea, 0.0);

    // ---- どこまで押すか ----
    const double zeroLimit = std::max(tolerance.modelLinearMm, 1.0e-9);
    switch (request.extent) {
    case ExtrudeExtentMode::Distance:
        analysis.startOffsetMm = 0.0;
        analysis.endOffsetMm = request.distanceMm;
        break;
    case ExtrudeExtentMode::SymmetricDistance:
        analysis.startOffsetMm = -0.5 * request.distanceMm;
        analysis.endOffsetMm = 0.5 * request.distanceMm;
        break;
    case ExtrudeExtentMode::TwoDistances:
        analysis.startOffsetMm = -request.secondDistanceMm;
        analysis.endOffsetMm = request.distanceMm;
        break;
    case ExtrudeExtentMode::ToTarget:
    case ExtrudeExtentMode::ThroughAll:
        analysis.startOffsetMm = 0.0;
        analysis.endOffsetMm = 0.0;
        break;
    }

    if (request.extent == ExtrudeExtentMode::Distance
        || request.extent == ExtrudeExtentMode::SymmetricDistance
        || request.extent == ExtrudeExtentMode::TwoDistances) {
        if (!geometry::IsFinite(request.distanceMm)
            || !geometry::IsFinite(request.secondDistanceMm)) {
            return Result<ExtrudeAnalysis>::Failure(MakeError(kBadInput,
                "距離が数になっていません。", {}));
        }
        const double span = analysis.endOffsetMm - analysis.startOffsetMm;
        if (std::abs(span) <= zeroLimit) {
            if (request.outputs.part) {
                return Result<ExtrudeAnalysis>::Failure(MakeError(kZeroDistance,
                    "距離が0なので、部品は作れません。",
                    "同じ場所に輪郭ワイヤーだけを作ることはできます。"));
            }
            if (!request.zeroDistanceConfirmed) {
                return Result<ExtrudeAnalysis>::Failure(MakeError(kZeroDistance,
                    "距離が0です。元の輪郭と同じ場所に作ります。",
                    "このまま作るなら確認してください。"));
            }
            analysis.notes.push_back(MakeWarning("EXT-100",
                "距離が0なので、元の輪郭と同じ場所にできます。", {}));
        }
    }

    // ---- ToTarget が届くか(EXT-003) ----
    if (request.extent == ExtrudeExtentMode::ToTarget) {
        double smallest = 0.0;
        double largest = 0.0;
        bool first = true;
        std::size_t missed = 0;
        Vector3 missedPoint{};
        for (const SampledProfile& item : sampled) {
            for (const Vector3& point : item.points) {
                std::optional<double> reach;
                if (request.targetKind == ExtrudeTargetKind::Plane) {
                    reach = RayToPlane(point, analysis.direction, request.targetPlane,
                        tolerance.numericEpsilon);
                } else {
                    reach = RayToSurface(point, analysis.direction, request.targetSurface,
                        tolerance.numericEpsilon);
                }
                if (!reach.has_value()) {
                    ++missed;
                    if (missed == 1) {
                        missedPoint = point;
                    }
                    continue;
                }
                if (first) {
                    smallest = *reach;
                    largest = *reach;
                    first = false;
                } else {
                    smallest = std::min(smallest, *reach);
                    largest = std::max(largest, *reach);
                }
            }
        }
        if (missed > 0) {
            // 部分的な solid を作らない(§8.3)。
            return Result<ExtrudeAnalysis>::Failure(MakeError(kTargetNotReached,
                "押し出す先へ届かない場所があります。",
                std::to_string(missed) + " 箇所が届きません。最初の位置 ("
                    + std::to_string(missedPoint.x) + ", "
                    + std::to_string(missedPoint.y) + ", "
                    + std::to_string(missedPoint.z) + ")。"
                    + "部分的な形は作りません。"));
        }
        if (first) {
            return Result<ExtrudeAnalysis>::Failure(MakeError(kTargetNotReached,
                "押し出す先へ届く点がありません。", {}));
        }
        analysis.minimumReachMm = smallest;
        analysis.maximumReachMm = largest;
        analysis.startOffsetMm = 0.0;
        analysis.endOffsetMm = largest;
        if (largest - smallest > tolerance.modelLinearMm) {
            analysis.notes.push_back(MakeWarning("EXT-101",
                "押し出す先が平らでないため、場所によって長さが変わります。",
                "最短 " + std::to_string(smallest) + " mm / 最長 "
                    + std::to_string(largest) + " mm。"));
        }
    }

    // ---- 予測 ----
    analysis.expectedPartCount = request.outputs.part ? outerCount : 0;
    if (request.outputs.part && analysis.expectedPartCount == 0) {
        return Result<ExtrudeAnalysis>::Failure(MakeError(kBadInput,
            "外周の輪郭がありません。", "穴だけでは部品になりません。"));
    }
    if (plane.valid) {
        const double along = std::abs(Dot(analysis.direction, plane.normal));
        const double thickness =
            std::abs(analysis.endOffsetMm - analysis.startOffsetMm) * along;
        analysis.predictedVolumeMm3 = analysis.predictedProfileAreaMm2 * thickness;
    }
    // 面の数 = 上下の蓋(外周ごとに2枚)+ 側面(輪郭の線1本につき1枚)。
    analysis.predictedFaceCount = outerCount * 2 + segmentCount;

    // ---- 足す/引くの成立性 ----
    if (request.outputs.part && request.booleanMode == ExtrudeBooleanMode::AddToPart
        && analysis.expectedPartCount > 1) {
        // 非接触の外周が複数あると、足した結果が非連結になりうる(EXT-005)。
        analysis.notes.push_back(MakeWarning(kWouldBeDisconnected,
            "外周が複数あるため、足した結果が離ればなれになるかもしれません。",
            "外周 " + std::to_string(analysis.expectedPartCount)
                + " 個。実際の連結は形を作ってから確かめます。"));
    }
    (void)kNoIntersection;

    // notes は解析結果にも残し、警告としても返す。
    // 先に取り出してから move すること(同じ式の中で読むと順序が決まらない)。
    std::vector<Diagnostic> warnings = analysis.notes;
    return Result<ExtrudeAnalysis>::Success(std::move(analysis), std::move(warnings));
}

ExtrudeResultCheck CheckExtrudeResult(const ExtrudeAnalysis& analysis,
    double actualVolumeMm3, std::size_t actualFaceCount, std::size_t actualPartCount)
{
    ExtrudeResultCheck check;
    check.partCountMatches = actualPartCount == analysis.expectedPartCount;
    check.faceCountMatches = actualFaceCount == analysis.predictedFaceCount;
    if (analysis.predictedVolumeMm3 > 0.0) {
        check.volumeErrorRatio =
            std::abs(actualVolumeMm3 - analysis.predictedVolumeMm3)
            / analysis.predictedVolumeMm3;
        // 曲面までの押し出しなどで予測が厳密でない場合に備え、比で見る。
        // 厳密に出せた断面なら、ずれは丸め誤差ぶんしか許さない。
        // ベジェなどで近似した断面は、標本化のぶんだけ緩める。
        check.volumeMatches =
            check.volumeErrorRatio <= (analysis.areaIsExact ? 1.0e-6 : 1.0e-3);
    } else {
        check.volumeMatches = std::abs(actualVolumeMm3) <= 1.0e-9;
    }
    return check;
}

} // namespace kachakacha::v2::modeling
