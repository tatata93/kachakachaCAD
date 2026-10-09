#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Dimension.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/io/DocumentFile.h"
#include <cmath>
using namespace kachakacha::v2;
using test::Require;
using test::RequireNear;
namespace {
struct Fixture {
    base::DeterministicIdGenerator ids{976};
    document::Document doc{ids.NextTyped<base::IdKind::Document>()};
    domain::SegmentRef Add(geometry::CurveSegment curve) {
        domain::Feature feature;feature.id=ids.NextTyped<base::IdKind::Feature>();feature.type=domain::FeatureType::CreateWire;
        domain::Entity entity;entity.id=ids.NextTyped<base::IdKind::Entity>();entity.createdBy=feature.id;entity.kind=domain::EntityKind::Wire;
        const auto segment=ids.NextTyped<base::IdKind::Segment>();
        domain::CreateWireDefinition wire;wire.segments={curve};wire.segmentIds={segment};feature.definition=wire;
        feature.outputs.push_back({"wire",entity.id,domain::EntityKind::Wire});
        Require(doc.Run(document::AddFeatureCommand(feature,{entity},"wire")).committed,"wire added");
        return {entity.id,segment};
    }
    domain::SegmentRef Line(geometry::Vector3 a,geometry::Vector3 b){return Add(geometry::CurveSegment::MakeLine(a,b).Value());}
    document::ReferenceDimension Dim(std::string kind,std::vector<domain::SegmentRef> refs,double value,bool driving=true) {
        document::ReferenceDimension d;d.id=ids.NextTyped<base::IdKind::Dimension>();d.kind=kind;d.label="寸法";d.segments=refs;
        for(const auto& ref:refs)d.targets.push_back(ref.entityId);
        d.recordedValue=value;d.driving=driving;d.unit=kind=="dim_angle" ? "rad" : "mm";d.labelPosition=geometry::Vector3{2,3,4};return d;
    }
    void Set(const document::ReferenceDimension& d) {
        const auto result=doc.Run(document::SetDimensionCommand(d));
        std::string error;for(const auto& problem:result.diagnostics)error+=problem.summaryJa;
        Require(result.committed,"dimension committed: "+error);
    }
    double Value(const document::ReferenceDimension& d){const auto value=document::EvaluateDimension(doc.Snapshot(),d);Require(value.HasValue(),"evaluated");return value.Value().value;}
};
}
KACHA_V2_TEST(dimension, drives_length_and_preserves_undo)
{
    Fixture f;const auto line=f.Line({0,0,0},{10,0,0});const auto dim=f.Dim("dim_length",{line},20);
    f.Set(dim);RequireNear(f.Value(dim),20,1e-5,"driven length");
    Require(f.doc.Undo(),"undo");RequireNear(f.Value(dim),10,1e-8,"undo geometry and constraint");
    Require(f.doc.Redo(),"redo");RequireNear(f.Value(dim),20,1e-5,"redo");
}
KACHA_V2_TEST(dimension, rectangle_connections_and_existing_height_survive_width_edit)
{
    Fixture f;const auto a=f.Line({0,0,0},{10,0,0});const auto b=f.Line({10,0,0},{10,5,0});
    const auto c=f.Line({10,5,0},{0,5,0});const auto d=f.Line({0,5,0},{0,0,0});
    auto height=f.Dim("dim_length",{b},5);f.Set(height);
    auto width=f.Dim("dim_length",{a},20);f.Set(width);
    RequireNear(f.Value(height),5,1e-5,"existing height preserved");
    RequireNear(f.Value(f.Dim("dim_length",{c},0,false)),20,1e-5,"opposite side follows");
    RequireNear(f.Value(f.Dim("dim_length",{d},0,false)),5,1e-5,"connected side follows");
    width.recordedValue=25;f.Set(width);RequireNear(f.Value(width),25,1e-5,"edit existing");
}
KACHA_V2_TEST(dimension, contradictory_constraints_do_not_partially_change_document)
{
    Fixture f;const auto line=f.Line({0,0,0},{10,0,0});const auto first=f.Dim("dim_length",{line},20);f.Set(first);
    const auto revision=f.doc.Revision();const auto conflict=f.Dim("dim_length",{line},30);
    Require(!f.doc.Run(document::SetDimensionCommand(conflict)).committed,"conflicting value rejected");
    Require(f.doc.Revision()==revision,"no partial edit");RequireNear(f.Value(first),20,1e-5,"old value intact");
    auto reference=conflict;reference.driving=false;f.Set(reference);
    RequireNear(f.Value(reference),20,1e-5,"reference does not drive");
}
KACHA_V2_TEST(dimension, radius_diameter_angle_and_projected_lengths)
{
    Fixture f;const auto circle=f.Add(geometry::CurveSegment::MakeCircle({0,0,0},{0,0,1},{1,0,0},4).Value());
    auto diameter=f.Dim("dim_diameter",{circle},12);f.Set(diameter);RequireNear(f.Value(diameter),12,1e-5,"diameter");
    auto radius=f.Dim("dim_radius",{circle},6,false);f.Set(radius);RequireNear(f.Value(radius),6,1e-5,"radius");
    Fixture a;const auto first=a.Line({0,0,0},{10,0,0});const auto second=a.Line({0,0,0},{0,10,0});
    auto angle=a.Dim("dim_angle",{first,second},3.141592653589793/3);a.Set(angle);RequireNear(a.Value(angle),angle.recordedValue,1e-6,"angle");
    Fixture b;const auto diagonal=b.Line({0,0,0},{3,4,5});auto horizontal=b.Dim("dim_horizontal",{diagonal},6);b.Set(horizontal);
    RequireNear(b.Value(horizontal),6,1e-5,"horizontal dimension");
}
KACHA_V2_TEST(dimension, save_reload_retains_constraints_and_placement)
{
    Fixture f;auto dim=f.Dim("dim_length",{f.Line({0,0,0},{10,0,0})},15);f.Set(dim);
    io::DocumentFile file;file.snapshot=f.doc.Snapshot();const auto saved=io::SaveDocument(file);Require(saved.HasValue(),"save");
    const auto loaded=io::LoadDocument(saved.Value());Require(loaded.HasValue(),"load: "+loaded.FirstSummaryJa());
    const auto& restored=loaded.Value().snapshot.referenceDimensions.front();
    Require(restored.driving && restored.segments.size()==1 && restored.labelPosition==dim.labelPosition,"roundtrip fields");
    RequireNear(document::EvaluateDimension(loaded.Value().snapshot,restored).Value().value,15,1e-5,"restored value");
}
KACHA_V2_TEST(dimension, spatial_length_and_arc_connections)
{
    Fixture f;auto line=f.Line({1,2,3},{4,6,15});auto length=f.Dim("dim_length",{line},26);f.Set(length);
    RequireNear(f.Value(length),26,1e-5,"3D length");
    const auto anchors=document::EvaluateDimension(f.doc.Snapshot(),length).Value().anchors;
    RequireNear(geometry::Distance(anchors[0],{1,2,3}),0,1e-5,"first point fixed");
    Fixture a;const auto arc=a.Add(geometry::CurveSegment::MakeCircularArc({0,0,0},{0,0,1},{1,0,0},5,0,1.5707963267948966).Value());
    const auto attached=a.Line({5,0,0},{10,0,0});auto radius=a.Dim("dim_radius",{arc},7);a.Set(radius);
    const auto lineEnds=document::EvaluateDimension(a.doc.Snapshot(),a.Dim("dim_length",{attached},0,false)).Value().anchors;
    RequireNear(geometry::Distance(lineEnds[0],{7,0,0}),0,1e-5,"arc endpoint stays connected");
}
KACHA_V2_TEST(dimension, independent_edit_cannot_break_constraint)
{
    Fixture f;const auto ref=f.Line({0,0,0},{10,0,0});const auto d=f.Dim("dim_length",{ref},10);f.Set(d);
    const auto feature=f.doc.Snapshot().features.front();auto wire=std::get<domain::CreateWireDefinition>(feature.definition);
    wire.segments[0]=geometry::CurveSegment::MakeLine({0,0,0},{20,0,0}).Value();
    Require(!f.doc.Run(document::UpdateFeatureDefinitionCommand(feature.id,wire,{},"edit")).committed,"other edits respect constraint");
    auto reference=d;reference.driving=false;f.Set(reference);
    Require(f.doc.Run(document::UpdateFeatureDefinitionCommand(feature.id,wire,{},"edit")).committed,"reference releases constraint");
    RequireNear(f.Value(reference),20,1e-5,"reference follows geometry");
}
KACHA_V2_TEST_MAIN("dimension_tests")
