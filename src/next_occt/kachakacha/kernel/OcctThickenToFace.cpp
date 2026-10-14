#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <algorithm>
#include <cmath>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pln.hxx>
#include <Standard_Failure.hxx>
#endif

namespace kachakacha::v2::kernel {
base::Result<ThickenedSolid> ThickenSurfaceToFace(modeling::KernelShapeHandle source,
    modeling::KernelShapeHandle target, const std::optional<modeling::WorkPlaneFrame>& plane,
    double offset, const geometry::GeometryTolerance& tolerance)
{
    using Result = base::Result<ThickenedSolid>;
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        TopoDS_Shape sourceShape, targetShape;
        if (!LookupShape(source, sourceShape)) return Result::Failure(base::MakeError(kThickenSourceMissing,"元の面がありません。",{}));
        TopExp_Explorer faces(sourceShape,TopAbs_FACE);
        if (!faces.More()) return Result::Failure(base::MakeError(kThickenSourceMissing,"元の面がありません。",{}));
        const auto face=TopoDS::Face(faces.Current()); faces.Next();
        const GeomLib_IsPlanarSurface surface(BRep_Tool::Surface(face), tolerance.modelLinearMm);
        if (faces.More() || !surface.IsPlanar())
            return Result::Failure(base::MakeError(kThickenBadTarget,"面までは平らな元面から作成してください。","曲がった元面から一定方向へ伸ばす場合は押し出しを使用してください。"));
        const auto normal=surface.Plan().Axis().Direction();
        geometry::Vector3 direction{normal.X(),normal.Y(),normal.Z()};
        GProp_GProps props; BRepGProp::SurfaceProperties(sourceShape,props);
        const auto center=props.CentreOfMass();
        geometry::Vector3 destination;
        modeling::ExtrudeRequest request;
        auto directionPlane=plane;
        if (LookupShape(target,targetShape)) {
            BRepGProp::SurfaceProperties(targetShape,props);
            const auto point=props.CentreOfMass(); destination={point.X(),point.Y(),point.Z()};
            directionPlane.reset();
            TopExp_Explorer targetFaces(targetShape,TopAbs_FACE);
            if(targetFaces.More()) {
                const GeomLib_IsPlanarSurface targetSurface(BRep_Tool::Surface(TopoDS::Face(targetFaces.Current())),tolerance.modelLinearMm);
                if(targetSurface.IsPlanar()) {
                    const auto flat=targetSurface.Plan();const auto normal=flat.Axis().Direction();const auto location=flat.Location();
                    modeling::WorkPlaneFrame frame;frame.origin={location.X(),location.Y(),location.Z()};
                    frame.normal={normal.X(),normal.Y(),normal.Z()};directionPlane=frame;
                }
            }
        } else if (plane) {
            request.targetKind=modeling::ExtrudeTargetKind::Plane;
            request.targetPlane=*plane; destination=plane->origin;
        } else return Result::Failure(base::MakeError(kThickenBadTarget,"終端の面を選んでください。",{}));
        const geometry::Vector3 origin{center.X(),center.Y(),center.Z()};
        double toward = geometry::Dot(destination-origin,direction);
        if (directionPlane) {
            const double cosine = geometry::Dot(direction,directionPlane->normal);
            if (std::abs(cosine)<1e-9) return Result::Failure(base::MakeError(kThickenBadTarget,"元面の法線が終端面に平行です。",{}));
            toward = geometry::Dot(directionPlane->origin-origin,directionPlane->normal)/cosine;
        }
        if (toward<0) direction=direction*-1;
        request.customDirection=direction;
        const auto built=BuildExtrudeToFace(request,source,target,tolerance,offset);
        if (!built.HasValue()) return Result::Failure(built.Diagnostics());
        if (built.Value().parts.size()!=1) {
            for(const auto& part:built.Value().parts) ReleaseShape(part.handle);
            return Result::Failure(base::MakeError(kThickenBadTarget,"終端までが1つの部品になりません。",{}));
        }
        ThickenedSolid result;
        result.handle=built.Value().parts.front().handle;
        result.volumeMm3=built.Value().totalVolumeMm3;
        const auto edges=OutputEdges(result.handle,tolerance.modelLinearMm);
        if (!edges.HasValue()) {ReleaseShape(result.handle);return Result::Failure(edges.Diagnostics());}
        result.edges=edges.Value();
        const auto mesh=BuildShapeMesh(result.handle);
        if (mesh.HasValue()) for(const auto& triangle:mesh.Value().triangles)
            for(const auto& point:triangle.points)
                result.thicknessMm=std::max(result.thicknessMm,geometry::Dot(point-origin,direction));
        return Result::Success(std::move(result));
    } catch(const Standard_Failure& error) {
        return Result::Failure(base::MakeError(kThickenFailed,"終端まで立体化できません。",error.GetMessageString()));
    }
#else
    (void)source;(void)target;(void)plane;(void)offset;(void)tolerance;
    return Result::Failure(base::MakeError("KER-E005","幾何カーネルが必要です。",{}));
#endif
}
}
