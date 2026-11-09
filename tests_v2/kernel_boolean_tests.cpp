// 部品どうしの足す・引く・交差をカーネルで実際に行う層の試験(kernel/OcctBoolean)。
//
// 約束は 2 つ。何も変わらなかったら成功したことにしない。何も残らなかったら断る。
// 断るときの理由は演算ごとに違う(「触れていない」と一律に言わない)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctExtrude.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctThicken.h"
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Solid.hxx>
#include <TopLoc_Location.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Shape.hxx>
#include <BRepTools.hxx>
#include <gp_Pnt.hxx>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <chrono>
#include <iomanip>
#endif

#include <string>
#include <vector>
#include <algorithm>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::kernel::BooleanOperation;
using kachakacha::v2::kernel::BuildBoolean;
using kachakacha::v2::kernel::BuildExtrude;
using kachakacha::v2::modeling::AnalyzeExtrudeRequest;
using kachakacha::v2::modeling::ExtrudeDirectionMode;
using kachakacha::v2::modeling::ExtrudeExtentMode;
using kachakacha::v2::modeling::ExtrudeProfile;
using kachakacha::v2::modeling::ExtrudeRequest;
using kachakacha::v2::modeling::KernelShapeHandle;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

//! 箱(xy の矩形を z から高さぶん押し出す)。作れなければ空の番号。
[[maybe_unused]] [[nodiscard]] KernelShapeHandle Box(double x0, double y0, double x1, double y1, double z,
    double height)
{
    ExtrudeProfile profile;
    profile.closed = true;
    profile.segments = {
        Line({x0, y0, z}, {x1, y0, z}),
        Line({x1, y0, z}, {x1, y1, z}),
        Line({x1, y1, z}, {x0, y1, z}),
        Line({x0, y1, z}, {x0, y0, z}),
    };
    ExtrudeRequest request;
    request.profiles = {profile};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = height;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    if (!analysis.HasValue()) {
        return {};
    }
    const auto built = BuildExtrude(request, analysis.Value(), Tolerance(), {});
    if (!built.HasValue() || built.Value().parts.empty()) {
        return {};
    }
    return built.Value().parts.front().handle;
}

[[nodiscard]] std::string FirstCode(const std::vector<kachakacha::v2::base::Diagnostic>& found)
{
    return found.empty() ? std::string("(なし)") : found.front().code;
}

} // namespace

#ifndef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_boolean_absent, カーネルが無い版は足し引きせずに断る)
{
    const auto built = BuildBoolean(BooleanOperation::Intersection, {}, {}, 0.01);
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "KER-B005", "カーネル不在の診断コード");
}

#else

