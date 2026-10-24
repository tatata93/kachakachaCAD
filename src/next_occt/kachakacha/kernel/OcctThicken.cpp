#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/kernel/OcctCorrectedThicken.h"

#include <cmath>
#include <string>
#include <algorithm>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/kernel/OcctFoldedBandThicken.h"
#include "kachakacha/kernel/OcctRevolvedThicken.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <Bnd_Box.hxx>
#include <BRepGProp.hxx>
#include "kachakacha/kernel/OcctMassProperties.h"
#include <BRepLib.hxx>
#include <BRep_Builder.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffset_MakeOffset.hxx>
#include <GeomAbs_JoinType.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <NCollection_Array1.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Dir.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>

#include <exception>

#endif

namespace kachakacha::v2::kernel {
namespace {

using base::MakeError;
using base::Result;

} // namespace

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

//! 立体の辺を core の曲線へ直す。画面に出すために使う。
//! 直せない辺は飛ばす。飛ばしても立体は正しいので、断る理由にはしない。
[[nodiscard]] std::vector<geometry::CurveSegment> EdgesOf(const TopoDS_Shape& shape,
    const geometry::GeometryTolerance& tolerance)
{
    std::vector<geometry::CurveSegment> edges;
    for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const auto made =
            FromEdge(TopoDS::Edge(explorer.Current()), tolerance.modelLinearMm);
        if (made.HasValue()) {
            edges.push_back(made.Value());
        }
    }
    return edges;
}

// Adjacent planar faces need intersected offset planes at their real creases.
// The simple offset deliberately does not compute those intersections.
bool PlanarSheetWithCreases(const TopoDS_Shape& shape){
    int count=0;for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()){
        if(BRepAdaptor_Surface(TopoDS::Face(f.Current()),true).GetType()!=GeomAbs_Plane)return false;
        ++count;
    }
    return count>1;
}

//! 面を法線方向へずらした面(シェル)を作る。中央付け・内側付けの下ごしらえ。
[[nodiscard]] Result<TopoDS_Shape> OffsetSurface(const TopoDS_Shape& face,
    double distanceMm,const geometry::GeometryTolerance& tolerance,bool intersect = false)
{
    using Out = Result<TopoDS_Shape>;
    try {
        if(face.ShapeType()==TopAbs_COMPOUND){
            TopoDS_Compound islands;BRep_Builder builder;builder.MakeCompound(islands);
            for(TopoDS_Iterator i(face);i.More();i.Next()){
                const auto moved=OffsetSurface(i.Value(),distanceMm,tolerance,intersect);
                if(!moved.HasValue())return moved;builder.Add(islands,moved.Value());
            }
            return Out::Success(islands);
        }
        BRepOffsetAPI_MakeOffsetShape maker;
        if(intersect||PlanarSheetWithCreases(face))maker.PerformByJoin(face,distanceMm,tolerance.modelLinearMm,
            BRepOffset_Skin,false,false,GeomAbs_Intersection,true);
        else maker.PerformBySimple(face, distanceMm);
        if (!maker.IsDone()) {
            return Out::Failure(MakeError(kThickenFailed,
                "面に厚みを付けられませんでした。",
                "面をずらせませんでした。厚みが曲がりに対して大きすぎるか、"
                "ずらした面が自分自身と交わります。厚みを小さくしてください。"));
        }
        return Out::Success(maker.Shape());
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kThickenFailed,
            "面に厚みを付けられませんでした。", error.what()));
    } catch (...) {
        return Out::Failure(MakeError(kThickenFailed,
            "面に厚みを付けられませんでした。", "幾何カーネルが失敗しました。"));
    }
}

