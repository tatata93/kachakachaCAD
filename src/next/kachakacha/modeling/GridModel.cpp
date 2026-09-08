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

    const WorkPlaneFrame frame =
        plane.has_value() ? *plane : StandardPlane(StandardPlaneKind::XY);

    GridEvaluation evaluation;
    evaluation.visible = definition.visible;
    // 原点は UV で持っているので、平面が動けば自然に付いていく。
    evaluation.originXyz = frame.PointAt(definition.originUmm, definition.originVmm);
    evaluation.uDirection = frame.uAxis;
    evaluation.vDirection = frame.vAxis;
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

} // namespace kachakacha::v2::modeling