KACHA_V2_TEST(kernel_boolean, five_adjacent_trimmed_offset_plies_preserve_material)
{
    using namespace kachakacha::v2;
    const auto path=std::filesystem::path(KACHACAD_TEST_FIXTURE_DIR)/"trimmed-curved-ply-source.kshape";
    std::ifstream file(path,std::ios::binary);
    Require(file.good(),"trimmed curved source fixture is available");
    const std::string bytes(std::istreambuf_iterator<char>(file),{});
    const auto source=kernel::RestoreOutputShape(bytes);Require(source.HasValue(),source.FirstSummaryJa());
    const auto before=kernel::CaptureOutputShape(source.Value()).Value();
    std::vector<KernelShapeHandle> plies;
    std::vector<std::string> original;
    for(int layer=0;layer<5;++layer) {
        modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::OffsetGuide;
        request.offsetDistanceMm=-.05-.1*layer;
        modeling::GuideChain chain;chain.role=modeling::ChainRole::SourceSurface;request.chains.push_back(chain);
        const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,Tolerance());
        Require(analysis.HasValue(),analysis.FirstSummaryJa());
        const auto neutral=kernel::BuildGuideSurface(request,analysis.Value(),Tolerance(),source.Value());
        Require(neutral.HasValue(),neutral.FirstSummaryJa());
        const auto stock=kernel::ThickenSurface(neutral.Value().handle,.1,fabrication::ThicknessPlacement::Centered,Tolerance());
        Require(stock.HasValue(),stock.FirstSummaryJa());
        kernel::ReleaseShape(neutral.Value().handle);
        plies.push_back(stock.Value().handle);original.push_back(kernel::CaptureOutputShape(plies.back()).Value());
    }
    for(int placement=0;placement<4;++placement) {
        const auto started=std::chrono::steady_clock::now();
        std::vector<KernelShapeHandle> inputs;
        gp_Trsf transform;
        if(placement>=2)transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.53);
        if(placement==3)transform.SetTranslationPart(gp_Vec(1000,-2000,3000));
        for(const auto handle:plies) {
            TopoDS_Shape native;Require(kernel::LookupShape(handle,native),"source ply exists");
            inputs.push_back(kernel::StoreShape(BRepBuilderAPI_Transform(native,transform,true).Shape()));
        }
        if(placement%2)std::reverse(inputs.begin(),inputs.end());
        const auto result=kernel::BuildBooleans(BooleanOperation::Union,inputs.front(),
            std::vector(inputs.begin()+1,inputs.end()),Tolerance().modelLinearMm);
        Require(result.HasValue(),"placed five-ply union: "+std::to_string(placement)+" "+result.FirstSummaryJa());
        const auto components=kernel::BooleanSolidComponents(result.Value().handle);
        Require(components.HasValue()&&components.Value().size()==1,"five touching thin plies form one solid");
        for(const auto handle:components.Value())kernel::ReleaseShape(handle);
        Require(result.Value().volumeMm3>220&&result.Value().volumeMm3<222,"no thin ply is lost after order or rigid motion changes");
        if(placement==0)Require(kernel::OutputStep({result.Value().handle}).HasValue(),"thin curved union exports STEP");
        kernel::ReleaseShape(result.Value().handle);
        for(const auto handle:inputs)kernel::ReleaseShape(handle);
        std::cout<<"trimmed ply placement "<<placement<<" verified in "
            <<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<" seconds\n"<<std::flush;
    }
    for(std::size_t i=0;i<plies.size();++i){
        RequireEqual(kernel::CaptureOutputShape(plies[i]).Value(),original[i],"original ply geometry is unchanged");
        kernel::ReleaseShape(plies[i]);
    }
    RequireEqual(kernel::CaptureOutputShape(source.Value()).Value(),before,"original trim and support are unchanged");
    kernel::ReleaseShape(source.Value());
}

namespace {
// Preserve the material orientation while representing its solid and shells
// with reversed relative orientations inside otherwise empty containers.
// This is the representation produced by some legitimate mirrored/trimmed parts.
KernelShapeHandle WrappedSolid(KernelShapeHandle handle,int depth)
{
    using namespace kachakacha::v2;
    TopoDS_Shape shape;Require(kernel::LookupShape(handle,shape),"source exists");
    TopExp_Explorer solids(shape,TopAbs_SOLID);Require(solids.More(),"one source solid");
    const auto source=solids.Current();solids.Next();Require(!solids.More(),"exactly one source solid");
    BRep_Builder builder;TopoDS_Solid reversed;builder.MakeSolid(reversed);
    for(TopExp_Explorer shells(source,TopAbs_SHELL);shells.More();shells.Next())
        builder.Add(reversed,shells.Current().Reversed());
    reversed.Reverse();shape=reversed;
    for(int i=0;i<depth;++i){TopoDS_Compound parent;builder.MakeCompound(parent);builder.Add(parent,shape);shape=parent;}
    return kernel::StoreShape(shape);
}

void RequireMaterialAt(KernelShapeHandle handle,Vector3 point,TopAbs_State wanted)
{
    TopoDS_Shape shape;Require(kachakacha::v2::kernel::LookupShape(handle,shape),"solid exists");
    BRepClass3d_SolidClassifier classifier(shape,gp_Pnt(point.x,point.y,point.z),1e-7);
    Require(classifier.State()==wanted,"material classification at the specified point");
}
}

