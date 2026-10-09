#include "kachakacha/document/Dimension.h"
#include "kachakacha/geometry/Measurement.h"
#include <algorithm>
#include <cmath>
namespace kachakacha::v2::document {
using namespace geometry;
namespace {
std::optional<CurveSegment> CurveOf(const DocumentSnapshot& snapshot, const domain::SegmentRef& ref)
{
    const auto entity = std::find_if(snapshot.entities.begin(), snapshot.entities.end(),
        [&](const auto& item){ return item.id == ref.entityId; });
    if (entity == snapshot.entities.end()) return {};
    for (const auto& feature : snapshot.features) if (feature.id == entity->createdBy) {
        if (const auto* wire = std::get_if<domain::CreateWireDefinition>(&feature.definition)) {
            for (std::size_t index=0; index<wire->segmentIds.size() && index<wire->segments.size(); ++index)
                if (wire->segmentIds[index] == ref.segmentId) return wire->segments[index];
        }
    }
    return {};
}
}
base::Result<DimensionValue> EvaluateDimension(const DocumentSnapshot& snapshot, const ReferenceDimension& dim)
{
    using Out = base::Result<DimensionValue>;
    std::vector<CurveSegment> curves;
    for (const auto& ref : dim.segments) {
        const auto curve = CurveOf(snapshot, ref);
        if (!curve) return Out::Failure(base::MakeError("DIM-001", "寸法の元の作図線が見つかりません。",
            "作図線の寸法を指定してください。加工結果・派生辺には測定を使用してください。"));
        curves.push_back(*curve);
    }
    if (curves.empty() || curves.size()>2) return Out::Failure(base::MakeError("DIM-002", "寸法の対象を1本か2本指定してください。", {}));
    const auto& first=curves[0];
    DimensionValue result;
    if (dim.kind=="dim_radius" || dim.kind=="dim_diameter") {
        const auto radius=MeasureCurveRadius(first);
        if (curves.size()!=1 || !radius) return Out::Failure(base::MakeError("DIM-003", "半径・直径は円か円弧を1本選んでください。", {}));
        result.value=*radius*(dim.kind=="dim_diameter" ? 2 : 1);
        const auto point=first.Evaluate(0.5);
        result.anchors={dim.kind=="dim_diameter" ? first.Center()*2-point : first.Center(),point};
    } else if (dim.kind=="dim_angle") {
        if (curves.size()!=2 || first.Kind()!=CurveKind::Line || curves[1].Kind()!=CurveKind::Line)
            return Out::Failure(base::MakeError("DIM-004", "角度は直線を2本選んでください。", {}));
        const auto a=Normalized(first.EndPoint()-first.StartPoint());
        const auto b=Normalized(curves[1].EndPoint()-curves[1].StartPoint());
        result.value=std::acos(std::clamp(Dot(a,b),-1.0,1.0));
        const auto closest=MeasureCurveToCurve(first,curves[1]);
        result.anchors={closest.firstPoint+a*10,closest.firstPoint,closest.firstPoint+b*10};
    } else {
        if (dim.kind!="dim_length" && dim.kind!="dim_horizontal" && dim.kind!="dim_vertical")
            return Out::Failure(base::MakeError("DIM-005", "この寸法の種類は扱えません。",dim.kind));
        Vector3 a=first.StartPoint(), b=first.EndPoint();
        if (curves.size()==2) {
            a=first.Evaluate(dim.segments[0].startParameter);
            b=curves[1].Evaluate(dim.segments[1].startParameter);
        } else if (first.Kind()!=CurveKind::Line)
            return Out::Failure(base::MakeError("DIM-006", "長さ寸法は直線1本、または2本の線の端点を選んでください。", {}));
        result.anchors={a,b};
        result.value=dim.kind=="dim_horizontal" ? std::abs(Dot(b-a,dim.dimensionU))
            : dim.kind=="dim_vertical" ? std::abs(Dot(b-a,dim.dimensionV)) : Distance(a,b);
    }
    return Out::Success(result);
}
std::vector<base::Diagnostic> ValidateDrivingDimensions(const DocumentSnapshot& snapshot)
{
    std::vector<base::Diagnostic> errors;
    for (const auto& dim : snapshot.referenceDimensions) if (dim.driving) {
        const auto value=EvaluateDimension(snapshot,dim);
        if (!value.HasValue()) { errors.insert(errors.end(),value.Diagnostics().begin(),value.Diagnostics().end());continue; }
        const double tolerance=dim.unit=="rad" ? 1e-6 : 1e-5;
        if (!std::isfinite(dim.recordedValue) || std::abs(value.Value().value-dim.recordedValue)>tolerance)
            errors.push_back(base::MakeError("DIM-007","寸法拘束と矛盾するため変更できません。",
                dim.label+": 寸法を編集するか、参照寸法へ切り替えてください。"));
    }
    return errors;
}
std::vector<base::Diagnostic> SetDimensionCommand::Apply(DocumentSnapshot& candidate) const
{
    if (dimension_.id.IsNil() || !std::isfinite(dimension_.recordedValue)
        || (dimension_.driving && dimension_.recordedValue<=0))
        return {base::MakeError("DIM-008","寸法値は有限の正の数にしてください。",{})};
    const auto measured=EvaluateDimension(candidate,dimension_);
    if(!measured.HasValue())return measured.Diagnostics();
    if(dimension_.unit!=(dimension_.kind=="dim_angle" ? "rad" : "mm")
        || (dimension_.kind=="dim_angle" && dimension_.recordedValue>3.141592653589793)
        || (dimension_.labelPosition && !dimension_.labelPosition->IsFinite()))
        return {base::MakeError("DIM-011","寸法の単位・角度・配置位置が不正です。",{})};
    if(std::abs(dimension_.dimensionU.Length()-1)>1e-6 || std::abs(dimension_.dimensionV.Length()-1)>1e-6
        || std::abs(Dot(dimension_.dimensionU,dimension_.dimensionV))>1e-6)
        return {base::MakeError("DIM-012","寸法の基準軸が直交していません。",{})};
    auto found=std::find_if(candidate.referenceDimensions.begin(),candidate.referenceDimensions.end(),
        [&](const auto& dim){ return dim.id==dimension_.id; });
    if(found==candidate.referenceDimensions.end())candidate.referenceDimensions.push_back(dimension_);
    else *found=dimension_;
    if(dimension_.driving) {
        const auto solved=SolveDimensions(candidate,dimension_);
        if(!solved.HasValue())return solved.Diagnostics();
    }
    for(auto& dim:candidate.referenceDimensions) if(!dim.segments.empty()) {
        const auto value=EvaluateDimension(candidate,dim);
        if(!value.HasValue())return value.Diagnostics();
        dim.anchors=value.Value().anchors;
        if(!dim.driving)dim.recordedValue=value.Value().value;
    }
    return ValidateDrivingDimensions(candidate);
}
} // namespace kachakacha::v2::document
