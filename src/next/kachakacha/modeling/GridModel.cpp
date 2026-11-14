#include "kachakacha/modeling/GridModel.h"

#include <cmath>
#include <string>

namespace kachakacha::v2::modeling {

using base::MakeError;
using base::Result;
using geometry::Dot;

namespace {

constexpr const char* kBrokenPlane = "UI-G001";
constexpr const char* kBadSpacing = "UI-G002";
constexpr const char* kBadSubdivision = "UI-G003";
constexpr const char* kBadOrigin = "UI-G004";
constexpr const char* kBadAngle = "UI-G005";

} // namespace

int MinorPointsPerEdge(int subdivision) noexcept
{
    if (subdivision < 2) {
        return 0;
    }
    return subdivision - 1;
}

int MinorPointsPerCell(int subdivision) noexcept
{
    if (subdivision < 2) {
        return 0;
    }
    return subdivision * subdivision - 1;
}

Result<GridEvaluation> EvaluateGrid(const GridDefinition& definition,
    const std::optional<WorkPlaneFrame>& plane, double pixelsPerMillimeter)
{
    if (definition.workPlaneId.has_value() && !plane.has_value()) {
        // 指している作業平面が無い。別の平面へ勝手に付け替えない。
        return Result<GridEvaluation>::Failure(MakeError(kBrokenPlane,
            "グリッドが置かれている作業平面がありません。",
            "別の平面へ勝手に付け替えることはしません。"
            "平面を選び直すか、グリッドを世界の面へ戻してください。"));
    }
    if (!(definition.majorSpacingMm > 0.0)
        || !geometry::IsFinite(definition.majorSpacingMm)) {
        return Result<GridEvaluation>::Failure(MakeError(kBadSpacing,
            "グリッドの間隔が正の数ではありません。",
            std::to_string(definition.majorSpacingMm)));
    }
    if (definition.subdivision != 0 && definition.subdivision != 2
        && definition.subdivision != 3 && definition.subdivision != 4) {
        return Result<GridEvaluation>::Failure(MakeError(kBadSubdivision,
            "副点の細かさは 1/2、1/3、1/4 のどれかです。",
            std::to_string(definition.subdivision)));
    }
    if (!geometry::IsFinite(definition.rotation.Value())) {
        return Result<GridEvaluation>::Failure(MakeError(kBadAngle,
            "グリッドの角度には有限の数を指定してください。", {}));
    }

    const WorkPlaneFrame frame =
        plane.has_value() ? *plane : StandardPlane(StandardPlaneKind::XY);

    GridEvaluation evaluation;
    evaluation.visible = definition.visible;
    // 原点は UV で持っているので、平面が動けば自然に付いていく。
    evaluation.originXyz = frame.PointAt(definition.originUmm, definition.originVmm);
    const double angle = std::remainder(definition.rotation.Value(), 2.0 * geometry::kPi);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    evaluation.uDirection = frame.uAxis * c + frame.vAxis * s;
    evaluation.vDirection = frame.vAxis * c - frame.uAxis * s;
    evaluation.majorSpacingMm = definition.majorSpacingMm;
    evaluation.subdivision = definition.subdivision;
    evaluation.minorPointsPerEdge = MinorPointsPerEdge(definition.subdivision);
    evaluation.minorPointsPerCell = MinorPointsPerCell(definition.subdivision);
    evaluation.minorSpacingMm = definition.subdivision >= 2
        ? definition.majorSpacingMm / definition.subdivision
        : 0.0;
    evaluation.majorSpacingPx = definition.majorSpacingMm * pixelsPerMillimeter;
    evaluation.minorVisible = definition.subdivision >= 2
        && evaluation.minorSpacingMm * pixelsPerMillimeter >= kMinimumGridSpacingPx;
    return Result<GridEvaluation>::Success(evaluation);
}

Result<geometry::Radians> GridAngleFromPoints(const WorkPlaneFrame& plane,
    const Vector3& first, const Vector3& second, double minimumLengthMm)
{
    const auto delta = second - first;
    const double u = Dot(delta, plane.uAxis);
    const double v = Dot(delta, plane.vAxis);
    const double distance = std::hypot(u, v);
    if (!std::isfinite(distance) || !(distance > minimumLengthMm) || distance == 0.0) {
        return Result<geometry::Radians>::Failure(MakeError(kBadAngle,
            "作業面に投影した2点が重なっています。", "離れた2点を選んでください。"));
    }
    return Result<geometry::Radians>::Success(geometry::Radians(std::atan2(v, u)));
}

Result<GridDefinition> MoveGridOrigin(const GridDefinition& definition,
    const WorkPlaneFrame& plane, const Vector3& worldTarget)
{
    if (!worldTarget.IsFinite()) {
        return Result<GridDefinition>::Failure(MakeError(kBadOrigin,
            "原点に有限でない数が入っています。", {}));
    }
    if (!IsOrthonormalRightHanded(plane, 1.0e-6)) {
        return Result<GridDefinition>::Failure(MakeError(kBrokenPlane,
            "作業平面が正しくありません。", {}));
    }
    GridDefinition moved = definition;
    // ワールド座標では持たない。平面の UV へ直して保存する。
    moved.originUmm = plane.CoordinateU(worldTarget);
    moved.originVmm = plane.CoordinateV(worldTarget);
    return Result<GridDefinition>::Success(moved);
}

Result<GridDefinition> SetGridOriginUv(const GridDefinition& definition, double u,
    double v)
{
    if (!geometry::IsFinite(u) || !geometry::IsFinite(v)) {
        return Result<GridDefinition>::Failure(MakeError(kBadOrigin,
            "原点に有限でない数が入っています。", {}));
    }
    GridDefinition moved = definition;
    moved.originUmm = u;
    moved.originVmm = v;
    return Result<GridDefinition>::Success(moved);
}

Result<GridDefinition> SetGridOriginXyz(const GridDefinition& definition,
    const WorkPlaneFrame& plane, const Vector3& xyz)
{
    return MoveGridOrigin(definition, plane, xyz);
}

Result<GridDefinition> SetGridSpacing(const GridDefinition& definition,
    double majorSpacingMm, int subdivision)
{
    if (!(majorSpacingMm > 0.0) || !geometry::IsFinite(majorSpacingMm)) {
        return Result<GridDefinition>::Failure(MakeError(kBadSpacing,
            "グリッドの間隔は正の数にしてください。",
            std::to_string(majorSpacingMm)));
    }
    if (subdivision != 0 && subdivision != 2 && subdivision != 3 && subdivision != 4) {
        return Result<GridDefinition>::Failure(MakeError(kBadSubdivision,
            "副点の細かさは 1/2、1/3、1/4 のどれかです。",
            std::to_string(subdivision)));
    }
    GridDefinition next = definition;
    next.majorSpacingMm = majorSpacingMm;
    next.subdivision = subdivision;
    return Result<GridDefinition>::Success(next);
}

GridLineRange GridLineRangeFor(double spacingMm, double minUmm, double maxUmm,
    double minVmm, double maxVmm, long long maxLines)
{
    GridLineRange range;
    if (!(spacingMm > 0.0) || maxUmm < minUmm || maxVmm < minVmm || maxLines <= 0) {
        return range;
    }
    // 端で切れて見えないよう、1本ずつ外へ伸ばす。
    range.firstU = static_cast<long long>(std::floor(minUmm / spacingMm)) - 1;
    range.lastU = static_cast<long long>(std::ceil(maxUmm / spacingMm)) + 1;
    range.firstV = static_cast<long long>(std::floor(minVmm / spacingMm)) - 1;
    range.lastV = static_cast<long long>(std::ceil(maxVmm / spacingMm)) + 1;
    const long long countU = range.lastU - range.firstU + 1;
    const long long countV = range.lastV - range.firstV + 1;
    if (countU > maxLines || countV > maxLines) {
        // 画面に入りきらない本数。引くと固まるだけで、何も読み取れない。
        return GridLineRange{};
    }
    range.any = true;
    return range;
}

double GridLineSpacingPx(const geometry::ScreenMapping& mapping,
    const Vector3& origin, const Vector3& along, const Vector3& offset)
{
    const auto a=mapping.Project(origin),b=mapping.Project(origin+along),c=mapping.Project(origin+offset);
    if(!a||!b||!c)return 0;
    const double dx=b->x-a->x,dy=b->y-a->y;
    const double length=std::hypot(dx,dy);
    return length>1e-12?std::abs(dx*(c->y-a->y)-dy*(c->x-a->x))/length:0;
}

} // namespace kachakacha::v2::modeling