KACHA_V2_TEST(kernel_boolean, pure_single_solid_containers_preserve_all_boolean_material)
{
    using namespace kachakacha::v2;
    const auto a=Box(0,0,10,20,0,1),b=Box(9,5,11,10,0,1),c=Box(10,5,12,10,0,1);
    const auto wrappedA=WrappedSolid(a,3),wrappedB=WrappedSolid(b,1),wrappedC=WrappedSolid(c,2);
    const auto aa=kernel::CaptureOutputShape(wrappedA),bb=kernel::CaptureOutputShape(wrappedB);
    Require(aa.HasValue()&&bb.HasValue(),"wrapped native operands can be captured");
    RequireNear(kernel::ShapeVolume(wrappedA).Value(),200,1e-6,"containers do not change source material");
    for(const bool reverse:{false,true})for(const auto operation:{BooleanOperation::Union,BooleanOperation::Difference,BooleanOperation::Intersection}){
        const auto result=BuildBoolean(operation,reverse?wrappedB:wrappedA,reverse?wrappedA:wrappedB,1e-6);
        Require(result.HasValue(),result.FirstSummaryJa());
        const double expected=operation==BooleanOperation::Union?205:operation==BooleanOperation::Intersection?5:reverse?5:195;
        RequireNear(result.Value().volumeMm3,expected,1e-6,"Boolean must retain the actual solid, independent of its container");
        kernel::ReleaseShape(result.Value().handle);
    }
    const auto batch=kernel::BuildBooleans(BooleanOperation::Union,wrappedA,{wrappedB,wrappedC},1e-6);
    Require(batch.HasValue(),batch.FirstSummaryJa());RequireNear(batch.Value().volumeMm3,210,1e-6,"all wrapped tools contribute to one connected result");
    kernel::ReleaseShape(batch.Value().handle);
    RequireEqual(kernel::CaptureOutputShape(wrappedA).Value(),aa.Value(),"source container, locations, and orientations are not rewritten");
    RequireEqual(kernel::CaptureOutputShape(wrappedB).Value(),bb.Value(),"tool container remains unchanged");
    for(const auto handle:{a,b,c,wrappedA,wrappedB,wrappedC})kernel::ReleaseShape(handle);
}

KACHA_V2_TEST(kernel_boolean, wrapped_perforated_panel_union_preserves_holes_after_rigid_motion)
{
    using namespace kachakacha::v2;
    const auto outer=Box(0,0,20,20,0,1),opening=Box(5,5,15,15,-1,3);
    const auto panel=BuildBoolean(BooleanOperation::Difference,outer,opening,1e-6);Require(panel.HasValue(),panel.FirstSummaryJa());
    const auto rimOuter=Box(4.9,4.9,15.1,15.1,0,1),rimOpening=Box(5.25,5.25,14.75,14.75,-1,3);
    const auto rim=BuildBoolean(BooleanOperation::Difference,rimOuter,rimOpening,1e-6);Require(rim.HasValue(),rim.FirstSummaryJa());
    const auto wrappedPanel=WrappedSolid(panel.Value().handle,2),wrappedRim=WrappedSolid(rim.Value().handle,1);
    const auto panelBytes=kernel::CaptureOutputShape(wrappedPanel).Value(),rimBytes=kernel::CaptureOutputShape(wrappedRim).Value();
    for(const int placement:{0,1,2,3}){
        const bool moved=placement!=0;
        gp_Trsf transform;
        if(placement==1||placement==3){transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.53);transform.SetTranslationPart(gp_Vec(12,-7,9));}
        else if(placement==2)transform.SetMirror(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(1,0,0)));
        const auto transformed=[&](KernelShapeHandle handle){
            TopoDS_Shape shape;Require(kernel::LookupShape(handle,shape),"source exists");
            return kernel::StoreShape(placement==3?shape.Moved(TopLoc_Location(transform))
                :BRepBuilderAPI_Transform(shape,transform,true).Shape());
        };
        const auto a=transformed(wrappedPanel),b=transformed(wrappedRim);
        const auto result=BuildBoolean(BooleanOperation::Union,moved?b:a,moved?a:b,1e-6);
        Require(result.HasValue(),result.FirstSummaryJa());RequireNear(result.Value().volumeMm3,309.75,1e-5,"outer panel is preserved and its opening becomes 9.5 by 9.5");
        for(const auto sample:std::vector<std::pair<Vector3,TopAbs_State>>{{{2,2,.5},TopAbs_IN},{{5.1,10,.5},TopAbs_IN},{{10,10,.5},TopAbs_OUT},{{30,30,.5},TopAbs_OUT}}){
            gp_Pnt point(sample.first.x,sample.first.y,sample.first.z);point.Transform(transform);
            RequireMaterialAt(result.Value().handle,{point.X(),point.Y(),point.Z()},sample.second);
        }
        const auto step=kernel::OutputStep({result.Value().handle});Require(step.HasValue()&&!step.Value().empty(),"perforated union exports STEP");
        for(const auto handle:{a,b,result.Value().handle})kernel::ReleaseShape(handle);
    }
    RequireEqual(kernel::CaptureOutputShape(wrappedPanel).Value(),panelBytes,"original perforated panel and container are unchanged");
    RequireEqual(kernel::CaptureOutputShape(wrappedRim).Value(),rimBytes,"original rim is unchanged");
    for(const auto handle:{outer,opening,panel.Value().handle,rimOuter,rimOpening,rim.Value().handle,wrappedPanel,wrappedRim})kernel::ReleaseShape(handle);
}