//! 面(またはシェル)を、法線方向へ厚みぶん膨らませて立体にする。
[[nodiscard]] Result<TopoDS_Shape> ThickSolid(const TopoDS_Shape& base,
    double thicknessMm,const geometry::GeometryTolerance& tolerance,bool intersect = false)
{
    using Out = Result<TopoDS_Shape>;
    try {
        if(base.ShapeType()==TopAbs_COMPOUND){
            TopoDS_Compound islands;BRep_Builder builder;builder.MakeCompound(islands);
            for(TopoDS_Iterator i(base);i.More();i.Next()){
                const auto made=ThickSolid(i.Value(),thicknessMm,tolerance,intersect);
                if(!made.HasValue())return made;builder.Add(islands,made.Value());
            }
            return Out::Success(islands);
        }
        TopoDS_Shape thickened;bool done=false;
        if(intersect||PlanarSheetWithCreases(base)){
            BRepOffset_MakeOffset maker;
            maker.Initialize(base,thicknessMm,tolerance.modelLinearMm,
                BRepOffset_Skin,false,false,GeomAbs_Intersection,true,true);
            maker.MakeOffsetShape();done=maker.IsDone();if(done)thickened=maker.Shape();
        }else {
            BRepOffsetAPI_MakeThickSolid maker;maker.MakeThickSolidBySimple(base,thicknessMm);
            done=maker.IsDone();if(done)thickened=maker.Shape();
        }
        if (!done) {
            return Out::Failure(MakeError(kThickenFailed,
                "面に厚みを付けられませんでした。",
                "厚みが曲がりに対して大きすぎるか、面が自分自身と交わります。"
                "厚みを小さくするか、面を分けてください。"));
        }
        // Simple thickening can return an inside-out solid. Absolute volume hid
        // that defect, while STL consumers received inward-facing triangles.
        std::vector<TopoDS_Solid> solids;
        for (TopExp_Explorer e(thickened, TopAbs_SOLID); e.More(); e.Next()) {
            auto solid = TopoDS::Solid(e.Current());
            if (!BRepLib::OrientClosedSolid(solid)) {
                return Out::Failure(MakeError(kThickenFailed,
                    "厚みを付けた形の内外を決められません。", "開いた殻や不整合な接続があります。"));
            }
            solids.push_back(solid);
        }
        if (solids.empty()) return Out::Failure(MakeError(kThickenFailed,
            "厚みを付けても閉じた立体になりませんでした。", {}));
        if (solids.size() == 1) return Out::Success(solids.front());
        TopoDS_Compound compound; BRep_Builder builder; builder.MakeCompound(compound);
        for (const auto& solid : solids) builder.Add(compound, solid);
        return Out::Success(compound);
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kThickenFailed,
            "面に厚みを付けられませんでした。", error.what()));
    } catch (...) {
        return Out::Failure(MakeError(kThickenFailed,
            "面に厚みを付けられませんでした。", "幾何カーネルが失敗しました。"));
    }
}

} // namespace

namespace {
bool OffsetCrossesFocalPoint(const TopoDS_Shape& shape,double first,double last) {
    for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()) {
        const auto face=TopoDS::Face(e.Current());BRepAdaptor_Surface surface(face,true);
        const int nu=surface.NbUIntervals(GeomAbs_CN),nv=surface.NbVIntervals(GeomAbs_CN);
        if(nu<=0||nv<=0||nu>20000/nv)continue;
        NCollection_Array1<double> us(1,nu+1),vs(1,nv+1);
        surface.UIntervals(us,GeomAbs_CN);surface.VIntervals(vs,GeomAbs_CN);
        for(int i=1;i<=nu;++i)for(int j=1;j<=nv;++j)for(double a:{1.0/6,.5,5.0/6})for(double b:{1.0/6,.5,5.0/6}) {
            const double u=us(i)+(us(i+1)-us(i))*a,v=vs(j)+(vs(j+1)-vs(j))*b;
            if(BRepClass_FaceClassifier(face,gp_Pnt2d(u,v),1e-9).State()==TopAbs_OUT)continue;
            gp_Pnt p;gp_Vec du,dv,duu,dvv,duv;surface.D2(u,v,p,du,dv,duu,dvv,duv);
            auto normal=du.Crossed(dv);const double area=normal.SquareMagnitude();if(area<1e-28)continue;
            normal.Normalize();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
            const double E=du.Dot(du),F=du.Dot(dv),G=dv.Dot(dv);
            const double l=normal.Dot(duu),m=normal.Dot(duv),n=normal.Dot(dvv);
            const double mean=(l*G-2*m*F+n*E)/(2*area),gaussian=(l*n-m*m)/area;
            const double delta=std::sqrt(std::max(0.0,mean*mean-gaussian));
            for(double k:{mean-delta,mean+delta})for(double distance:{first,last})
                if(std::isfinite(k)&&1-distance*k<=1e-9)return true;
        }
    }
    return false;
}
}

