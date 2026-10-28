#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2HoverEditTool.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/geometry/CurveTrim.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/document/Dimension.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QPointF>
#include <QPixmap>
#include <QString>
#include <algorithm>
#include <cmath>

namespace kachakacha::v2::selftest {
namespace {
using namespace kachakacha::v2;
bool Same(const geometry::CurveSegment& a, const geometry::CurveSegment& b) {
    return a.Kind()==b.Kind() && a.ControlPoints()==b.ControlPoints() && a.Center()==b.Center()
        && a.Normal()==b.Normal() && a.ReferenceDirection()==b.ReferenceDirection() && a.Radius()==b.Radius()
        && a.StartAngleRad()==b.StartAngleRad() && a.SweepAngleRad()==b.SweepAngleRad();
}
bool OthersIntact(const modeling::SnapScene& before, const modeling::SnapScene& after, base::EntityId edited) {
    for(const auto& curve:before.curves) if(curve.entityId!=edited) {
        const auto match=std::find_if(after.curves.begin(),after.curves.end(),[&](const auto& c){
            return c.entityId==curve.entityId && c.segmentId==curve.segmentId && Same(c.segment,curve.segment);
        });
        if(match==after.curves.end())return false;
    }
    return true;
}
double Length(const modeling::SnapScene& scene, base::EntityId id) {
    double length=0;for(const auto& c:scene.curves)if(c.entityId==id)length+=c.segment.TotalLength(1e-8);return length;
}
void Capture(V2MainWindow& w,const char* name) {
    const auto path=qEnvironmentVariable("KACHACAD_TRIM_CAPTURE_DIR");if(path.isEmpty())return;
    QDir().mkpath(path);QApplication::processEvents();w.grab().save(path+"/"+name);
}
bool PointAt(V2MainWindow& w, geometry::Vector3 point, base::EntityId wanted, bool click) {
    const auto p=w.Viewport().Mapping().Project(point);if(!p)return false;
    w.Viewport().HoverAt({p->x,p->y});
    for(int i=0;i<100;++i) {
        const auto candidate=w.Viewport().CurrentCandidate();
        if(candidate&&candidate->entityId==wanted) {
            if(click)w.Viewport().ClickAt({p->x,p->y});
            return true;
        }
        if(!w.Viewport().CycleCandidate(false))break;
    }
    return Explain("カーソル位置で目的の元線を選べる",false);
}
base::EntityId Baked(V2MainWindow& w,const std::vector<geometry::CurveSegment>& curves,
    const std::vector<base::EntityId>& inputs,const char* name,base::IdGenerator& ids) {
    domain::Feature f;f.id=ids.NextTyped<base::IdKind::Feature>();f.type=domain::FeatureType::TransformWire;
    f.displayName=name;f.inputEntityIds=inputs;domain::CreateWireDefinition wire;wire.segments=curves;
    for(const auto& c:curves){(void)c;wire.segmentIds.push_back(ids.NextTyped<base::IdKind::Segment>());}
    f.definition=wire;domain::Entity e;e.id=ids.NextTyped<base::IdKind::Entity>();e.kind=domain::EntityKind::Wire;e.displayName=name;
    f.outputs={{"wire",e.id,e.kind}};
    return w.Session().GetDocument().Run(document::AddFeatureCommand(f,{e},name)).committed?e.id:base::EntityId{};
}
bool CaseLegacyR(V2MainWindow& w) {
    w.RunCommand("file.new");base::DeterministicIdGenerator ids(870000);
    const auto source=w.Session().AddWire({geometry::CurveSegment::MakeLine({0,0,0},{10,0,0}).Value()},false,"元直線");
    const auto cut=w.Session().AddWire({geometry::CurveSegment::MakeLine({9,-2,0},{9,2,0}).Value()},false,"境界");
    if(!source.committed||!cut.committed)return false;const auto id=source.createdEntityIds.front();
    const auto arc=geometry::CurveSegment::MakeCircularArc({9,1,0},{0,0,1},{0,-1,0},1,0,1.5707963267948966).Value();
    const auto r=Baked(w,{arc},{id,cut.createdEntityIds.front()},"過去版のR丸め",ids);
    const auto r2=Baked(w,{geometry::CurveSegment::MakeCubicBezier({{0,3,0},{2,5,0},{6,5,0},{8,3,0}}).Value()},{r},"後続曲線",ids);
    if(r.IsNil()||r2.IsNil())return false;
    w.Session().SetScene(app::RebuildSceneKeepingView(w.Session().Scene(),w.Session().GetDocument().Snapshot(),ids));
    w.RunCommand("wire.trim");w.resize(1440,900);QApplication::processEvents();
    w.Viewport().SetViewDirection(ViewDirection::Top);w.Viewport().SetViewCenter({5,1,0});w.Viewport().SetVisibleWidthMm(24);
    const auto before=w.Session().Scene();
    if(!PointAt(w,{9.5,0,0},id,false))return false;
    Capture(w,"legacy-r-before.png");const auto planned=w.HoverEditTool().Outcome();
    if(!Explain("赤い下見は直線の1 mmだけ",planned&&planned->previewLine.size()>1
        &&std::abs(geometry::Distance(planned->previewLine.front(),planned->previewLine.back())-1)<1e-6))return false;
    if(!PointAt(w,{9.5,0,0},id,true))return false;Capture(w,"legacy-r-after.png");
    const auto after=w.Session().Scene();
    if(!Explain("赤い区間だけ消え、元線UUID・全てのRと後続曲線を維持",w.Session().GetDocument().FindEntity(id)
        &&OthersIntact(before,after,id)&&std::abs(Length(before,id)-Length(after,id)-1)<1e-6))return false;
    w.RunCommand("edit.undo");if(std::abs(Length(w.Session().Scene(),id)-10)>1e-6||!OthersIntact(before,w.Session().Scene(),id))return false;
    w.RunCommand("edit.redo");return w.SaveAndReopen("trim-legacy-r-preserved.kcd2")
        &&OthersIntact(before,w.Session().Scene(),id)&&std::abs(Length(w.Session().Scene(),id)-9)<1e-6&&w.RebuildProblems().isEmpty();
}
bool CaseWholeWithBakedR(V2MainWindow& w) {
    w.RunCommand("file.new");base::DeterministicIdGenerator ids(880000);
    const auto source=w.Session().AddWire({geometry::CurveSegment::MakeLine({0,0,0},{1,0,0}).Value()},false,"不要な短線");
    if(!source.committed)return false;const auto id=source.createdEntityIds.front();
    const auto r=Baked(w,{geometry::CurveSegment::MakeCircularArc({0,3,0},{0,0,1},{1,0,0},1,0,1.57).Value()},{id},"残すR",ids);
    if(r.IsNil())return false;
    w.Session().SetScene(app::RebuildSceneKeepingView(w.Session().Scene(),w.Session().GetDocument().Snapshot(),ids));
    w.RunCommand("wire.trim");w.Viewport().SetViewDirection(ViewDirection::Top);
    w.Viewport().SetViewCenter({0,0,0});w.Viewport().SetVisibleWidthMm(20);const auto before=w.Session().Scene();
    if(!PointAt(w,{.5,0,0},id,true))return false;
    return Explain("短い元線全体を削除しても完成済みのRを巻き込まない",!w.Session().GetDocument().FindEntity(id)
        &&w.Session().GetDocument().FindEntity(r)&&OthersIntact(before,w.Session().Scene(),id)
        &&w.SaveAndReopen("trim-whole-preserves-r.kcd2")&&OthersIntact(before,w.Session().Scene(),id)&&w.RebuildProblems().isEmpty());
}
bool CaseUntouchedSegments(V2MainWindow& w) {
    w.RunCommand("file.new");std::vector<geometry::CurveSegment> chain{
        geometry::CurveSegment::MakeLine({0,0,0},{10,0,0}).Value(),
        geometry::CurveSegment::MakeCircularArc({10,2,0},{0,0,1},{0,-1,0},2,0,1.5707963267948966).Value(),
        geometry::CurveSegment::MakeLine({12,2,0},{12,8,0}).Value()};
    const auto source=w.Session().AddWire(chain,false,"直線とRの連続ワイヤー");
    if(!source.committed||!w.Session().AddWire({geometry::CurveSegment::MakeLine({1,-2,0},{1,2,0}).Value()},false,"境界").committed)return false;
    const auto id=source.createdEntityIds.front();w.RunCommand("wire.trim");
    w.Viewport().SetViewDirection(ViewDirection::Top);w.Viewport().SetViewCenter({6,3,0});w.Viewport().SetVisibleWidthMm(24);
    const auto before=w.Session().Scene();
    if(!PointAt(w,{.5,0,0},id,true))return false;
    int unchanged=0;for(const auto& old:before.curves)if(old.entityId==id&&old.segment.Kind()!=geometry::CurveKind::Line)
        for(const auto& now:w.Session().Scene().curves)if(now.entityId==id&&now.segmentId==old.segmentId&&Same(now.segment,old.segment))++unchanged;
    return Explain("同じワイヤー内の未編集Rは種類・形・線分UUIDを維持",unchanged==1&&OthersIntact(before,w.Session().Scene(),id)
        &&std::abs(Length(before,id)-Length(w.Session().Scene(),id)-1)<1e-6);
}
bool CaseSplitDimension(V2MainWindow& w) {
    w.RunCommand("file.new");base::DeterministicIdGenerator ids(890000);
    const auto a=w.Session().AddWire({geometry::CurveSegment::MakeLine({0,0,0},{10,0,0}).Value(),
        geometry::CurveSegment::MakeLine({10,0,0},{10,5,0}).Value()},false,"折れ線");
    if(!a.committed)return false;const auto id=a.createdEntityIds.front();
    for(double x:{4.,6.})if(!w.Session().AddWire({geometry::CurveSegment::MakeLine({x,-1,0},{x,1,0}).Value()},false,"境界").committed)return false;
    const auto before=w.Session().Scene();base::SegmentId vertical;
    for(const auto& c:before.curves)if(c.entityId==id&&c.segment.EndPoint().y==5)vertical=c.segmentId;
    document::ReferenceDimension d;d.id=ids.NextTyped<base::IdKind::Dimension>();d.kind="dim_length";d.label="残す側の寸法";
    d.unit="mm";d.targets={id};d.segments={{id,vertical}};d.recordedValue=5;
    if(!w.Session().GetDocument().Run(document::SetDimensionCommand(d)).committed)return false;
    w.RunCommand("wire.trim");w.Viewport().SetViewDirection(ViewDirection::Top);w.Viewport().SetViewCenter({5,2,0});w.Viewport().SetVisibleWidthMm(20);
    if(!PointAt(w,{5,0,0},id,true))return false;
    const auto& dims=w.Session().GetDocument().Snapshot().referenceDimensions;
    if(!Explain("2本に分かれた先の未編集線分IDと寸法を正しい線へ引継ぐ",dims.size()==1&&dims[0].segments.front().segmentId==vertical
        &&dims[0].segments.front().entityId!=id&&document::EvaluateDimension(w.Session().GetDocument().Snapshot(),dims[0]).HasValue()))return false;
    w.RunCommand("edit.undo");if(w.Session().GetDocument().Snapshot().referenceDimensions.front().segments.front().entityId!=id)return false;
    w.RunCommand("edit.redo");return w.SaveAndReopen("trim-split-dimension.kcd2")
        &&std::abs(document::EvaluateDimension(w.Session().GetDocument().Snapshot(),w.Session().GetDocument().Snapshot().referenceDimensions.front()).Value().value-5)<1e-8;
}
bool CaseNativeRollback(V2MainWindow& w) {
    w.RunCommand("file.new");const auto source=w.Session().AddWire({
        geometry::CurveSegment::MakeLine({0,0,0},{10,0,0}).Value(),geometry::CurveSegment::MakeLine({10,0,0},{10,10,0}).Value(),
        geometry::CurveSegment::MakeLine({10,10,0},{0,10,0}).Value(),geometry::CurveSegment::MakeLine({0,10,0},{0,0,0}).Value()},false,"面の境界");
    if(!source.committed)return false;const auto id=source.createdEntityIds.front();
    app::SelectionSet picked;picked.entityIds={id};w.Viewport().SetSelection(picked);w.RunCommand("part.extrude");
    if(!w.HandleToolKey(Qt::Key_Return,nullptr)||CountOfKind(w,domain::EntityKind::Part)!=1)return false;
    if(!w.Session().AddWire({geometry::CurveSegment::MakeLine({1,-1,0},{1,1,0}).Value()},false,"境界").committed)return false;
    w.RunCommand("wire.trim");w.Viewport().SetViewDirection(ViewDirection::Top);w.Viewport().SetViewCenter({5,5,0});w.Viewport().SetVisibleWidthMm(20);
    const auto before=w.Session().Scene();const auto revision=w.Session().GetDocument().Revision();
    if(!PointAt(w,{.5,0,0},id,true))return false;
    Note((std::string("native rollback revision=")+std::to_string(revision)+" -> "+std::to_string(w.Session().GetDocument().Revision())
        +" length="+std::to_string(Length(before,id))+" -> "+std::to_string(Length(w.Session().Scene(),id))
        +" problems="+w.RebuildProblems().toStdString()+" status="+w.StatusText().toStdString()).c_str());
    return Explain("参照面を再計算できない編集は文書と表示を戻す",w.Session().GetDocument().Revision()==revision
        &&std::abs(Length(before,id)-Length(w.Session().Scene(),id))<1e-8&&w.RebuildProblems().isEmpty()
        &&CountOfKind(w,domain::EntityKind::Part)==1&&!w.Viewport().ShapeViews().empty());
}
bool CaseOwner(V2MainWindow& w) {
    const auto path=qEnvironmentVariable("KACHACAD_TRIM_OWNER_FILE");
    if(path.isEmpty()||!w.OpenDocumentFile(path)||!w.RebuildProblems().isEmpty())return false;
    base::EntityId id;for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.id.ShortSuffix()=="0089")id=e.id;
    if(id.IsNil())return false;w.resize(1600,1000);QApplication::processEvents();
    w.RunCommand("wire.trim");w.Viewport().SetViewDirection(ViewDirection::Front);
    w.Viewport().SetViewCenter({0,0,14});w.Viewport().SetVisibleWidthMm(48);
    const auto before=w.Session().Scene();const auto count=w.Session().GetDocument().Snapshot().entities.size();
    if(!PointAt(w,{6,0,2},id,false))return false;Capture(w,"115-before.png");
    const auto plan=w.HoverEditTool().Outcome();
    if(plan) for(const auto& c:before.curves) if(c.entityId.ShortSuffix()=="00c0") {
        const auto& target=plan->source.segments.front();
        const auto cuts=geometry::CutPointsOn(target,{c.segment},w.Session().GetDocument().Snapshot().settings.tolerance);
        Note((std::string("owner R ")+c.segmentId.ShortSuffix()+" kind="+std::to_string(static_cast<int>(c.segment.Kind()))+" cuts="+std::to_string(cuts.size())).c_str());
    }
    Note(plan ? (std::string("owner preview source=")+plan->source.entityId.ShortSuffix()+" / "+plan->summaryJa+" chains="+std::to_string(plan->chains.size())).c_str() : "owner preview absent");
    if(!Explain("115投影の下見は1 mmのはみ出し区間",plan&&plan->chains.size()==1
        &&std::abs(geometry::Distance(plan->previewLine.front(),plan->previewLine.back())-1)<1e-5))return false;
    if(!PointAt(w,{6,0,2},id,true))return false;Capture(w,"115-after.png");
    if(!Explain("115投影で全ての他の線・R・窓・面を残し1 mmだけ削除",w.Session().GetDocument().Snapshot().entities.size()==count
        &&OthersIntact(before,w.Session().Scene(),id)&&std::abs(Length(before,id)-Length(w.Session().Scene(),id)-1)<1e-5))return false;
    w.RunCommand("edit.undo");if(std::abs(Length(before,id)-Length(w.Session().Scene(),id))>1e-5)return false;
    w.RunCommand("edit.redo");return w.SaveAndReopen("115-trim-local-preserved.kcd2")
        &&OthersIntact(before,w.Session().Scene(),id)&&w.RebuildProblems().isEmpty();
}
}
std::vector<SelfTestCase> TrimPreservationCases() {
    std::vector<SelfTestCase> cases{
        {"HP-TR-07 one millimeter trim preserves legacy fillets and all other geometry",CaseLegacyR},
        {"HP-TR-08 whole wire removal preserves baked downstream curves",CaseWholeWithBakedR},
        {"HP-TR-09 untouched curve geometry and segment identities survive partial trim",CaseUntouchedSegments},
        {"HP-TR-10 split preserves unedited dimension references",CaseSplitDimension},
        {"HP-TR-11 native consumer failure rolls back geometry and history",CaseNativeRollback}};
    if(qEnvironmentVariableIsSet("KACHACAD_TRIM_OWNER_FILE"))cases.push_back({"HP-TR-OWNER actual 115 one millimeter trim preserves all rounded geometry",CaseOwner});
    return cases;
}
}