KACHA_V2_TEST(kernel_boolean, disconnected_multi_solid_containers_are_not_reduced_to_one_child)
{
    using namespace kachakacha::v2;
    const auto near=Box(0,0,10,10,0,1),far=Box(30,0,40,10,0,1),tool=Box(9,0,11,10,0,1);
    BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
    for(const auto handle:{near,far}){TopoDS_Shape shape;Require(kernel::LookupShape(handle,shape),"source exists");builder.Add(compound,shape);}
    const auto container=kernel::StoreShape(compound);const auto bytes=kernel::CaptureOutputShape(container).Value();
    const auto result=BuildBoolean(BooleanOperation::Union,container,tool,1e-6);
    Require(!result.HasValue(),"normalization must not discard the distant solid");
    RequireEqual(kernel::CaptureOutputShape(container).Value(),bytes,"refused multi-solid input is not rewritten");
    for(const auto handle:{near,far,tool,container})kernel::ReleaseShape(handle);
}

KACHA_V2_TEST(kernel_boolean, disconnected_addition_is_refused_without_leaking_or_touching_inputs)
{
    using namespace kachakacha::v2;
    const auto a=Box(0,0,10,10,0,10),apart=Box(20,0,30,10,0,10),overlap=Box(5,0,15,10,0,10);
    const auto before=kernel::CachedShapeCount();
    const auto separated=BuildBoolean(BooleanOperation::Union,a,apart,1e-6);
    Require(!separated.HasValue(),"one part cannot contain two disconnected solids");
    const auto batch=kernel::BuildBooleans(BooleanOperation::Union,a,{overlap,apart},1e-6);
    Require(!batch.HasValue(),"batch addition also requires one connected solid");
    Require(kernel::CachedShapeCount()==before,"refused previews release intermediate shapes");
    const auto retry=BuildBoolean(BooleanOperation::Union,a,overlap,1e-6);
    Require(retry.HasValue(),retry.FirstSummaryJa());
    kernel::ReleaseShape(retry.Value().handle);kernel::ReleaseShape(a);kernel::ReleaseShape(apart);kernel::ReleaseShape(overlap);
}

KACHA_V2_TEST(kernel_boolean, redundant_cut_and_common_tools_do_not_cancel_a_useful_batch)
{
    using namespace kachakacha::v2;
    const auto a=Box(0,0,10,10,0,10),apart=Box(20,0,30,10,0,10),cut=Box(5,-1,11,11,-1,12),around=Box(-1,-1,11,11,-1,12);
    for(const bool reverse:{false,true}) {
        const auto difference=kernel::BuildBooleans(BooleanOperation::Difference,a,reverse?std::vector{cut,apart}:std::vector{apart,cut},1e-6);
        Require(difference.HasValue(),difference.FirstSummaryJa());RequireNear(difference.Value().volumeMm3,500,1e-6,"one useful cutter removes half");
        const auto common=kernel::BuildBooleans(BooleanOperation::Intersection,a,reverse?std::vector{cut,around}:std::vector{around,cut},1e-6);
        Require(common.HasValue(),common.FirstSummaryJa());RequireNear(common.Value().volumeMm3,500,1e-6,"one enclosing tool does not cancel useful common");
        kernel::ReleaseShape(difference.Value().handle);kernel::ReleaseShape(common.Value().handle);
    }
    const auto unchanged=kernel::BuildBooleans(BooleanOperation::Difference,a,{apart,apart},1e-6);
    Require(!unchanged.HasValue()&&FirstCode(unchanged.Diagnostics())=="KER-B003","whole no-change still explained");
    kernel::ReleaseShape(a);kernel::ReleaseShape(apart);kernel::ReleaseShape(cut);kernel::ReleaseShape(around);
}