Result<ThickenedSolid> ThickenSurface(modeling::KernelShapeHandle sourceShape,
    double thicknessMm, fabrication::ThicknessPlacement placement,
    const geometry::GeometryTolerance& tolerance, bool correctOpposite)
{
    using Out = Result<ThickenedSolid>;
    if(correctOpposite) {
        const auto exact=ThickenSurface(sourceShape,thicknessMm,placement,tolerance,false);
        if(exact.HasValue())return exact;
        if(!(thicknessMm>0)||!std::isfinite(thicknessMm))return exact;
        return detail::CorrectedThicken(sourceShape,thicknessMm,placement,tolerance);
    }
    if (!(thicknessMm > 0.0) || !std::isfinite(thicknessMm)) {
        return Out::Failure(MakeError(kThickenBadThickness,
            "厚みが正の数ではありません。",
            "0mm の板は作れません。付けたい厚みを入れてください。"));
    }
    TopoDS_Shape face;
    if (!LookupShape(sourceShape, face) || face.IsNull()) {
        return Out::Failure(MakeError(kThickenSourceMissing,
            "元になる面がありません。",
            "先に形状ガイドで面を作ってから、厚みを付けてください。"));
    }
    // OCCT's simple offset adds side-surface pcurves to shared source edges.
    // A private topology/geometry copy keeps the source and later rebuilds intact.
    try {
        BRepBuilderAPI_Copy copy(face,true,false);
        if(!copy.IsDone())return Out::Failure(MakeError(kThickenFailed,"計算用の面を複製できませんでした。"));
        face=copy.Shape();
    } catch(const Standard_Failure&) {
        return Out::Failure(MakeError(kThickenFailed,"計算用の面を複製できませんでした。"));
    }
    // どこから膨らませ始めるかを、厚みの付け方で決める。
    //   外側 : 元の面から外へ    → 元の面が内側の皮になる
    //   中央 : 半分だけ内へ下げてから膨らませる → 元の面が厚みの真ん中に残る
    //   内側 : 厚みぶん内へ下げてから膨らませる → 元の面が外側の皮になる
    // 「膨らませてから半分戻す」ではないのは、曲がった面では戻し方が一意に
    // 決まらず、元の面が動いてしまうからである。
    const double startOffset =
        placement == fabrication::ThicknessPlacement::Centered ? -thicknessMm * 0.5
                                                               : 0.0;
    // A focal fold found at any point is a definitive invalid offset. Absence
    // of a sampled fold is not acceptance: the full solid checks still run.
    try {
        const double endOffset=placement==fabrication::ThicknessPlacement::Inside?-thicknessMm:
            placement==fabrication::ThicknessPlacement::Centered?thicknessMm*.5:thicknessMm;
        if(OffsetCrossesFocalPoint(face,startOffset,endOffset))return Out::Failure(MakeError(kThickenFailed,
            "この厚みでは曲面の内側が折り返します。",
            "局所的な曲率半径を超える厚みです。厚みを小さくするか、外側付けへ切り替えてください。元の面は変更していません。"));
    } catch(const Standard_Failure&) {
        // A preliminary differential check is optional; the authoritative
        // construction and validity checks below retain their existing refusal.
    }
    auto folded=detail::ThickenFoldedBand(face,thicknessMm,placement,tolerance);
    if(!folded)folded=detail::ThickenRevolvedProfile(face,thicknessMm,placement,tolerance);
    TopoDS_Shape base = face;
    if (!folded && startOffset != 0.0) {
        const auto moved = OffsetSurface(face, startOffset,tolerance);
        if (!moved.HasValue()) {
            return Out::Failure(moved.Diagnostics());
        }
        base = moved.Value();
    }
    // A one-sided inward wall starts at the exact source and offsets once.
    // Offset(-t) followed by Offset(+t) creates nested offset surfaces and can
    // corrupt B-spline revolution seams instead of retaining the source skin.
    const double signedThickness=placement==fabrication::ThicknessPlacement::Inside ? -thicknessMm : thicknessMm;
    const auto built = folded ? *folded : ThickSolid(base, signedThickness,tolerance);
    if (!built.HasValue()) {
        return Out::Failure(built.Diagnostics());
    }
    ThickenedSolid made;
    TopoDS_Shape finalShape=built.Value();
    made.handle = StoreShape(finalShape);
    auto checked = CheckSolidForExport(made.handle, tolerance.modelLinearMm);
    // A shell can mix curved and planar faces. Simple offsetting does not
    // intersect their displaced boundaries; retain the fast path, but rebuild
    // failed results with native intersections and the SAME safety checks.
    if(!folded&&(!checked.HasValue()||!checked.Value().Ok())) {
        auto joinedBase=startOffset==0.0?Result<TopoDS_Shape>::Success(face)
            :OffsetSurface(face,startOffset,tolerance,true);
        if(joinedBase.HasValue()) {
            const auto joined=ThickSolid(joinedBase.Value(),signedThickness,tolerance,true);
            if(joined.HasValue()) {
                const auto handle=StoreShape(joined.Value());
                const auto check=CheckSolidForExport(handle,tolerance.modelLinearMm);
                if(check.HasValue()&&check.Value().Ok()) {
                    ReleaseShape(made.handle);made.handle=handle;checked=check;finalShape=joined.Value();
                } else ReleaseShape(handle);
            }
        }
    }
    bool joinedIndividualFaces=false;
    if(!folded&&(!checked.HasValue()||!checked.Value().Ok()))try {
        // Some exact composite spline faces are valid for simple thickening
        // but unsupported by OCCT's joined offset (C0 parameter knots). Build
        // the finite wall of each original face, then unite their overlaps.
        // Keep all source geometry and holes; never fill a hole or raise the
        // tolerance to force a connection. A disconnected result is refused.
        TopoDS_Shape united;int count=0;
        for(TopExp_Explorer f(face,TopAbs_FACE);f.More();f.Next()) {
            BRepBuilderAPI_Copy copy(f.Current(),true,false);
            auto skin=startOffset==0.0?Result<TopoDS_Shape>::Success(copy.Shape())
                :OffsetSurface(copy.Shape(),startOffset,tolerance);
            if(!skin.HasValue()){united.Nullify();break;}
            const auto wall=ThickSolid(skin.Value(),signedThickness,tolerance);
            if(!wall.HasValue()){united.Nullify();break;}
            if(count++==0)united=wall.Value();
            else {
                BRepAlgoAPI_Fuse fuse;fuse.SetNonDestructive(true);
                TopTools_ListOfShape args,tools;args.Append(united);tools.Append(wall.Value());
                fuse.SetArguments(args);fuse.SetTools(tools);fuse.Build();
                if(!fuse.IsDone()||fuse.HasErrors()){united.Nullify();break;}
                united=fuse.Shape();
            }
        }
        int solids=0;if(!united.IsNull())for(TopExp_Explorer s(united,TopAbs_SOLID);s.More();s.Next())++solids;
        if(count>1&&solids==1) {
            for(TopExp_Explorer s(united,TopAbs_SOLID);s.More();s.Next()) {
                auto solid=TopoDS::Solid(s.Current());if(!BRepLib::OrientClosedSolid(solid))break;
                const auto handle=StoreShape(solid);const auto check=CheckSolidForExport(handle,tolerance.modelLinearMm);
                if(check.HasValue()&&check.Value().Ok()) {
                    ReleaseShape(made.handle);made.handle=handle;checked=check;finalShape=solid;joinedIndividualFaces=true;
                } else ReleaseShape(handle);
            }
        }
    }catch(const Standard_Failure&){} // Preserve the refusal when native union cannot make one safe solid.
    if (!checked.HasValue() || !checked.Value().Ok()) {
        ReleaseShape(made.handle);
        if(!checked.HasValue())return Out::Failure(MakeError(kThickenFailed,
            "厚みを付けた形の安全性を確認できませんでした。",
            checked.FirstSummaryJa()+(checked.Diagnostics().empty()?std::string():
                "\n"+checked.Diagnostics().front().detailsJa)));
        const auto reason=!checked.Value().closed
            ? "厚み面の境界が閉じていないか、1本の辺へ3面以上が接続しています。"
            : !checked.Value().selfIntersectionFree
                ? "厚み面の接続・境界の整合性または自己交差の検査に失敗しました。"
                : "閉じた形の体積または内外の向きが不正です。";
        return Out::Failure(MakeError(kThickenFailed,
            "厚みを付けた形の安全検査に失敗したため、部品を作りませんでした。",
            std::string(reason)+"厚みを小さくするか、急な折れや巻き縁を別部品にしてください。"));
    }
    made.volumeMm3 = checked.Value().volumeMm3;
    made.thicknessMm = thicknessMm;
    made.normalThicknessMinMm = made.normalThicknessMaxMm = thicknessMm;
    made.edges = EdgesOf(finalShape, tolerance);
    std::vector<base::Diagnostic> notes;
    if(joinedIndividualFaces)notes.push_back(base::MakeWarning("THK-009",
        "各面の厚み領域を結合しました。", "継ぎ目は各面の厚み領域の和です。元の面と穴を保持し、閉鎖性・体積・自己交差を検査しました。"));
    return Out::Success(std::move(made),std::move(notes));
}

