#include "V2SelfTest.h"
#include "V2EntityTree.h"
#include "V2LoopFacesTool.h"
#include "V2MainWindow.h"
#include "V2ThickenDock.h"
#include "V2Viewport.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/modeling/MeshPick.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QIODevice>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointF>
#include <QPushButton>
#include <QString>
#include <QTreeWidgetItem>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {
using base::EntityId;
using domain::EntityKind;
using geometry::CurveSegment;
using geometry::Vector3;
void MouseClick(QWidget& target,const QPointF& p,Qt::KeyboardModifiers modifiers)
{
    for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
        QMouseEvent event(type,p,QPointF(target.mapToGlobal(p.toPoint())),Qt::LeftButton,
            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,modifiers);
        QApplication::sendEvent(&target,&event);
    }
}
bool CaseLoopFacesPanoramaHrubber(V2MainWindow& window,bool compound=false,bool viewportPick=false)
{
    const QString path=qEnvironmentVariable("KACHACAD_SURFACE_TEST_FILE");
    if(path.isEmpty()||!window.OpenDocumentFile(path))return false;
    window.RunCommand("surface.from_lines");
    app::SelectionSet selection;
    for(const auto& entity:window.Session().GetDocument().Snapshot().entities)
        if(entity.id.ToString().ends_with("169a")||entity.id.ToString().ends_with("16a5")
            ||entity.id.ToString().ends_with("1a32")||entity.id.ToString().ends_with("1a3d"))
            selection.entityIds.push_back(entity.id);
    if(!Explain("パノラマ窓の外周と内周の投影ワイヤ4本を元データから選べる",selection.entityIds.size()==4))return false;
    if(compound){
        std::vector<geometry::CurveSegment> curves;
        for(const auto id:selection.entityIds)for(const auto& curve:window.Session().Scene().curves)
            if(curve.entityId==id)curves.push_back(curve.segment);
        if(!Explain("同じ32区間を1つの複合ワイヤとして選べる",curves.size()==32))return false;
        const auto made=window.Session().AddWire(curves,false,"Hゴムの内外周をまとめた入力");
        if(!made.committed)return false;
        selection.entityIds={made.createdEntityIds.front()};
        if(!window.SaveAndReopen(qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT")+"-compound-input.kcd2"))return false;
        window.RunCommand("surface.from_lines");
    }
    const auto original=window.Session().GetDocument().Snapshot();
    io::DocumentFile originalFile;originalFile.snapshot=original;
    const auto originalJson=io::WriteDocumentJson(originalFile);
    const auto originalsPreserved=[&]{
        io::DocumentFile current;current.snapshot=window.Session().GetDocument().Snapshot();
        std::erase_if(current.snapshot.entities,[&](const auto& entity){return std::none_of(original.entities.begin(),original.entities.end(),[&](const auto& e){return e.id==entity.id;});});
        std::erase_if(current.snapshot.features,[&](const auto& feature){return std::none_of(original.features.begin(),original.features.end(),[&](const auto& f){return f.id==feature.id;});});
        current.snapshot.revision=original.revision;current.snapshot.evaluationOrder=original.evaluationOrder;
        return io::WriteDocumentJson(current)==originalJson;
    };
    window.Viewport().SetSelection({});QApplication::processEvents();
    if(viewportPick){
        auto& view=window.Viewport();
        window.resize(1600,1000);view.SetViewDirection(ViewDirection::Isometric);view.FitToDocument();
        QApplication::processEvents();
        for(const auto id:selection.entityIds){
            bool pressed=false;
            for(const auto& curve:window.Session().Scene().curves){
                if(curve.entityId!=id||curve.segment.Kind()==geometry::CurveKind::Line)continue;
                const auto pixel=view.Mapping().Project(curve.segment.Evaluate(.43));if(!pixel)continue;
                const QPointF p(pixel->x,pixel->y);view.HoverAt(p);
                for(std::size_t i=0,n=view.CandidateCount();i<n;++i){
                    const auto candidate=view.CurrentCandidate();
                    if(candidate&&candidate->entityId==id&&candidate->segmentId==curve.segmentId){
                        MouseClick(view,p,Qt::ControlModifier);
                        pressed=true;break;
                    }
                    view.CycleCandidate(false);
                }
                if(pressed)break;
            }
            if(!Explain("3Dビューの曲線をCtrlクリックで選び足せる",pressed))return false;
        }
        if(!Explain("3Dの4回のクリックは4ワイヤーの部分区間を選ぶ",view.Selection().entityIds==selection.entityIds
            &&view.Selection().ordered.size()==4&&std::all_of(view.Selection().ordered.begin(),view.Selection().ordered.end(),
                [](const auto& ref){return ref.segmentId.has_value();})))return false;
        QApplication::processEvents();
        const auto& partialPlan=window.LoopFacesTool().Plan();
        if(partialPlan)Note(partialPlan->summaryJa.c_str());
        if(!Explain("4部分区間を選んだだけでは窓枠の帯にならず、原本を変えない",partialPlan
            &&partialPlan->summaryJa.find("穴 1個")==std::string::npos&&originalsPreserved()))return false;
        auto* whole=window.findChild<QPushButton*>("loopFacesWholeWires");
        if(!Explain("選択した区間数とワイヤー全体を使うボタンが右棚に見える",whole&&whole->isVisible()
            &&whole->isEnabled()&&window.LoopFacesTool().Dock()->SelectionTextJa().contains(QStringLiteral("4区間"))))return false;
        const auto output=qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT");
        if(!output.isEmpty())window.grab().save(output+"-partial-selection.png");
        MouseClick(*whole,whole->rect().center(),Qt::NoModifier);
        QApplication::processEvents();
        if(!Explain("1回のボタン操作で指定4本の全32区間を使い原本は不変",view.Selection().entityIds==selection.entityIds
            &&app::SelectedCurves(view.Selection(),window.Session().Scene()).size()==32&&originalsPreserved()
            &&!whole->isVisible()))return false;
        if(!output.isEmpty())window.grab().save(output+"-whole-selection.png");
    }else{
        auto* tree=window.EntityTree();if(!tree)return false;
        for(std::size_t i=0;i<selection.entityIds.size();++i){
            auto* item=tree->FindKey("entity:"+QString::fromStdString(selection.entityIds[i].ToString()));
            if(!item)return false;
            for(auto* parent=item->parent();parent;parent=parent->parent())parent->setExpanded(true);
            tree->scrollToItem(item);QApplication::processEvents();
            MouseClick(*tree->viewport(),tree->visualItemRect(item).center(),i?Qt::ControlModifier:Qt::NoModifier);
        }
        if(!Explain("左ペインのCtrlクリックから指定した入力だけを選べる",window.Viewport().Selection().entityIds==selection.entityIds))return false;
    }
    QApplication::processEvents(); // Let the queued preview run after the final mouse release.
    const auto& plan=window.LoopFacesTool().Plan();
    if(plan)Note(plan->summaryJa.c_str());
    if(!Explain("投影区間の継ぎ目を含む窓枠の面候補を表示する",
        plan&& !plan->faces.empty() && window.Viewport().ToolPreviewFaceCount()>0))return false;
    const double tolerance=window.LoopFacesTool().Dock()->JoinToleranceValue();
    if(!Explain("窓全体を埋めず1つの穴を持つ帯を下見する",plan->faces.size()==1
        &&plan->summaryJa.find("穴 1個")!=std::string::npos))return false;
    const QString toleranceLabel=QStringLiteral("文書の接続許容値をそのまま使う（実値 %1）")
        .arg(tolerance,0,'f',3);
    if(!Explain(toleranceLabel.toUtf8().constData(),
        std::abs(tolerance-window.Session().GetDocument().Snapshot().settings.tolerance.interactiveJoinMm)<1e-10))return false;
    const int before=CountOfKind(window,EntityKind::GuideSurface);
    const int wires=CountOfKind(window,EntityKind::Wire);
    std::vector<EntityId> priorSurfaces;
    for(const auto& entity:window.Session().GetDocument().Snapshot().entities)
        if(entity.kind==EntityKind::GuideSurface)priorSurfaces.push_back(entity.id);
    if(!window.LoopFacesTool().HandleKey(Qt::Key_Return))return false;
    QApplication::processEvents();
    const QString buildLabel=QStringLiteral("候補を面にして元の投影ワイヤを保つ（%1）").arg(window.StatusText());
    const bool surfaceMade=CountOfKind(window,EntityKind::GuideSurface)>before;
    if(!Explain(buildLabel.toUtf8().constData(),surfaceMade))return false;
    const QString outputRoot=qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT");
    const QString output=outputRoot.isEmpty()?QString():outputRoot+(compound?"-compound":"");
    if(!output.isEmpty()){
        window.Viewport().SetViewDirection(ViewDirection::Isometric);
        window.Viewport().FitToDocument();QApplication::processEvents();
        window.grab().save(output+QStringLiteral("-h-rubber-made-face.png"));
    }
    if(!Explain("面を作っても輪郭ワイヤを全て消さない",CountOfKind(window,EntityKind::Wire)>=wires))return false;
    EntityId madeSurface;
    for(const auto& entity:window.Session().GetDocument().Snapshot().entities)
        if(entity.kind==EntityKind::GuideSurface
            &&std::find(priorSurfaces.begin(),priorSurfaces.end(),entity.id)==priorSurfaces.end())madeSurface=entity.id;
    if(!Explain("作った面を選択できる",!madeSurface.IsNil()))return false;
    const auto hits=[&](EntityId id,Vector3 origin,Vector3 direction){
        for(const auto& shape:window.Viewport().ShapeViews())if(shape.entityId==id)
            for(const auto& tri:shape.mesh.triangles)if(modeling::RayHitsTriangle(origin,direction,tri))return true;
        return false;
    };
    const auto hollowBand=[&](EntityId id){return !hits(id,{10,0,21.5},{0,-1,0})
        &&hits(id,{10,0,25.35},{0,-1,0})&&hits(id,{10,0,18.15},{0,-1,0})
        &&hits(id,{30,-20.65,21.5},{-1,0,0});};
    if(!Explain("窓内を空けたまま前面の上下と側面にHゴムの帯を残す",hollowBand(madeSurface)&&originalsPreserved()))return false;
    app::SelectionSet faceSelection;faceSelection.entityIds={madeSurface};
    window.Viewport().SetSelection(faceSelection);QApplication::processEvents();
    const auto* selectedFace=window.Session().GetDocument().FindEntity(madeSurface);
    if(!Explain("厚み付け前に選択欄が新しい形状ガイドの面を指す",
        window.Viewport().Selection().entityIds.size()==1
            &&window.Viewport().Selection().entityIds.front()==madeSurface
            &&selectedFace&&selectedFace->kind==EntityKind::GuideSurface))return false;
    window.RunCommand("part.thicken");
    auto* amount=window.findChild<QDoubleSpinBox*>("thickenAmount");
    if(!Explain("面の厚み設定欄が開く",amount!=nullptr))return false;
    if(!Explain(window.ThickenDock().SurfaceTextJa().toUtf8().constData(),
        window.ThickenDock().SurfaceTextJa().contains(QStringLiteral("面"))))return false;
    amount->setValue(.1);
    QApplication::processEvents();
    (void)window.ThickenDock().ClickCorrection(false);
    bool thickened=false;
    const QString thicknessTrace=qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT")+QStringLiteral("-thickness.txt");
    if(thicknessTrace!=QStringLiteral("-thickness.txt"))QFile::remove(thicknessTrace);
    for(const auto placement:{kachakacha::v2::fabrication::ThicknessPlacement::Centered,
        kachakacha::v2::fabrication::ThicknessPlacement::Inside,
        kachakacha::v2::fabrication::ThicknessPlacement::Outside}){
        if(!window.ThickenDock().ClickPlacementCard(placement))continue;
        QApplication::processEvents();
        const bool confirmed=window.ThickenDock().ClickConfirm();
        if(confirmed&&CountOfKind(window,EntityKind::Part)>0){thickened=true;break;}
        if(thicknessTrace!=QStringLiteral("-thickness.txt")){
            QFile file(thicknessTrace);if(file.open(QIODevice::WriteOnly|QIODevice::Append))
                file.write(QStringLiteral("placement=%1 confirmed=%2\n%3\nstatus=%4\n")
                    .arg(static_cast<int>(placement)).arg(confirmed).arg(window.ThickenDock().StatusTextJa(),window.StatusText())
                    .toUtf8());
        }
    }
    if(!thickened&&!output.isEmpty())window.grab().save(output+QStringLiteral("-h-rubber-thicken-failed.png"));
    if(!Explain("Hゴム面を現実的な0.1 mm厚で立体にできる",thickened))return false;
    if(!Explain("選択した H ゴム面を0.1 mm厚の立体にできる",CountOfKind(window,EntityKind::Part)>0))return false;
    EntityId madePart;
    for(const auto& entity:window.Session().GetDocument().Snapshot().entities)if(entity.kind==EntityKind::Part)madePart=entity.id;
    if(!Explain("厚みを付けても窓と帯と原本のUUID・曲線・参照を保つ",hollowBand(madePart)&&originalsPreserved()))return false;
    if(!output.isEmpty()){
        app::SelectionSet solid;solid.entityIds={madePart};window.Viewport().SetSelection(solid);
        window.Viewport().IsolateSelection();window.RunCommand("view.fit_selection");QApplication::processEvents();
        window.grab().save(output+"-h-rubber-solid.png");window.Viewport().ClearIsolation();
    }
    window.RunCommand("edit.undo");
    if(!Explain("厚み付けだけを取り消して元の面を保つ",CountOfKind(window,EntityKind::Part)==0
        &&CountOfKind(window,EntityKind::GuideSurface)==before+1))return false;
    window.RunCommand("edit.redo");
    if(!Explain("厚み付けをやり直せる",CountOfKind(window,EntityKind::Part)==1&&hollowBand(madePart)&&originalsPreserved()))return false;
    if(!output.isEmpty()){
        if(!Explain("穴付き面とHゴム立体を保存して実際に再読込できる",window.SaveAndReopen(output+"-h-rubber-solid.kcd2")
            &&window.RebuildProblems().isEmpty()&&hollowBand(madeSurface)&&hollowBand(madePart)&&originalsPreserved()))return false;
        return true;
    }
    window.RunCommand("edit.undo");window.RunCommand("edit.undo");
    if(!Explain("面作成を取り消すと元の面数に戻る",CountOfKind(window,EntityKind::GuideSurface)==before))return false;
    return true;
}

bool CaseLoopFacesOpenInner(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const auto line=[](Vector3 a,Vector3 b){return CurveSegment::MakeLine(a,b).Value();};
    const auto first=window.Session().AddWire({line({0,0,0},{20,0,0}),line({20,0,0},{20,20,0}),
        line({20,20,0},{0,20,0}),line({0,20,0},{0,0,0}),line({3,3,0},{7,3,0}),
        line({7,3,0},{7,7,0}),line({7,7,0},{3,7,0})},false,"外周と未閉窓");
    const auto second=window.Session().AddWire({line({12,3,0},{16,3,0}),line({16,3,0},{16,7,0}),
        line({16,7,0},{12,7,0}),line({12,7,0},{12,3,0})},true,"閉じた別の窓");
    if(!first.committed||!second.committed||!window.SaveAndReopen("open-inner-loop.kcd2"))return false;
    app::SelectionSet selected;selected.entityIds={first.createdEntityIds.front(),second.createdEntityIds.front()};
    window.Viewport().SetSelection(selected);window.RunCommand("surface.from_lines");
    QApplication::processEvents();
    if(!Explain("別の閉じた穴があっても未閉窓を黙って埋めず、確定を止める",
        window.LoopFacesTool().Plan()&&window.LoopFacesTool().Plan()->faces.empty()
        &&!window.LoopFacesTool().Dock()->ConfirmEnabled()
        &&window.LoopFacesTool().Dock()->SummaryTextJa().contains(QStringLiteral("内側"))))return false;
    const auto revision=window.Session().GetDocument().Revision();
    window.HandleToolKey(Qt::Key_Return,nullptr);
    if(!Explain("Enterを押しても選択と原本と履歴を変更しない",window.Session().GetDocument().Revision()==revision
        &&CountOfKind(window,EntityKind::GuideSurface)==0&&window.Viewport().Selection().entityIds==selected.entityIds))return false;
    window.HandleToolKey(Qt::Key_Escape,nullptr);
    window.RunCommand("draw.line");window.RunCommand("surface.from_lines");
    const auto close=window.Session().AddWire({line({3,7,0},{3,3,0})},false,"未閉窓を閉じる辺");
    if(!close.committed||!window.SaveAndReopen("closed-inner-loop.kcd2"))return false;
    selected.entityIds.push_back(close.createdEntityIds.front());
    window.Viewport().SetSelection(selected);window.RunCommand("surface.from_lines");
    QApplication::processEvents();
    const auto& plan=window.LoopFacesTool().Plan();
    if(!Explain("不足辺を加えれば2つの穴を持つ1枚の面に戻れる",plan&&plan->faces.size()==1
        &&plan->summaryJa.find("穴 2個")!=std::string::npos))return false;
    window.HandleToolKey(Qt::Key_Return,nullptr);
    return Explain("修復後は元の3ワイヤを残して穴付き面を確定する",CountOfKind(window,EntityKind::GuideSurface)==1
        &&CountOfKind(window,EntityKind::Wire)==3);
}

bool CaseLoopFacesWholeWires(V2MainWindow& w)
{
    w.RunCommand("file.new");
    const auto line=[](Vector3 a,Vector3 b){return CurveSegment::MakeLine(a,b).Value();};
    std::vector<EntityId> wires;
    for(double x:{0.,3.,30.}){
        const double size=x==3?4:10;
        const double y=x==3?3:0;
        const auto wire=w.Session().AddWire({line({x,y,0},{x+size,y,0}),line({x+size,y,0},{x+size,y+size,0}),
            line({x+size,y+size,0},{x,y+size,0}),line({x,y+size,0},{x,y,0})},true,"選択範囲の試験");
        if(!wire.committed)return false;wires.push_back(wire.createdEntityIds.front());
    }
    if(!w.SaveAndReopen("whole-wire-selection.kcd2"))return false;
    app::SelectionSet selected;selected.entityIds={wires[0],wires[1]};
    for(auto id:selected.entityIds)for(const auto& curve:w.Session().Scene().curves)if(curve.entityId==id){
        app::SelectionRef ref;ref.entityId=id;ref.kind=app::SelectionElementKind::Edge;ref.segmentId=curve.segmentId;
        selected.ordered.push_back(ref);break;
    }
    w.Viewport().SetSelection(selected);w.RunCommand("surface.from_lines");QApplication::processEvents();
    auto* dock=w.LoopFacesTool().Dock();const auto revision=w.Session().GetDocument().Revision();
    const double tolerance=dock->JoinToleranceValue();
    if(!Explain("部分区間の入力を黙って全体にしない",app::SelectedCurves(w.Viewport().Selection(),w.Session().Scene()).size()==2
        &&dock->SelectionTextJa().contains(QStringLiteral("2区間"))))return false;
    if(!Explain("棚から選んだ2本だけを全体へ広げる",dock->ClickWholeWires()
        &&w.Viewport().Selection().entityIds==selected.entityIds
        &&app::SelectedCurves(w.Viewport().Selection(),w.Session().Scene()).size()==8
        &&!app::IsSelected(w.Viewport().Selection(),wires[2])&&w.Session().GetDocument().Revision()==revision
        &&dock->JoinToleranceValue()==tolerance&&!dock->ClickWholeWires()))return false;
    QApplication::processEvents();
    if(!Explain("全体へ切り替えると1つの穴を持つ1枚の面を下見する",w.LoopFacesTool().Plan()
        &&w.LoopFacesTool().Plan()->faces.size()==1&&w.Viewport().ToolPreviewFaceCount()>0
        &&dock->SummaryTextJa().contains(QStringLiteral("穴 1個"))))return false;
    w.HandleToolKey(Qt::Key_Escape,nullptr);w.RunCommand("draw.line");w.RunCommand("surface.from_lines");
    w.Viewport().SetSelection(selected);QApplication::processEvents();
    if(!Explain("取消して他の道具を経由しても部分選択と全体ボタンへ戻れる",app::SelectedCurves(w.Viewport().Selection(),w.Session().Scene()).size()==2
        &&dock->ClickWholeWires()))return false;
    w.HandleToolKey(Qt::Key_Escape,nullptr);
    return Explain("選択範囲の変更と取消で形状や履歴を変えない",w.Session().GetDocument().Revision()==revision
        &&CountOfKind(w,EntityKind::GuideSurface)==0);
}
} // namespace
std::vector<SelfTestCase> LoopFacesHrubberCases()
{
    std::vector<SelfTestCase> cases{{"HP-LF-OPEN-INNER 閉じていない窓を別の閉輪で埋めない",CaseLoopFacesOpenInner}};
    cases.push_back({"HP-LF-WHOLE-WIRES 部分区間から明示的に選択ワイヤー全体へ切り替える",CaseLoopFacesWholeWires});
    if(qEnvironmentVariableIsSet("KACHACAD_SURFACE_TEST_FILE")){
        cases.push_back({"HP-LF-115-HRUBBER パノラマ窓の投影継ぎ目を補正して面にする",[](V2MainWindow& w){return CaseLoopFacesPanoramaHrubber(w);}});
        cases.push_back({"HP-LF-115-HRUBBER-COMPOUND 同じ内外周を1ワイヤで選び穴付きの帯と立体にする",[](V2MainWindow& w){return CaseLoopFacesPanoramaHrubber(w,true);}});
        cases.push_back({"HP-LF-115-HRUBBER-VIEW 3DのCtrlクリックから穴付きの帯と立体にする",[](V2MainWindow& w){return CaseLoopFacesPanoramaHrubber(w,false,true);}});
    }
    return cases;
}
} // namespace kachakacha::v2::selftest