KACHA_V2_TEST(kernel_boolean, separating_cut_produces_stable_individual_solids)
{
    using namespace kachakacha::v2;
    const auto a=Box(0,0,10,10,0,10),tool=Box(4,-1,6,11,-1,12);
    const auto cut=BuildBoolean(BooleanOperation::Difference,a,tool,1e-6);Require(cut.HasValue(),cut.FirstSummaryJa());
    const auto parts=kernel::BooleanSolidComponents(cut.Value().handle);Require(parts.HasValue()&&parts.Value().size()==2,"two separate part results");
    for(const auto part:parts.Value()) {
        const auto volume=kernel::ShapeVolume(part);Require(volume.HasValue(),"each solid can be measured");
        RequireNear(volume.Value(),400,1e-6,"each remaining half is four hundred");kernel::ReleaseShape(part);
    }
    kernel::ReleaseShape(cut.Value().handle);kernel::ReleaseShape(a);kernel::ReleaseShape(tool);
}

KACHA_V2_TEST(kernel_boolean, batch_union_matches_ordered_union_and_preserves_inputs)
{
    using kachakacha::v2::kernel::BuildBooleans;
    const auto a = Box(0, 0, 10, 10, 0, 10);
    const auto b = Box(5, 0, 15, 10, 0, 10);
    const auto c = Box(10, 0, 20, 10, 0, 10);
    const auto serialized = [](KernelShapeHandle handle) {
        TopoDS_Shape shape;
        Require(kachakacha::v2::kernel::LookupShape(handle, shape), "input exists");
        std::ostringstream stream;
        BRepTools::Write(shape, stream);
        return stream.str();
    };
    const auto aa = serialized(a), bb = serialized(b), cc = serialized(c);
    const auto batch = BuildBooleans(BooleanOperation::Union, a, {b, c}, 1e-6);
    Require(batch.HasValue(), batch.FirstSummaryJa());
    const auto reversed = BuildBooleans(BooleanOperation::Union, c, {b, a}, 1e-6);
    Require(reversed.HasValue(), reversed.FirstSummaryJa());
    RequireNear(batch.Value().volumeMm3, 2000, 1e-6, "exact union volume");
    RequireNear(reversed.Value().volumeMm3, batch.Value().volumeMm3, 1e-6, "input order invariant");
    RequireNear(batch.Value().previousVolumeMm3, 1000, 1e-6, "original base volume");
    Require(serialized(a) == aa && serialized(b) == bb && serialized(c) == cc,
        "batch union preserves all source BREP geometry and topology");
    const auto ab = BuildBoolean(BooleanOperation::Union, a, b, 1e-6);
    Require(ab.HasValue(), ab.FirstSummaryJa());
    const auto sequential = BuildBoolean(BooleanOperation::Union, ab.Value().handle, c, 1e-6);
    Require(sequential.HasValue(), sequential.FirstSummaryJa());
    RequireNear(batch.Value().volumeMm3, sequential.Value().volumeMm3, 1e-6, "same result as ordered union");
}

KACHA_V2_TEST(kernel_boolean, binary_operations_preserve_source_geometry)
{
    for(const auto operation:{BooleanOperation::Union,BooleanOperation::Difference,BooleanOperation::Intersection}) {
        const auto a=Box(0,0,10,10,0,10),b=Box(5,0,15,10,0,10);
        const auto serialized=[](KernelShapeHandle handle) {
            TopoDS_Shape shape;Require(kachakacha::v2::kernel::LookupShape(handle,shape),"input exists");
            std::ostringstream stream;BRepTools::Write(shape,stream);return stream.str();
        };
        const auto beforeA=serialized(a),beforeB=serialized(b);
        const auto result=BuildBoolean(operation,a,b,1e-6);
        Require(result.HasValue(),result.FirstSummaryJa());
        Require(serialized(a)==beforeA&&serialized(b)==beforeB,"preview operation leaves original BRep unchanged");
        RequireNear(result.Value().volumeMm3,operation==BooleanOperation::Union?1500:500,1e-6,"independent analytic volume");
        kachakacha::v2::kernel::ReleaseShape(result.Value().handle);
        kachakacha::v2::kernel::ReleaseShape(a);kachakacha::v2::kernel::ReleaseShape(b);
    }
}