namespace {

//! 面の点が、平面からどちら側へどれだけ離れているか。
struct PlaneReach {
    double maximumMm = 0.0;   //!< いちばん遠い点までの距離(正)
    int side = 0;             //!< +1 / -1。0 なら平面の上か、またいでいる
    geometry::Vector3 farthest{};
};

[[nodiscard]] Result<PlaneReach> MeasureReach(const TopoDS_Shape& face,
    const geometry::Vector3& origin, const geometry::Vector3& unitNormal,
    double toleranceMm)
{
    using Out = Result<PlaneReach>;
    PlaneReach reach;
    // Measure the entire boundary, including circular edges without extrema vertices.
    gp_Trsf local;local.SetTransformation(gp_Ax3(ToPoint(origin),gp_Dir(ToVector(unitNormal))));
    const auto transformed=BRepBuilderAPI_Transform(face,local,false).Shape();
    Bnd_Box bounds;BRepBndLib::AddOptimal(transformed,bounds,false,false);
    if(bounds.IsVoid())return Out::Failure(MakeError(kThickenBadTarget,"面の範囲を測れません。",{}));
    double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
    const bool positive=z1>toleranceMm,negative=z0<-toleranceMm;
    reach.maximumMm=std::max(std::abs(z0),std::abs(z1));
    reach.side=positive?1:-1;
    reach.farthest=origin+unitNormal*(reach.maximumMm*reach.side);
    if (positive && negative) {
        return Out::Failure(MakeError(kThickenBadTarget,
            "面が相手の平面をまたいでいます。",
            "面の一部が平面の向こう側にあります。どちら側を埋めるのか決まらないので、"
            "平面を面の片側へ置いてください。"));
    }
    if (!positive && !negative) {
        return Out::Failure(MakeError(kThickenBadTarget,
            "面が相手の平面の上に載っています。",
            "面と平面の間に厚みがありません。平面を面から離してください。"));
    }
    return Out::Success(reach);
}

//! 面の真ん中の法線。厚みをどちらへ膨らませるかを決めるのに使う。
[[nodiscard]] geometry::Vector3 MiddleNormal(const TopoDS_Shape& shape)
{
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());
        double u0 = 0.0, u1 = 0.0, v0 = 0.0, v1 = 0.0;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        BRepGProp_Face properties(face);
        gp_Pnt point;
        gp_Vec normal;
        properties.Normal((u0 + u1) * 0.5, (v0 + v1) * 0.5, point, normal);
        if (normal.Magnitude() > 1.0e-12) {
            return geometry::Vector3{normal.X(), normal.Y(), normal.Z()}
                * (1.0 / normal.Magnitude());
        }
    }
    return geometry::Vector3{0.0, 0.0, 1.0};
}

} // namespace

