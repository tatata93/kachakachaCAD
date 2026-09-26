#include "kachakacha/kernel/OcctGptFabrication.h"
#include "kachakacha/fabrication/GptApproximation.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Pnt.hxx>
#endif

namespace kachakacha::v2::kernel {
namespace {
using Out=base::Result<app::FabricationEvaluation>;
using geometry::Vector3;
#ifdef KACHACAD_V2_WITH_OCCT
Vector3 Point(const gp_Pnt& p) { return {p.X(),p.Y(),p.Z()}; }
std::vector<Vector3> Loop(const TopoDS_Wire& wire,const TopoDS_Face& face,double tolerance)
{
    std::vector<Vector3> points;
    for (BRepTools_WireExplorer walk(wire,face);walk.More();walk.Next()) {
        const auto edge=walk.Current();
        BRepAdaptor_Curve curve(edge);
        GCPnts_QuasiUniformDeflection sample(curve,tolerance);
        if (!sample.IsDone() || sample.NbPoints()<2) { return {}; }
        const int count=sample.NbPoints();
        for (int at=1;at<count;++at) {
            const int index=edge.Orientation()==TopAbs_REVERSED ? count-at+1 : at;
            points.push_back(Point(sample.Value(index)));
        }
    }
    return points;
}
base::Result<fabrication::GptApproxSource> ReadFace(const TopoDS_Face& face,double tolerance)
{
    using Result=base::Result<fabrication::GptApproxSource>;
    fabrication::GptApproxSource source;
    const auto outer=BRepTools::OuterWire(face);
    source.boundary=Loop(outer,face,tolerance/10);
    if (source.boundary.size()<3) { return Result::Failure(base::MakeError("GPT-F004","外周を読み取れません。","元の面の境界を確認してください。")); }
    for (TopExp_Explorer e(face,TopAbs_WIRE);e.More();e.Next()) {
        const auto wire=TopoDS::Wire(e.Current());
        if (wire.IsSame(outer)) { continue; }
        auto hole=Loop(wire,face,tolerance/10);
        if (hole.size()<3) { return Result::Failure(base::MakeError("GPT-F004","開口を読み取れません。","開口を消さずに処理を中止しました。")); }
        source.holes.push_back(std::move(hole));
    }
    double u0,u1,v0,v1; BRepTools::UVBounds(face,u0,u1,v0,v1);
    BRepAdaptor_Surface surface(face);
    for (int cells=64;cells<=256;cells*=2) {
        source.samples.clear(); double error=0;
        for (int row=0;row<cells;++row) {
            for (int col=0;col<cells;++col) {
                const double u=u0+(u1-u0)*(col+.5)/cells,v=v0+(v1-v0)*(row+.5)/cells;
                BRepClass_FaceClassifier inside(face,gp_Pnt2d(u,v),1e-7);
                if (inside.State()!=TopAbs_IN && inside.State()!=TopAbs_ON) { continue; }
                const auto center=Point(surface.Value(u,v)); source.samples.push_back(center);
                const double du=(u1-u0)/(2*cells),dv=(v1-v0)/(2*cells);
                const auto linear=(Point(surface.Value(u-du,v-dv))+Point(surface.Value(u+du,v-dv))
                    +Point(surface.Value(u-du,v+dv))+Point(surface.Value(u+du,v+dv)))*.25;
                error=std::max(error,(linear-center).Length());
            }
        }
        if (error<=tolerance/5 && source.samples.size()>=3) { return Result::Success(std::move(source)); }
    }
    return Result::Failure(base::MakeError("GPT-F005","曲面を十分な精度で測れません。","許容偏差を大きくするか、面を分けてください。"));
}
#endif
}
base::Result<app::FabricationEvaluation> BuildGptFabrication(
    const domain::CreateFabricationModelDefinition& definition,const std::vector<GptFabricationInput>& inputs)
{
    if (!std::isfinite(definition.targetMaxDeviation.value) || definition.targetMaxDeviation.value<=0
        || !std::isfinite(definition.materialThickness.value) || definition.materialThickness.value<=0
        || !std::isfinite(definition.minimumPartWidthMm) || definition.minimumPartWidthMm<=0
        || definition.maximumPartCount<1 || definition.maximumPartCount>64
        || definition.splitAxis<0 || definition.splitAxis>2) {
        return Out::Failure(base::MakeError("GPT-F001","近似の数値条件が不正です。","偏差・板厚・部材数・最小幅を確認してください。"));
    }
    if (inputs.empty() || inputs.size()!=definition.parts.size() || !definition.openingWires.empty()
        || !definition.foldWires.empty() || !definition.reliefCutWires.empty() || !definition.connectionWires.empty()
        || !definition.automaticBoundaries || !definition.manualBoundaries.empty()
        || definition.rangeUMin!=0 || definition.rangeUMax!=1 || definition.rangeVMin!=0 || definition.rangeVMax!=1) {
        return Out::Failure(base::MakeError("GPT-F006","GPT版で扱えない入力条件があります。","対象面を確認してください。追加の手動線・範囲指定は未対応です。入力は無視しません。"));
    }
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        app::FabricationEvaluation result; result.method=app::FabricationMethod::GptApproximation;
        double squared=0; std::size_t samples=0;
        for (const auto& input:inputs) {
            TopoDS_Shape shape;
            if (!LookupShape(input.handle,shape)) { return Out::Failure(base::MakeError("GPT-F007","元の面が見つかりません。","面を作り直して選択してください。")); }
            std::size_t faceIndex=0;
            for (TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next(),++faceIndex) {
                const auto source=ReadFace(TopoDS::Face(e.Current()),definition.targetMaxDeviation.value);
                if (!source.HasValue()) { return Out::Failure(source.Diagnostics()); }
                fabrication::GptApproxOptions options{definition.targetMaxDeviation.value,
                    definition.minimumPartWidthMm,definition.maximumPartCount-static_cast<int>(result.panels.size()),definition.splitAxis};
                const auto made=fabrication::ApproximateGpt(source.Value(),options);
                if (!made.HasValue()) { return Out::Failure(made.Diagnostics()); }
                result.reachedTolerance=result.reachedTolerance && made.Value().reached;
                result.maximumSeamGapMm=std::max(result.maximumSeamGapMm,made.Value().seamGapMm);
                for (auto panel:made.Value().panels) {
                    panel.pattern.panelId="GPT 部材"+std::to_string(result.panels.size()+1);
                    squared+=panel.squaredMm; samples+=panel.sampleCount;
                    result.maximumDeviationMm=std::max(result.maximumDeviationMm,panel.maximumMm);
                    result.panels.push_back(panel.pattern); result.gptPanels.push_back(std::move(panel));
                    result.panelOrigins.push_back({input.id,faceIndex});
                }
            }
            if (faceIndex==0) { return Out::Failure(base::MakeError("GPT-F007","対象に面がありません。","形状ガイドの面を選択してください。")); }
        }
        result.rmsDeviationMm=std::sqrt(squared/std::max(std::size_t(1),samples));
        std::ostringstream note;
        note<<"GPT近似 "<<result.panels.size()<<" 部材 / 標本最大 "<<result.maximumDeviationMm
            <<" mm / RMS "<<result.rmsDeviationMm<<" mm / 部材間の隙間 "<<result.maximumSeamGapMm<<" mm。";
        if (!result.reachedTolerance) { note<<"許容未達。部材数を増やすか最小幅・許容を変更してください。"; }
        result.summaryJa=note.str(); return Out::Success(std::move(result));
    } catch (const Standard_Failure&) {
        return Out::Failure(base::MakeError("GPT-F008","面の近似計算を完了できません。","元の面を確認してください。"));
    }
#else
    return Out::Failure(base::MakeError("GPT-F008","面を読むカーネルがありません。","Windows通常版を使用してください。"));
#endif
}
}