KACHA_V2_TEST(kernel_boolean, batch_union_redundant_tools_and_missing_inputs)
{
    using kachakacha::v2::kernel::BuildBooleans;
    const auto a = Box(0, 0, 10, 10, 0, 10);
    const auto inside = Box(2, 2, 4, 4, 2, 2);
    const auto b = Box(5, 0, 15, 10, 0, 10);
    const auto useful = BuildBooleans(BooleanOperation::Union, a, {inside, b}, 1e-6);
    Require(useful.HasValue(), useful.FirstSummaryJa());
    RequireNear(useful.Value().volumeMm3, 1500, 1e-6, "contained tool does not reject useful batch");
    const auto unchanged = BuildBooleans(BooleanOperation::Union, a, {inside, inside}, 1e-6);
    Require(!unchanged.HasValue() && FirstCode(unchanged.Diagnostics()) == "KER-B003", "whole no-change remains rejected");
    const auto missing = BuildBooleans(BooleanOperation::Union, a, {b, {}}, 1e-6);
    Require(!missing.HasValue() && FirstCode(missing.Diagnostics()) == "KER-B002", "missing input rejects entire batch");
}

KACHA_V2_TEST(kernel_boolean, inward_input_is_rejected_instead_of_hidden_by_absolute_volume)
{
    const auto a = Box(0, 0, 10, 10, 0, 10);
    const auto b = Box(5, 0, 15, 10, 0, 10);
    TopoDS_Shape reversed;
    Require(kachakacha::v2::kernel::LookupShape(a, reversed), "input available");
    reversed.Reverse();
    const auto bad = kachakacha::v2::kernel::StoreShape(reversed);
    const auto result = BuildBoolean(BooleanOperation::Union, bad, b, 1e-6);
    Require(!result.HasValue() && FirstCode(result.Diagnostics()) == "KER-B001",
        "an inverted solid must never produce a successful union");
    kachakacha::v2::kernel::ReleaseShape(a);
    kachakacha::v2::kernel::ReleaseShape(b);
    kachakacha::v2::kernel::ReleaseShape(bad);
}

namespace {
void DenseCommonDisagreement(bool reverse)
{
    namespace kernel=kachakacha::v2::kernel;
    const auto load=[](const char* suffix) {
        const auto path=std::filesystem::path(KACHACAD_TEST_FIXTURE_DIR)
            /(std::string("dense-curved-overlap-")+suffix+".kshape");
        std::ifstream file(path,std::ios::binary);
        Require(file.good(),"native curved fixture is available");
        const std::string bytes(std::istreambuf_iterator<char>(file),{});
        const auto restored=kernel::RestoreOutputShape(bytes);
        Require(restored.HasValue(),restored.FirstSummaryJa());
        return restored.Value();
    };
    const auto left=load("left"),right=load("right");
    const auto beforeLeft=kernel::CaptureOutputShape(left),beforeRight=kernel::CaptureOutputShape(right);
    Require(beforeLeft.HasValue()&&beforeRight.HasValue(),"input bytes captured");
    for(const auto handle:{left,right}) {
        TopoDS_Shape native;Require(kernel::LookupShape(handle,native),"native input exists");
        Require(BRepClass3d_SolidClassifier(native,gp_Pnt(0,-5,32.4),1e-6).State()==TopAbs_IN,
            "independent point lies strictly in both solids");
    }
    const auto result=BuildBoolean(BooleanOperation::Intersection,reverse?right:left,reverse?left:right,1e-6);
    Require(!result.HasValue()&&FirstCode(result.Diagnostics())==kernel::kBooleanFailed,
        "contradictory empty native result must not be described as disjoint: "+result.FirstSummaryJa());
    RequireEqual(kernel::CaptureOutputShape(left).Value(),beforeLeft.Value(),"left source bytes preserved");
    RequireEqual(kernel::CaptureOutputShape(right).Value(),beforeRight.Value(),"right source bytes preserved");
    kernel::ReleaseShape(left);kernel::ReleaseShape(right);
}
}

KACHA_V2_TEST(kernel_boolean, dense_curved_empty_common_contradicts_overlap_left_first)
{
    DenseCommonDisagreement(false);
}
KACHA_V2_TEST(kernel_boolean, dense_curved_empty_common_contradicts_overlap_right_first)
{
    DenseCommonDisagreement(true);
}