Result<ThickenedSolid> ThickenSurfaceToPlane(modeling::KernelShapeHandle sourceShape,
    const geometry::Vector3& planeOrigin, const geometry::Vector3& planeNormal,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<ThickenedSolid>;
    if (!(planeNormal.Length() > 1.0e-12)) {
        return Out::Failure(MakeError(kThickenBadTarget, "相手の平面の向きが決まりません。", {}));
    }
    TopoDS_Shape face;
    if (!LookupShape(sourceShape, face) || face.IsNull()) {
        return Out::Failure(MakeError(kThickenSourceMissing,
            "元になる面がありません。",
            "先に形状ガイドで面を作ってから、平面まで立体にしてください。"));
    }
    try {
    TopExp_Explorer sourceFaces(face,TopAbs_FACE);
    if(!sourceFaces.More())return Out::Failure(MakeError(kThickenBadTarget,"元になる面がありません。",{}));
    const auto sourceFace=TopoDS::Face(sourceFaces.Current());sourceFaces.Next();
    const GeomLib_IsPlanarSurface planar(BRep_Tool::Surface(sourceFace),tolerance.modelLinearMm);
    if(sourceFaces.More()||!planar.IsPlanar())return Out::Failure(MakeError(kThickenBadTarget,
        "平面までの立体化は、1枚の平らな面で使用してください。",
        "曲面や折れた面の法線は場所で変わるため、平面まで一様には届きません。曲面には厚みを数値で指定してください。"));
    const geometry::Vector3 unitNormal = planeNormal * (1.0 / planeNormal.Length());
    const auto reach = MeasureReach(face, planeOrigin, unitNormal,
        std::max(tolerance.modelLinearMm, 1.0e-6));
    if (!reach.HasValue()) {
        return Out::Failure(reach.Diagnostics());
    }
    // 平面へ向かう向き。面の法線がそちらを向いていなければ、負の厚みで膨らませる。
    const geometry::Vector3 toward = unitNormal * (-static_cast<double>(reach.Value().side));
    const auto sourceNormal=MiddleNormal(face);
    const double cosine=Dot(sourceNormal,toward);
    if(std::abs(cosine)<=std::max(tolerance.modelAngularRad,1e-9))return Out::Failure(MakeError(kThickenBadTarget,
        "面の法線方向では相手の平面に届きません。","相手の平面または元の面の向きを変更してください。"));
    // Exact planar reach, including steeply inclined targets. A fixed multiplier
    // can leave part of the source short of the target while still making a solid.
    const double length=(reach.Value().maximumMm+std::max(tolerance.modelLinearMm*10,1e-5))/std::abs(cosine);
    BRepPrimAPI_MakePrism prism(face,ToVector(sourceNormal*(cosine>0?length:-length)),true);
    if(!prism.IsDone())return Out::Failure(MakeError(kThickenFailed,"平面へ向かう立体を作れませんでした。",{}));
        const double margin = std::max(reach.Value().maximumMm * 10.0, 1000.0);
        const gp_Pln plane(ToPoint(planeOrigin), gp_Dir(ToVector(unitNormal)));
        BRepBuilderAPI_MakeFace maker(plane, -margin, margin, -margin, margin);
        if (!maker.IsDone()) {
            return Out::Failure(MakeError(kThickenTrimFailed,
                "相手の平面で切るための面を作れませんでした。", {}));
        }
        // 面のある側だけを残す。
        BRepPrimAPI_MakeHalfSpace halfSpace(maker.Face(), ToPoint(reach.Value().farthest));
        if (!halfSpace.IsDone()) {
            return Out::Failure(MakeError(kThickenTrimFailed,
                "相手の平面で切るための領域を作れませんでした。", {}));
        }
        TopTools_ListOfShape arguments,tools;arguments.Append(prism.Shape());tools.Append(halfSpace.Solid());
        BRepAlgoAPI_Common common;common.SetArguments(arguments);common.SetTools(tools);common.SetNonDestructive(true);
        common.Build();
        if (!common.IsDone()) {
            return Out::Failure(MakeError(kThickenTrimFailed,
                "相手の平面で切れませんでした。", {}));
        }
        TopExp_Explorer solids(common.Shape(),TopAbs_SOLID);
        if(!solids.More())return Out::Failure(MakeError(kThickenTrimFailed,"平面で切った結果、立体が残りませんでした。",{}));
        auto result=TopoDS::Solid(solids.Current());solids.Next();
        if(solids.More()||!BRepLib::OrientClosedSolid(result))return Out::Failure(MakeError(kThickenTrimFailed,
            "1つの閉じた立体にできませんでした。","面を分けて作成してください。"));
        const auto handle=StoreShape(result);
        const auto checked=CheckSolidForExport(handle,tolerance.modelLinearMm);
        if (!checked.HasValue()||!checked.Value().Ok()) {
            ReleaseShape(handle);
            return Out::Failure(MakeError(kThickenTrimFailed,
                "平面までの立体が安全検査に通らないため、部品を作りませんでした。",
                checked.HasValue()?"閉鎖性・法線・体積・自己交差を確認してください。":checked.FirstDetailsJa()));
        }
        ThickenedSolid made;
        made.handle = handle;
        made.volumeMm3 = checked.Value().volumeMm3;
        made.thicknessMm = reach.Value().maximumMm;
        made.edges = EdgesOf(result, tolerance);
        return Out::Success(std::move(made));
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kThickenTrimFailed,
            "相手の平面で切れませんでした。", error.what()));
    } catch (...) {
        return Out::Failure(MakeError(kThickenTrimFailed,
            "相手の平面で切れませんでした。", "幾何カーネルが失敗しました。"));
    }
}

#else

Result<ThickenedSolid> ThickenSurface(modeling::KernelShapeHandle,
    double, fabrication::ThicknessPlacement, const geometry::GeometryTolerance&, bool)
{
    return Result<ThickenedSolid>::Failure(MakeError(kThickenFailed,
        "面に厚みを付けられませんでした。",
        "この組み立てには幾何カーネルが入っていません。"));
}

Result<ThickenedSolid> ThickenSurfaceToPlane(modeling::KernelShapeHandle,
    const geometry::Vector3&, const geometry::Vector3&, const geometry::GeometryTolerance&)
{
    return Result<ThickenedSolid>::Failure(MakeError(kThickenFailed,
        "面に厚みを付けられませんでした。",
        "この組み立てには幾何カーネルが入っていません。"));
}

#endif

} // namespace kachakacha::v2::kernel
