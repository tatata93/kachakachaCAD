#include "kachakacha/kernel/OcctFaceQuery.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <Geom_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt2d.hxx>
#include <Standard_Failure.hxx>
#endif

namespace kachakacha::v2::kernel {
base::Result<fabrication::SurfaceEvaluator> FabricationEvaluatorOf(
    modeling::KernelShapeHandle handle, std::size_t faceIndex)
{
    using Out=base::Result<fabrication::SurfaceEvaluator>;
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        TopoDS_Shape shape;
        if (!LookupShape(handle,shape)) return Out::Failure(base::MakeError(kFaceSourceMissing,
            "近似の元の面がありません。", "面を再生成してください。"));
        TopExp_Explorer faces(shape,TopAbs_FACE);
        for(std::size_t index=0;index<faceIndex && faces.More();++index) faces.Next();
        if(!faces.More())return Out::Failure(base::MakeError(kFaceIndexOutOfRange,
            "近似の面番号が範囲外です。", "対象の面を選び直してください。"));
        const auto face=TopoDS::Face(faces.Current());
        const auto surface=BRep_Tool::Surface(face);
        if(surface.IsNull())return Out::Failure(base::MakeError(kFaceSourceMissing,
            "近似の曲面を評価できません。", "元の面を確認してください。"));
        double u0,u1,v0,v1;
        BRepTools::UVBounds(face,u0,u1,v0,v1);
        int wireCount=0;
        for(TopExp_Explorer wires(face,TopAbs_WIRE);wires.More();wires.Next())++wireCount;
        if(wireCount>1)return Out::Failure(base::MakeError("FAB-B006",
            "開口のある面を矩形の帯で埋めることはできません。",
            "開口を境界として指定してください。平面なら実際の外周と開口をそのまま使います。"));
        // A rectangular UV grid must not extend outside a trimmed face or fill holes.
        // Capture the surface handle by value; document rebuilds may release its cache handle.
        for(int row=0;row<=32;++row)for(int column=0;column<=32;++column) {
            const gp_Pnt2d point(u0+(u1-u0)*row/32.0,v0+(v1-v0)*column/32.0);
            BRepClass_FaceClassifier classified(face,point,1e-7);
            if(classified.State()==TopAbs_OUT)return Out::Failure(base::MakeError("FAB-B006",
                "この面のトリム・開口は矩形の帯へ置き換えられません。",
                "外形を横切る近似は作りません。四辺の区画へ分けるか、開口を別の境界として指定してください。"));
        }
        fabrication::SurfaceEvaluator evaluate=[surface,u0,u1,v0,v1](double u,double v) {
            const auto point=surface->Value(u0+(u1-u0)*v,v0+(v1-v0)*u);
            return geometry::Vector3{point.X(),point.Y(),point.Z()};
        };
        return Out::Success(std::move(evaluate));
    } catch(const Standard_Failure&) {
        return Out::Failure(base::MakeError(kFaceSourceMissing,
            "近似の元の曲面を評価できません。", "元の面の範囲と退化を確認してください。"));
    }
#else
    (void)handle;(void)faceIndex;
    return Out::Failure(base::MakeError(kFaceSourceMissing,
        "近似の曲面評価には幾何カーネルが必要です。", "OCCTを有効にしてください。"));
#endif
}
} // namespace kachakacha::v2::kernel