namespace {
void DenseAdjacentUnion(bool rotated)
{
    namespace kernel=kachakacha::v2::kernel;
    const auto load=[&](const char* side) {
        const auto path=std::filesystem::path(KACHACAD_TEST_FIXTURE_DIR)
            /(std::string("dense-curved-adjacent-")+side+".kshape");
        std::ifstream file(path,std::ios::binary);
        Require(file.good(),"native adjacent curved fixture is available");
        const std::string bytes(std::istreambuf_iterator<char>(file),{});
        const auto restored=kernel::RestoreOutputShape(bytes);
        Require(restored.HasValue(),restored.FirstSummaryJa());
        if(!rotated)return restored.Value();
        TopoDS_Shape source;Require(kernel::LookupShape(restored.Value(),source),"native source exists");
        gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.7);
        placement.SetTranslationPart(gp_Vec(11,-7,5));
        const auto placed=kernel::StoreShape(BRepBuilderAPI_Transform(source,placement,true).Shape());
        kernel::ReleaseShape(restored.Value());return placed;
    };
    const auto a=load("right"),b=load("left");
    const auto aa=kernel::CaptureOutputShape(a),bb=kernel::CaptureOutputShape(b);
    Require(aa.HasValue()&&bb.HasValue(),"input geometry captured");
    const auto started=std::chrono::steady_clock::now();
    const auto result=BuildBoolean(BooleanOperation::Union,rotated?b:a,rotated?a:b,1e-6);
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    Require(result.HasValue(),result.FirstSummaryJa());
    Require(seconds<150,"dense curved addition finishes within the unchanged 180-second UI watchdog");
    // Reference volume from exact native trim integration of the retained skin;
    // no mesh, bounding-box overlap or absolute-volume substitute is accepted.
    std::ostringstream massMessage;massMessage<<std::setprecision(17)
        <<"placement and argument order preserve material volume; actual="<<result.Value().volumeMm3
        <<" delta="<<result.Value().volumeMm3-3991.8371234911001;
    // Rotating and reversing the native Boolean solve is not a byte-identical
    // operation. Compare two independently resolved/integrated skins within
    // twice the existing 1e-8 relative analysis-volume budget. The production
    // validity, precision and per-candidate volume limits are unchanged.
    const double referenceVolume=3991.8371234911001;
    RequireNear(result.Value().volumeMm3,referenceVolume,rotated?2e-8*referenceVolume:1e-5,massMessage.str());
    if(!rotated) {
        TopoDS_Shape native;Require(kernel::LookupShape(result.Value().handle,native),"result exists");
        int faces=0;for(TopExp_Explorer e(native,TopAbs_FACE);e.More();e.Next())++faces;
        Require(faces==7,"analysis partitioning never replaces the seven-face stored result");
    }
    const auto solids=kernel::BooleanSolidComponents(result.Value().handle);
    Require(solids.HasValue()&&solids.Value().size()==1,"addition is one connected solid");
    for(const auto handle:solids.Value())kernel::ReleaseShape(handle);
    RequireEqual(kernel::CaptureOutputShape(a).Value(),aa.Value(),"first source topology and curves preserved");
    RequireEqual(kernel::CaptureOutputShape(b).Value(),bb.Value(),"second source topology and curves preserved");
    const auto step=kernel::OutputStep({result.Value().handle});
    Require(step.HasValue()&&!step.Value().empty(),"validated dense curved result exports STEP");
    kernel::ReleaseShape(result.Value().handle);kernel::ReleaseShape(a);kernel::ReleaseShape(b);
}
}

KACHA_V2_TEST(kernel_boolean, dense_curved_adjacent_addition_preserves_the_source_skin)
{
    DenseAdjacentUnion(false);
}
KACHA_V2_TEST(kernel_boolean, rotated_dense_curved_adjacent_addition_reverses_argument_order)
{
    DenseAdjacentUnion(true);
}

KACHA_V2_TEST(kernel_boolean, touching_faces_do_not_become_a_positive_overlap_witness)
{
    namespace kernel=kachakacha::v2::kernel;
    const auto a=Box(0,0,10,10,0,10),b=Box(10,0,20,10,0,10);
    const auto result=BuildBoolean(BooleanOperation::Intersection,a,b,1e-6);
    Require(!result.HasValue()&&FirstCode(result.Diagnostics())==kernel::kBooleanNothingLeft,
        "face contact alone has no common solid volume");
    kernel::ReleaseShape(a);kernel::ReleaseShape(b);
}

KACHA_V2_TEST(kernel_boolean, overlapping_bounds_with_material_inside_a_hole_remain_disjoint)
{
    namespace kernel=kachakacha::v2::kernel;
    const auto outer=Box(0,0,10,10,0,10),cutter=Box(2,2,8,8,-1,12);
    const auto ring=BuildBoolean(BooleanOperation::Difference,outer,cutter,1e-6);
    Require(ring.HasValue(),ring.FirstSummaryJa());
    const auto insideHole=Box(3,3,7,7,1,8);
    const auto result=BuildBoolean(BooleanOperation::Intersection,ring.Value().handle,insideHole,1e-6);
    Require(!result.HasValue()&&FirstCode(result.Diagnostics())==kernel::kBooleanNothingLeft,
        "overlapping bounding boxes do not establish overlapping material");
    for(const auto handle:{outer,cutter,ring.Value().handle,insideHole})kernel::ReleaseShape(handle);
}

