#include "kachakacha/kernel/OcctImage.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctTessellate.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>
#endif
namespace kachakacha::v2::kernel {
using namespace geometry;
using base::Result;
namespace {
template<class T> Result<T> Failure(){return Result<T>::Failure(base::MakeError("IMG-003","貼付先の面を読み取れません。","別の面か貼り方を選んでください。"));}
}
Result<ImageSurfacePoint> ImagePointOnSurface(modeling::KernelShapeHandle handle,const Vector3& point) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        TopoDS_Shape shape;if(!LookupShape(handle,shape))return Failure<ImageSurfacePoint>();
        TopExp_Explorer e(shape,TopAbs_FACE);if(!e.More())return Failure<ImageSurfacePoint>();
        const auto surface=BRep_Tool::Surface(TopoDS::Face(e.Current()));
        GeomAPI_ProjectPointOnSurf project(ToPoint(point),surface);
        if(project.NbPoints()==0)return Failure<ImageSurfacePoint>();
        double u,v;project.LowerDistanceParameters(u,v);gp_Pnt p;gp_Vec du,dv;surface->D1(u,v,p,du,dv);
        if(du.Magnitude()<1e-12||dv.Magnitude()<1e-12)return Failure<ImageSurfacePoint>();
        return Result<ImageSurfacePoint>::Success({FromPoint(p),{u,v,0},{du.Magnitude(),dv.Magnitude(),0}});
    }catch(...){return Failure<ImageSurfacePoint>();}
#else
    (void)handle;(void)point;return Failure<ImageSurfacePoint>();
#endif
}
Result<std::vector<modeling::ImageTriangle>> ImageSurfaceTriangles(const domain::CreateImageDefinition& image) {
    using Triangles=std::vector<modeling::ImageTriangle>;
    if(image.faceBrep.empty())return Result<Triangles>::Success(modeling::FlatImageTriangles(image));
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        const auto handle=RestoreOutputShape(image.faceBrep);if(!handle.HasValue())return Failure<Triangles>();
        TopoDS_Shape shape;if(!LookupShape(handle.Value(),shape))return Failure<Triangles>();
        // Retain the captured tessellation: remeshing a face alone may choose a
        // different deflection than its parent and make a coplanar decal flicker.
        bool complete=true;
        for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()) {
            TopLoc_Location location;const auto mesh=BRep_Tool::Triangulation(TopoDS::Face(e.Current()),location);
            if(mesh.IsNull()||!mesh->HasUVNodes()){complete=false;break;}
        }
        if(!complete){const auto tessellation=BuildShapeMesh(handle.Value());if(!tessellation.HasValue())return Failure<Triangles>();}
        Triangles result;
        for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()) {
            const auto face=TopoDS::Face(e.Current());TopLoc_Location location;
            const auto mesh=BRep_Tool::Triangulation(face,location);
            if(mesh.IsNull()||!mesh->HasUVNodes())return Failure<Triangles>();
            for(int i=1;i<=mesh->NbTriangles();++i) {
                int a,b,c;mesh->Triangle(i).Get(a,b,c);const std::array<int,3> nodes{a,b,c};modeling::ImageTriangle triangle;
                for(std::size_t k=0;k<3;++k){const auto p=mesh->Node(nodes[k]).Transformed(location.Transformation());const auto uv=mesh->UVNode(nodes[k]);
                    triangle.mesh.points[k]=FromPoint(p);triangle.pixels[k]=modeling::ImagePixelAt(image,FromPoint(p),{uv.X(),uv.Y(),0});}
                triangle.mesh.normal=Normalized(Cross(triangle.mesh.points[1]-triangle.mesh.points[0],triangle.mesh.points[2]-triangle.mesh.points[0]));
                result.push_back(triangle);
            }
        }
        return Result<Triangles>::Success(std::move(result));
    }catch(...){return Failure<Triangles>();}
#else
    return Failure<Triangles>();
#endif
}
}