KACHA_V2_TEST(kernel_boolean, 重なる2つの箱を足す引く交差すると体積が合う)
{
    // A: 40×20×30 = 24000、B: x を 20 ずらした同じ箱。重なりは 20×20×30 = 12000。
    const auto a = Box(0, 0, 40, 20, 0, 30);
    const auto b = Box(20, 0, 60, 20, 0, 30);
    Require(a.Valid() && b.Valid(), "箱が作れること");
    const auto sum = BuildBoolean(BooleanOperation::Union, a, b, 0.01);
    Require(sum.HasValue(), "足せる: " + sum.FirstSummaryJa());
    RequireNear(sum.Value().volumeMm3, 36000.0, 1.0e-3, "足すと 36000");
    RequireNear(sum.Value().previousVolumeMm3, 24000.0, 1.0e-3, "前の体積は土台の 24000");
    const auto rest = BuildBoolean(BooleanOperation::Difference, a, b, 0.01);
    Require(rest.HasValue(), "引ける: " + rest.FirstSummaryJa());
    RequireNear(rest.Value().volumeMm3, 12000.0, 1.0e-3, "引くと 12000");
    const auto common = BuildBoolean(BooleanOperation::Intersection, a, b, 0.01);
    Require(common.HasValue(), "交差できる: " + common.FirstSummaryJa());
    RequireNear(common.Value().volumeMm3, 12000.0, 1.0e-3, "交差は重なりの 12000");
    Require(common.Value().handle.Valid(), "出来た形の番号がある");
}

KACHA_V2_TEST(kernel_boolean, 何も変わらないか何も残らないときは演算ごとの理由で断る)
{
    const auto big = Box(0, 0, 40, 40, 0, 40);
    const auto inside = Box(10, 10, 20, 20, 10, 10);
    const auto apart = Box(100, 0, 120, 20, 0, 20);
    const auto around = Box(-10, -10, 50, 50, -10, 60);
    Require(big.Valid() && inside.Valid() && apart.Valid() && around.Valid(), "箱が作れること");
    // 足す: 相手が土台の中なら何も変わらない。
    const auto union0 = BuildBoolean(BooleanOperation::Union, big, inside, 0.01);
    Require(!union0.HasValue() && FirstCode(union0.Diagnostics()) == "KER-B003"
            && union0.FirstSummaryJa().find("収まって") != std::string::npos,
        "足す: 土台の中に収まっていると言う: " + union0.FirstSummaryJa());
    // 引く: 重ならなければ何も変わらない。
    const auto cut0 = BuildBoolean(BooleanOperation::Difference, big, apart, 0.01);
    Require(!cut0.HasValue() && FirstCode(cut0.Diagnostics()) == "KER-B003"
            && cut0.FirstSummaryJa().find("重なっていない") != std::string::npos,
        "引く: 重なっていないと言う: " + cut0.FirstSummaryJa());
    // 交差: 重ならなければ何も残らない。相手が土台を全部含めば土台のまま。
    const auto common0 = BuildBoolean(BooleanOperation::Intersection, big, apart, 0.01);
    Require(!common0.HasValue() && FirstCode(common0.Diagnostics()) == "KER-B004"
            && common0.FirstSummaryJa().find("共通部分がありません") != std::string::npos,
        "交差: 共通部分が無いと言う: " + common0.FirstSummaryJa());
    const auto common1 = BuildBoolean(BooleanOperation::Intersection, big, around, 0.01);
    Require(!common1.HasValue() && FirstCode(common1.Diagnostics()) == "KER-B003",
        "交差: 土台のままなら何も変わらないと言う: " + common1.FirstSummaryJa());
    // 引き切れば何も残らない。
    const auto cut1 = BuildBoolean(BooleanOperation::Difference, big, around, 0.01);
    Require(!cut1.HasValue() && FirstCode(cut1.Diagnostics()) == "KER-B004",
        "引き切れば断る: " + cut1.FirstSummaryJa());
}

#endif

KACHA_V2_TEST_MAIN("kernel_boolean_tests")
