//! 「厚み」の人の道(HP-TH、指示書 matrix P-10)。
//!
//! 厚みを押す → 面待ち → 3D で形状ガイドの面を押すと入る(押し直すと外れる) →
//! 作り方(外側/中央/内側)を選ぶ → Enter で確定、1回の取り消しで消える。
//! Esc でやめれば何も作らない。
//!
//! HP-TH-01〜08 の選択は実際に拾う道(`SelectAt` 経由の `ClickOnAnyGuideSurface`)。
//! 添付文書の任意試験 HP-TH-OWNER は、対象の形状ガイドIDで選択を指定する。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2GeometryJob.h"
#include "V2HeavyJobFixture.h"
#include "V2ThickenDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/ThickenInputState.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/fabrication/FabricationSettings.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include <QFile>
#include <QByteArray>
#include <QIODevice>

#include <QPointF>
#include <QString>
#include <QApplication>
#include <QPushButton>
#include <QTimer>
#include <QObject>
#include <QElapsedTimer>
#include <QProcess>
#include <QDir>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QKeyEvent>
#include <QEvent>
#include <QChar>
#include <QComboBox>
#include <iostream>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::fabrication::ThicknessPlacement;

//! 何も無いところから、矩形 → 境界を拾う → 面を確定、まで(HumanPathApprox と同じ下ごしらえ)。
[[nodiscard]] bool MakeFreshGuideSurface(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))
        || !Explain("境界を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.RunCommand("surface.create");
    return Explain("面の下見が見える", window.SurfacePreviewShown())
        && Explain("Enterで面を確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        && Explain("形状ガイドができる", CountOfKind(window, EntityKind::GuideSurface) == 1);
}

//! 面を作ったあと、選択を外して部品モードへ、「厚み」を構えるところまで。
[[nodiscard]] bool ArmThickenOnFreshSurface(V2MainWindow& window)
{
    if (!MakeFreshGuideSurface(window)) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);   // 空所を押して選択を外す
    window.SetMode(UiMode::Part);
    window.RunCommand("part.thicken");
    return Explain("厚みを押すと棚が構える",
               window.ThickenShelfShown() && window.ShelfShown(Shelf::Thicken))
        && Explain("構えただけでは部品は増えない",
            CountOfKind(window, EntityKind::Part) == 0);
}

//! HP-TH-01。面が3Dで拾えて欄に入り、作り方を選んで Enter すると部品になり、
//! 1回の取り消しで消える。
[[nodiscard]] bool CaseHumanPathThickenPickPlacementConfirm(V2MainWindow& window)
{
    if (!ArmThickenOnFreshSurface(window)) {
        return false;
    }
    if (!Explain("構えてから面を画面で拾える", ClickOnAnyGuideSurface(window))
        || !Explain("面の欄に入る", !window.ThickenInput().surfaces.empty())
        || !Explain("欄に名前が出る",
            window.ThickenDock().SurfaceTextJa() != QStringLiteral("(選んでいません)"))) {
        return false;
    }
    bool sawSurface = false;
    for (const auto& label : window.Viewport().ToolRoleLabels()) {
        sawSurface = sawSurface || label.text == QStringLiteral("SURFACE");
    }
    if (!Explain("3D に SURFACE の札が出る", sawSurface)) {
        return false;
    }
    if (!Explain("「内側」のカードを押せる",
            window.ThickenDock().ClickPlacementCard(ThicknessPlacement::Inside))
        || !Explain("作り方が内側になる、平面までではない",
            window.ThickenInput().placement == ThicknessPlacement::Inside
                && !window.ThickenInput().toPlane)) {
        return false;
    }
    if (!Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("部品が1つできる", CountOfKind(window, EntityKind::Part) == 1)
        || !Explain("確定すると棚が引っ込む",
            !window.ThickenShelfShown() && !window.ShelfShown(Shelf::Thicken))) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1回の取り消しで部品が消える", CountOfKind(window, EntityKind::Part) == 0);
}

//! HP-TH-02。Esc は何も作らずにやめられ、同じ面をもう一度押すと欄が空になる。
[[nodiscard]] bool CaseHumanPathThickenEscAndRepick(V2MainWindow& window)
{
    if (!ArmThickenOnFreshSurface(window)) {
        return false;
    }
    if (!Explain("構えてから面を画面で拾える", ClickOnAnyGuideSurface(window))
        || !Explain("面の欄に入る", !window.ThickenInput().surfaces.empty())) {
        return false;
    }
    if (!Explain("Escでやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))
        || !Explain("厚みの道具を維持する", window.ThickenShelfShown())
        || !Explain("何も作っていない", CountOfKind(window, EntityKind::Part) == 0)) {
        return false;
    }
    // もう一度構えて、同じ面を押すと空になる(3D で押し直すと外れる)。
    window.RunCommand("part.thicken");
    if (!Explain("もう一度構えられる", window.ThickenShelfShown())
        || !Explain("同じ面をまた拾える", ClickOnAnyGuideSurface(window))
        || !Explain("面の欄に入る", !window.ThickenInput().surfaces.empty())) {
        return false;
    }
    // 押し直すと外れるので、ClickOnAnyGuideSurface は「選択に残っているか」で偽になる。
    // ここでは戻り値を見ず、欄が空になったことだけを見る。
    (void)ClickOnAnyGuideSurface(window);
    return Explain("同じ面を押し直すと欄が空になる", window.ThickenInput().surfaces.empty());
}

//! その線から平面の面を作る(線を拾う → 面を作る → Enter)。
[[nodiscard]] bool PlaneFromWire(V2MainWindow& window, const kachakacha::v2::base::EntityId& wire)
{
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    if (!ClickOnCurveOf(window, wire)) {
        return false;
    }
    window.RunCommand("surface.create");
    return window.SurfacePreviewShown() && window.HandleToolKey(Qt::Key_Return, nullptr);
}

//! その面の塗りの真ん中を実際に押す。
[[nodiscard]] bool ClickSurfaceMiddle(V2MainWindow& window, const kachakacha::v2::base::EntityId& id)
{
    auto& viewport = window.Viewport();
    for (const auto& shape : viewport.ShapeViews()) {
        if (shape.entityId != id || shape.mesh.Empty()) {
            continue;
        }
        const auto screen = viewport.Mapping().Project((shape.mesh.minimum + shape.mesh.maximum) * 0.5);
        if (!screen.has_value()) {
            return false;
        }
        viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        return true;
    }
    return false;
}

//! HP-TH-03。面を 2 枚押すと両方が欄に入り、Enter で部品が 2 個でき、1 回の取り消しで両方消える。
[[nodiscard]] bool CaseHumanPathThickenManySurfaces(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const auto left = DrawRectangleAtByHand(window, 0.15, 0.35, 0.40, 0.65);
    const auto right = DrawRectangleAtByHand(window, 0.60, 0.35, 0.85, 0.65);
    if (!Explain("矩形を 2 つ引ける", !left.IsNil() && !right.IsNil())
        || !Explain("面が 2 枚できる", PlaneFromWire(window, left) && PlaneFromWire(window, right)
                && CountOfKind(window, EntityKind::GuideSurface) == 2)) {
        return false;
    }
    const auto surfaces = kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface).entityIds;
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    window.SetMode(UiMode::Part);
    window.RunCommand("part.thicken");
    if (!Explain("厚みを押すと棚が構える", window.ThickenShelfShown())
        || !Explain("1 枚目を押せる", ClickSurfaceMiddle(window, surfaces[0]))
        || !Explain("2 枚目を押せる", ClickSurfaceMiddle(window, surfaces[1]))
        || !Explain("2 枚とも欄に入る(面は 1 枚に限らない)",
            window.ThickenInput().surfaces.size() == 2)) {
        return false;
    }
    if (!Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("部品が 2 個できる", CountOfKind(window, EntityKind::Part) == 2)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1 回の取り消しで 2 個とも消える", CountOfKind(window, EntityKind::Part) == 0);
}

bool CaseThickenPreviewOwnership(V2MainWindow& window)
{
    if(!ArmThickenOnFreshSurface(window))return false;
    const auto baseline=kachakacha::v2::kernel::CachedShapeCount();
    if(!ClickOnAnyGuideSurface(window))return false;
    for(int i=0;i<8;++i) {
        const auto placement=i%2?ThicknessPlacement::Inside:ThicknessPlacement::Outside;
        if(!window.ThickenDock().ClickPlacementCard(placement))return false;
        if(!Explain("only current thickness preview is retained",
            kachakacha::v2::kernel::CachedShapeCount()==baseline+1))return false;
    }
    if(!window.HandleToolKey(Qt::Key_Escape,nullptr))return false;
    if(!Explain("cancel releases thickness preview and preserves the guide",
        kachakacha::v2::kernel::CachedShapeCount()==baseline&&CountOfKind(window,EntityKind::Part)==0))return false;
    if(!ClickOnAnyGuideSurface(window)||!window.ThickenDock().ClickConfirm())return false;
    if(!Explain("retry commits a usable part",CountOfKind(window,EntityKind::Part)==1))return false;
    window.RunCommand("edit.undo");window.RunCommand("edit.redo");
    return Explain("committed shape survives undo and redo",CountOfKind(window,EntityKind::Part)==1
        &&window.RebuildProblems().isEmpty());
}

bool CaseCancelRunningThickness(V2MainWindow& window)
{
    if(!ArmThickenOnFreshSurface(window))return false;
    // Use a new value so this test actually starts a process rather than
    // correctly reusing an identical, already validated preview.
    auto* amount=window.findChild<QDoubleSpinBox*>("thickenAmount");if(!amount)return false;amount->setValue(.731);
    const auto revision=window.Session().GetDocument().Revision();
    const auto baseline=kernel::CachedShapeCount();
    int heartbeats=0;bool cancelled=false;QElapsedTimer elapsed;elapsed.start();
    QTimer timer;timer.setInterval(1);
    QObject::connect(&timer,&QTimer::timeout,&window,[&]{
        if(!V2GeometryJob::Active())return;
        ++heartbeats;
        auto* dialog=window.findChild<QDialog*>("geometryJobDialog");
        if(!dialog||dialog->objectName()!="geometryJobDialog"||dialog->property("calculationPid").toLongLong()==0)return;
        // A queued command must not replace the document during the modal job.
        window.RunCommand("file.new");
        const auto report=qEnvironmentVariable("KACHACAD_JOB_REPORT_DIR");
        if(!report.isEmpty()&&!cancelled){QDir().mkpath(report);dialog->grab().save(report+"/cancel-progress.png");}
        auto* button=dialog->findChild<QPushButton*>("geometryJobCancel");
        if(button&&button->isEnabled()){cancelled=true;button->click();}
    });
    timer.start();const bool picked=ClickOnAnyGuideSurface(window);timer.stop();
    if(!Explain("running thickness cancels while UI timer remains responsive",picked&&cancelled&&heartbeats>0&&elapsed.elapsed()<10000))return false;
    if(!Explain("cancel keeps document and shape cache unchanged",window.Session().GetDocument().Revision()==revision
        &&CountOfKind(window,EntityKind::Part)==0&&kernel::CachedShapeCount()==baseline&&!V2GeometryJob::Active()))return false;
    if(!Explain("stopped job leaves a reason and no confirmable result",window.ThickenDock().StatusTextJa().contains(QStringLiteral("取り消"))
        &&!window.ThickenDock().ClickConfirm()))return false;
    if(!window.ThickenDock().ClickPlacementCard(ThicknessPlacement::Inside)||!window.ThickenDock().ClickConfirm())return false;
    window.RunCommand("edit.undo");window.RunCommand("edit.redo");
    return Explain("retry after process cancel commits and survives save/reopen",CountOfKind(window,EntityKind::Part)==1
        &&window.SaveAndReopen(QStringLiteral("cancelled-thicken-retry.kcd2"))&&window.RebuildProblems().isEmpty());
}
bool ExerciseComplexSurfaceStop(V2MainWindow& window,bool crash)
{
    const auto source=HeavySmoothSurface();if(!source.HasValue())return false;
    const auto before=kernel::CaptureOutputShape(source.Value());const auto count=kernel::CachedShapeCount();
    const auto revision=window.Session().GetDocument().Revision();
    QTimer timer;timer.setInterval(5);QElapsedTimer running,elapsed;elapsed.start();bool cancelled=false;int ticks=0;
    QObject::connect(&timer,&QTimer::timeout,&window,[&]{
        auto* dialog=QApplication::activeModalWidget();
        if(!dialog||dialog->objectName()!="geometryJobDialog")return;++ticks;
        if(dialog->property("calculationPhase").toString().contains(QStringLiteral("厚みを計算"))&&!running.isValid())running.start();
        if(!running.isValid()||running.elapsed()<150||cancelled)return;
        const auto report=qEnvironmentVariable("KACHACAD_JOB_REPORT_DIR");
        if(!report.isEmpty()){QDir().mkpath(report);dialog->grab().save(report+(crash?"/complex-surface-crash.png":"/complex-surface-calculating.png"));}
        if(crash) {
            auto* child=dialog->findChild<QProcess*>("geometryJobProcess");
            if(child&&child->state()==QProcess::Running){cancelled=true;child->kill();}
        } else {
            auto* button=dialog->findChild<QPushButton*>("geometryJobCancel");
            if(button&&button->isEnabled()){cancelled=true;button->click();}
        }
    });
    timer.start();const auto result=V2GeometryJob::Thicken(&window,source.Value(),.8,ThicknessPlacement::Inside,std::nullopt,{});timer.stop();
    const auto after=kernel::CaptureOutputShape(source.Value());
    const bool unchanged=before.HasValue()&&after.HasValue()&&before.Value()==after.Value();
    const bool ok=cancelled&&!result.HasValue()&&result.FirstDiagnostic().code==(crash?"JOB-001":"JOB-002")
        &&ticks>10&&elapsed.elapsed()<10000&&unchanged&&kernel::CachedShapeCount()==count
        &&window.Session().GetDocument().Revision()==revision&&!V2GeometryJob::Active();
    std::cout<<"complex smooth surface "<<(crash?"crash":"cancellation")<<": elapsed="<<elapsed.elapsed()<<" ms, UI ticks="<<ticks<<'\n';
    if(result.HasValue())kernel::ReleaseShape(result.Value().handle);
    kernel::ReleaseShape(source.Value());
    if(!Explain("actual smooth offset stops during calculation without changing source",ok))return false;
    if(!ArmThickenOnFreshSurface(window)||!ClickOnAnyGuideSurface(window)||!window.ThickenDock().ClickConfirm())return false;
    return Explain("normal operation and saved document remain usable after interrupted heavy job",
        window.SaveAndReopen(QStringLiteral("complex-job-recovery.kcd2"))&&window.RebuildProblems().isEmpty());
}
bool CaseCancelComplexSurface(V2MainWindow& window){return ExerciseComplexSurfaceStop(window,false);}
bool CaseFailedThicknessProcess(V2MainWindow& window){return ExerciseComplexSurfaceStop(window,true);}
bool CaseInteractiveThicknessChanges(V2MainWindow& window)
{
    if(!ArmThickenOnFreshSurface(window))return false;
    // Earlier cases may have validated the same rectangle/default amount.
    // Start with a distinct input to exercise a running calculation.
    auto* initial=window.findChild<QDoubleSpinBox*>("thickenAmount");if(!initial)return false;initial->setValue(.617);
    const auto revision=window.Session().GetDocument().Revision();
    bool changed=false,enabled=false,moved=false,consistent=false,decimal=false;
    QTimer timer;timer.setInterval(1);
    QObject::connect(&timer,&QTimer::timeout,&window,[&]{
        auto* dialog=window.findChild<QDialog*>("geometryJobDialog");
        if(changed||!dialog||dialog->property("calculationPid").toLongLong()==0)return;
        changed=true;enabled=window.isEnabled()&&window.Viewport().isEnabled()
            &&window.ThickenDock().isEnabled()&&QApplication::activeModalWidget()==nullptr;
        consistent=window.ThickenDock().StatusTextJa().contains(QStringLiteral("計算中"))
            &&!window.ThickenDock().StatusTextJa().contains(QStringLiteral("作れません"))
            &&!window.ThickenDock().ClickConfirm()&&window.Session().GetDocument().Revision()==revision;
        window.RunCommand("view.fit_all");
        window.Viewport().SetViewDirection(ViewDirection::Front);moved=window.Viewport().Direction()==ViewDirection::Front;
        (void)window.ThickenDock().ClickCorrection(true);
        auto* amount=window.findChild<QDoubleSpinBox*>("thickenAmount");
        if(amount) {
            amount->selectAll();
            for(const auto c:QStringLiteral("0.035")) {
                QKeyEvent key(QEvent::KeyPress,c==QChar('.')?Qt::Key_Period:Qt::Key_0+c.digitValue(),Qt::NoModifier,QString(c));
                QApplication::sendEvent(amount,&key);
            }
            decimal=amount->text().startsWith("0.035")&&amount->value()==.617;
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(amount,&enter);
        }
        (void)window.ThickenDock().ClickPlacementCard(ThicknessPlacement::Outside);
    });
    timer.start();const bool picked=ClickOnAnyGuideSurface(window);timer.stop();
    if(!Explain("parameters and viewport stay enabled during real calculation",picked&&changed&&enabled&&moved&&consistent&&decimal))return false;
    if(!Explain("latest input replaces cancelled generation without committing",window.ThickenInput().thicknessMm==.035
        &&window.ThickenInput().placement==ThicknessPlacement::Outside&&window.ThickenInput().correctOpposite&&window.Session().GetDocument().Revision()==revision
        &&CountOfKind(window,EntityKind::Part)==0&&!window.Viewport().ToolPreview().empty()))return false;
    if(!window.ThickenDock().ClickPlacementCard(ThicknessPlacement::Inside))return false;
    int jobs=0;QTimer observed;observed.setInterval(1);
    QObject::connect(&observed,&QTimer::timeout,&window,[&]{if(V2GeometryJob::Active())++jobs;});
    QElapsedTimer repeat;repeat.start();observed.start();
    const bool reused=window.ThickenDock().ClickPlacementCard(ThicknessPlacement::Outside);observed.stop();
    if(!Explain("identical validated preview is reused without starting a process",reused&&jobs==0&&repeat.elapsed()<1000))return false;
    if(!window.ThickenDock().ClickConfirm())return false;
    bool correct=false;
    for(const auto& f:window.Session().GetDocument().Snapshot().features)
        if(const auto* d=std::get_if<domain::ThickenSurfaceDefinition>(&f.definition))correct=d->thickness.value==.035&&d->placement==0&&d->correctOpposite;
    window.RunCommand("edit.undo");window.RunCommand("edit.redo");
    window.Viewport().SetViewDirection(ViewDirection::Isometric);
    return Explain("only the latest thickness is committed and survives undo redo",correct
        &&CountOfKind(window,EntityKind::Part)==1&&window.RebuildProblems().isEmpty());
}

bool CaseCorrectionButtons(V2MainWindow& window)
{
    if(!ArmThickenOnFreshSurface(window)||!ClickOnAnyGuideSurface(window))return false;
    const auto revision=window.Session().GetDocument().Revision();
    if(!window.ThickenDock().ClickCorrection(true)||!window.ThickenInput().correctOpposite)return false;
    if(!window.ThickenDock().ClickCorrection(false)||window.ThickenInput().correctOpposite)return false;
    if(!window.ThickenDock().ClickCorrection(true))return false;
    QApplication::processEvents();
    const auto* status=window.findChild<QLabel*>("thickenStatus");
    if(!Explain("result lines have enough visible height",status&&status->height()>=status->heightForWidth(status->width())))return false;
    if(!window.ThickenDock().ClickConfirm())return false;
    bool recorded=false;
    for(const auto& feature:window.Session().GetDocument().Snapshot().features)
        if(const auto* definition=std::get_if<domain::ThickenSurfaceDefinition>(&feature.definition))recorded=definition->correctOpposite;
    if(!Explain("correction mode is explicit and committed once",recorded&&window.Session().GetDocument().Revision()>revision))return false;
    window.RunCommand("edit.undo");if(CountOfKind(window,EntityKind::Part)!=0)return false;
    window.RunCommand("edit.redo");
    if(!window.SaveAndReopen(QStringLiteral("correction-option-roundtrip.kcd2")))return false;
    recorded=false;
    for(const auto& feature:window.Session().GetDocument().Snapshot().features)
        if(const auto* definition=std::get_if<domain::ThickenSurfaceDefinition>(&feature.definition))recorded=definition->correctOpposite;
    return Explain("correction flag survives native rebuilt save",recorded&&window.RebuildProblems().isEmpty()&&CountOfKind(window,EntityKind::Part)==1);
}
bool CaseThickenOutput(V2MainWindow& window,int mode,int placement)
{
    if(!MakeFreshGuideSurface(window))return false;
    if(placement==3) {
        WorkPlaneChoice plane;plane.method=modeling::WorkPlaneMethod::PointNormal;
        plane.origin={0,0,3};plane.name="厚み終端試験";
        window.Viewport().SetSelection({});window.CreateWorkPlaneFromChoice(plane,false);
    }
    window.Viewport().SetSelection({});window.SetMode(UiMode::Part);
    window.RunCommand(placement==3?"part.thicken_to_plane":"part.thicken");
    auto* output=window.findChild<QComboBox*>("thickenOutput");
    auto* amount=window.findChild<QDoubleSpinBox*>("thickenAmount");
    if(!output||!output->isVisible()||!amount||!ClickOnAnyGuideSurface(window))return false;
    amount->setValue(.125);
    if(placement==3) {
        if(!window.ThickenDock().ClickToPlaneCard())return false;
        auto* target=window.findChild<QComboBox*>("thickenTarget");
        if(!target||!target->isVisible())return false;
        const int at=target->findText(QStringLiteral("厚み終端試験"));if(at<0)return false;
        target->setCurrentIndex(at);
    } else if(!window.ThickenDock().ClickPlacementCard(static_cast<ThicknessPlacement>(placement)))return false;
    const auto before=window.Session().GetDocument().Snapshot();
    const int oldWires=CountOfKind(window,EntityKind::Wire);
    const auto revision=window.Session().GetDocument().Revision();
    int jobs=0;QTimer observed;observed.setInterval(5);
    QObject::connect(&observed,&QTimer::timeout,&window,[&]{if(V2GeometryJob::Active())++jobs;});
    observed.start();QElapsedTimer elapsed;elapsed.start();
    for(int pick:{0,1,2,mode}) {
        output->setCurrentIndex(pick);
        if(!Explain("preview shading follows output choice",(window.Viewport().ToolPreviewFaceCount()>0)==(pick!=1)))return false;
    }
    observed.stop();
    if(!Explain("output choice does not calculate or edit document",jobs==0&&elapsed.elapsed()<500
        &&window.Session().GetDocument().Revision()==revision)||!window.ThickenDock().ClickConfirm())return false;
    const int parts=mode==1?0:1;
    const int wires=CountOfKind(window,EntityKind::Wire);
    if(!Explain("only requested output entities are committed",CountOfKind(window,EntityKind::Part)==parts
        &&(mode==0?wires==oldWires:wires>oldWires)&&CountOfKind(window,EntityKind::GuideSurface)==1))return false;
    window.RunCommand("edit.undo");
    const auto undone=window.Session().GetDocument().Snapshot();
    const bool same=undone.entities.size()==before.entities.size()&&std::equal(before.entities.begin(),before.entities.end(),undone.entities.begin(),
        [](const auto& a,const auto& b){return a.id==b.id&&a.kind==b.kind&&a.visibility==b.visibility&&a.displayName==b.displayName;});
    if(!Explain("one undo restores every original entity",same
        &&CountOfKind(window,EntityKind::Wire)==oldWires&&CountOfKind(window,EntityKind::Part)==0))return false;
    window.RunCommand("edit.redo");
    return Explain("requested solids and native boundary curves survive save",window.SaveAndReopen(QStringLiteral("thicken-output.kcd2"))
        &&window.RebuildProblems().isEmpty()&&CountOfKind(window,EntityKind::Part)==parts&&CountOfKind(window,EntityKind::Wire)==wires);
}
bool CaseThickenOutputMatrix(V2MainWindow& window)
{
    for(auto theme:{UiTheme::Normal,UiTheme::Windows95}) {
        window.ApplyTheme(theme);
        for(int placement:{0,1,2,3})for(int output:{0,1,2})
            if(!CaseThickenOutput(window,output,placement)){window.ApplyTheme(UiTheme::Normal);return false;}
    }
    window.ApplyTheme(UiTheme::Normal);return true;
}
bool CaseJigOutputMatrix(V2MainWindow& window)
{
    for(auto theme:{UiTheme::Normal,UiTheme::Windows95})for(int mode:{0,1,2}) {
        std::cout<<"jig output mode="<<mode<<" theme="<<int(theme)<<" setup"<<std::endl;
        window.ApplyTheme(theme);if(!MakeFreshGuideSurface(window))return false;
        std::cout<<"jig source ready"<<std::endl;
        window.Viewport().SetSelection(app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),EntityKind::GuideSurface));
        window.SetMode(UiMode::Part);window.ActivateCommand("part.surface_jig");
        std::cout<<"jig panel ready"<<std::endl;
        auto* output=window.findChild<QComboBox*>("jigOutput");if(!output||!output->isVisible())return false;
        output->setCurrentIndex(mode);
        (void)window.ParameterDock().Apply(app::ParameterId::JigThicknessMm,QStringLiteral("0.125"));
        (void)window.ParameterDock().Apply(app::ParameterId::JigClearanceMm,QStringLiteral("0.025"));
        const int oldWires=CountOfKind(window,EntityKind::Wire);
        std::cout<<"jig confirming"<<std::endl;
        if(!window.HandleToolKey(Qt::Key_Return,nullptr))return false;
        std::cout<<"jig confirmed"<<std::endl;
        const int parts=mode==1?0:1,wires=CountOfKind(window,EntityKind::Wire);
        if(!Explain("jig generates requested native solid or independent boundaries",CountOfKind(window,EntityKind::Part)==parts
            &&(mode==0?wires==oldWires:wires>oldWires)))return false;
        window.RunCommand("edit.undo");
        std::cout<<"jig undo ready"<<std::endl;
        if(CountOfKind(window,EntityKind::Part)!=0||CountOfKind(window,EntityKind::Wire)!=oldWires)return false;
        window.RunCommand("edit.redo");
        std::cout<<"jig redo ready"<<std::endl;
        if(!window.SaveAndReopen(QStringLiteral("jig-output.kcd2"))||!window.RebuildProblems().isEmpty()
            ||CountOfKind(window,EntityKind::Part)!=parts||CountOfKind(window,EntityKind::Wire)!=wires)return false;
    }
    window.ApplyTheme(UiTheme::Normal);return true;
}
bool CaseCorrectionThemes(V2MainWindow& window)
{
    for(const auto theme:{UiTheme::Normal,UiTheme::Windows95}) {
        window.ApplyTheme(theme);
        if(!CaseCorrectionButtons(window)){window.ApplyTheme(UiTheme::Normal);return false;}
    }
    window.ApplyTheme(UiTheme::Normal);return true;
}

bool CaseOwnerCorrectedThickness(V2MainWindow& window)
{
    const auto path=qEnvironmentVariable("KACHACAD_THICKEN_TEST_FILE");
    const auto directory=qEnvironmentVariable("KACHACAD_THICKEN_TEST_OUTPUT");
    QFile sourceFile(path);if(!sourceFile.open(QIODevice::ReadOnly))return false;
    const auto original=sourceFile.readAll();sourceFile.close();
    if(!directory.isEmpty())QDir().mkpath(directory);
    for(const auto placement:{ThicknessPlacement::Inside,ThicknessPlacement::Centered}) {
        if(!window.OpenDocumentFile(path))return false;
        base::EntityId source;
        for(const auto& feature:window.Session().GetDocument().Snapshot().features)
            if(const auto* d=std::get_if<domain::CreateGuideSurfaceDefinition>(&feature.definition);d&&d->method==5)
                source=feature.outputs.front().entityId;
        if(source.IsNil())return false;
        const auto before=window.Session().GetDocument().Snapshot();const auto parts=CountOfKind(window,EntityKind::Part);
        window.resize(1600,1000);window.SetMode(UiMode::Part);window.Viewport().SetViewDirection(ViewDirection::Isometric);window.Viewport().FitToDocument();window.Viewport().SetSelection({});
        window.RunCommand("part.thicken");auto* amount=window.findChild<QDoubleSpinBox*>("thickenAmount");if(!amount)return false;amount->setValue(.5);
        if(!window.ThickenDock().ClickPlacementCard(placement))return false;
        app::SelectionSet selected;selected.entityIds={source};window.Viewport().SetSelection(selected);
        if(!Explain("strict owner offset still refuses a focal fold",!window.ThickenDock().ClickConfirm()))return false;
        QElapsedTimer time;time.start();if(!window.ThickenDock().ClickCorrection(true))return false;
        const auto status=window.ThickenDock().StatusTextJa();
        std::cout<<"owner corrected placement="<<int(placement)<<" elapsed="<<time.elapsed()<<" ms "<<status.toStdString()<<std::endl;
        if(!Explain("owner corrected preview reports actual measured deviations",status.contains(QStringLiteral("反対面を補正"))
            &&status.contains(QStringLiteral("法線厚み"))&&window.Session().GetDocument().Snapshot().features.size()==before.features.size()))return false;
        amount->setValue(0);int repeatedJobs=0;QTimer observed;observed.setInterval(1);
        QObject::connect(&observed,&QTimer::timeout,&window,[&]{if(V2GeometryJob::Active())++repeatedJobs;});
        observed.start();time.restart();amount->setValue(.5);observed.stop();
        std::cout<<"owner corrected reused="<<time.elapsed()<<" ms jobs="<<repeatedJobs<<std::endl;
        if(!Explain("corrected preview reuses checked result without another process",repeatedJobs==0&&time.elapsed()<2000))return false;
        const int oldWires=CountOfKind(window,EntityKind::Wire);
        auto* output=window.findChild<QComboBox*>("thickenOutput");if(!output||!output->isVisible())return false;
        time.restart();repeatedJobs=0;observed.start();
        for(int mode:{0,1,2})output->setCurrentIndex(mode);
        observed.stop();std::cout<<"owner output switches="<<time.elapsed()<<" ms jobs="<<repeatedJobs<<std::endl;
        if(!Explain("owner output choices do not rebuild the native wall",repeatedJobs==0&&time.elapsed()<500))return false;
        const auto stem=placement==ThicknessPlacement::Inside?QStringLiteral("inside"):QStringLiteral("centered");
        QApplication::processEvents();
        const auto* shown=window.findChild<QLabel*>("thickenStatus");
        if(!Explain("owner deviation lines are not clipped",shown&&shown->height()>=shown->heightForWidth(shown->width())))return false;
        if(!directory.isEmpty())window.grab().save(directory+"/corrected-"+stem+"-preview.png");
        const bool confirmed=window.ThickenDock().ClickConfirm();
        std::cout<<"owner both confirmed="<<confirmed<<" parts="<<CountOfKind(window,EntityKind::Part)<<" wires="<<CountOfKind(window,EntityKind::Wire)
            <<" status="<<window.StatusText().toStdString()<<std::endl;
        if(!confirmed||CountOfKind(window,EntityKind::Part)!=parts+1
            ||CountOfKind(window,EntityKind::Wire)<=oldWires)return false;
        bool stored=false;
        for(const auto& feature:window.Session().GetDocument().Snapshot().features)
            if(const auto* d=std::get_if<domain::ThickenSurfaceDefinition>(&feature.definition);d&&d->surface==source)
                stored=stored||(d->correctOpposite&&d->thickness.value==.5&&d->placement==int(placement));
        if(!stored)return false;
        QApplication::processEvents();
        if(!directory.isEmpty())window.grab().save(directory+"/corrected-"+stem+"-result.png");
        const int savedWires=CountOfKind(window,EntityKind::Wire);
        window.RunCommand("edit.undo");if(CountOfKind(window,EntityKind::Part)!=parts||CountOfKind(window,EntityKind::Wire)!=oldWires)return false;
        window.RunCommand("edit.redo");if(CountOfKind(window,EntityKind::Part)!=parts+1||!window.RebuildProblems().isEmpty())return false;
        if(!window.SaveAndReopen("owner-corrected-"+stem+".kcd2")||!window.RebuildProblems().isEmpty()
            ||CountOfKind(window,EntityKind::Wire)!=savedWires)return false;
        // Persist a reviewable result outside the temporary test document.
        if(!directory.isEmpty()) {window.SetPathChooser([directory,stem](bool){return directory+"/corrected-"+stem+".kcd2";});window.RunCommand("file.save_as");}
    }
    if(!sourceFile.open(QIODevice::ReadOnly))return false;
    return Explain("owner source bytes were never changed",sourceFile.readAll()==original);
}
bool CaseOwnerThickness(V2MainWindow& window)
{
    if(!window.OpenDocumentFile(qEnvironmentVariable("KACHACAD_THICKEN_TEST_FILE")))return false;
    const auto directory=qEnvironmentVariable("KACHACAD_THICKEN_TEST_OUTPUT");
    if(!directory.isEmpty())QDir().mkpath(directory);
    base::EntityId source;
    for(const auto& feature:window.Session().GetDocument().Snapshot().features) {
        const auto* definition=std::get_if<domain::CreateGuideSurfaceDefinition>(&feature.definition);
        if(!definition)continue;
        if(definition->method==5)source=feature.outputs.front().entityId;
        const auto table=app::GuideTableFromDefinition(window.Session().GetDocument(),window.Session().Scene(),*definition);
        if(!table.HasValue())return false;
        const auto request=modeling::ToGuideSurfaceRequest(table.Value(),{});if(!request.HasValue())return false;
        const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request.Value(),{});if(!analysis.HasValue())return false;
        const auto built=kernel::BuildGuideSurface(request.Value(),analysis.Value(),{});if(!built.HasValue())return false;
        const auto captured=kernel::CaptureOutputShape(built.Value().handle);if(!captured.HasValue())return false;
        if(!directory.isEmpty()) {
            const auto name=QString::fromStdString(feature.outputs.front().entityId.ToString());
            QFile file(directory+"/"+name+".brep");if(!file.open(QIODevice::WriteOnly))return false;
            file.write(QByteArray::fromStdString(captured.Value()));file.close();
        }
        kernel::ReleaseShape(built.Value().handle);
    }
    if(source.IsNil())return false;
    std::cout<<"owner selected source="<<source.ToString()<<std::endl;
    window.resize(1600,1000);window.SetMode(UiMode::Part);window.Viewport().FitToDocument();
    (void)window.ParameterDock().Apply(app::ParameterId::ThickenThicknessMm,QStringLiteral("6"));
    window.Viewport().SetSelection({});window.RunCommand("part.thicken");
    if(!window.ThickenDock().ClickPlacementCard(ThicknessPlacement::Centered))return false;
    const auto revision=window.Session().GetDocument().Revision();QElapsedTimer time;time.start();
    app::SelectionSet selection;selection.entityIds={source};window.Viewport().SetSelection(selection);
    std::cout<<"owner centered 6 mm: "<<time.elapsed()<<" ms\n";
    if(!Explain("invalid owner thickness reports within five seconds without changing document",time.elapsed()<5000
        &&window.Session().GetDocument().Revision()==revision&&!window.ThickenDock().ClickConfirm()))return false;
    if(!directory.isEmpty())window.grab().save(directory+"/six-mm-refusal.png");
    bool edited=false,responsive=false;QTimer timer;timer.setInterval(5);
    QObject::connect(&timer,&QTimer::timeout,&window,[&]{
        auto* dialog=window.findChild<QDialog*>("geometryJobDialog");
        if(edited||!dialog||dialog->property("calculationPid").toLongLong()==0)return;
        edited=true;responsive=window.isEnabled()&&window.Viewport().isEnabled()&&window.ThickenDock().isEnabled();
        window.Viewport().SetViewDirection(ViewDirection::Isometric);
        (void)window.ThickenDock().ClickPlacementCard(ThicknessPlacement::Outside);
        if(!directory.isEmpty())window.grab().save(directory+"/editable-during-calculation.png");
    });
    auto* amount=window.findChild<QDoubleSpinBox*>("thickenAmount");if(!amount)return false;
    timer.start();time.restart();amount->setValue(.5);timer.stop();
    std::cout<<"owner editable outside .5 mm: "<<time.elapsed()<<" ms\n";
    std::cout<<"owner changed="<<edited<<" responsive="<<responsive<<" placement="<<int(window.ThickenInput().placement)
        <<" status="<<window.ThickenDock().StatusTextJa().toStdString()<<std::endl;
    if(!Explain("owner calculation accepts changed input while viewport stays usable",edited&&responsive
        &&window.ThickenInput().placement==ThicknessPlacement::Outside&&window.Session().GetDocument().Revision()==revision))return false;
    // Clear the amount without starting another expensive cold calculation,
    // then restore the previously verified owner condition.
    amount->setValue(0);
    time.restart();amount->setValue(.5);
    std::cout<<"owner reused outside .5 mm: "<<time.elapsed()<<" ms\n";
    if(!Explain("owner verified preview is reusable within one second",time.elapsed()<1000&&window.ThickenDock().ClickConfirm()))return false;
    if(!directory.isEmpty())window.grab().save(directory+"/outside-half-mm-result.png");
    window.RunCommand("edit.undo");window.RunCommand("edit.redo");
    return Explain("owner valid solid survives undo redo and saved rebuild",window.RebuildProblems().isEmpty()
        &&window.SaveAndReopen(QStringLiteral("owner-thickness-performance.kcd2"))&&window.RebuildProblems().isEmpty());
}
bool CaseOwnerWireOutput(V2MainWindow& window)
{
    const auto path=qEnvironmentVariable("KACHACAD_THICKEN_TEST_FILE");
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;const auto original=file.readAll();file.close();
    if(!window.OpenDocumentFile(path))return false;
    base::EntityId source;
    for(const auto& f:window.Session().GetDocument().Snapshot().features)
        if(const auto* d=std::get_if<domain::CreateGuideSurfaceDefinition>(&f.definition);d&&d->method==5)source=f.outputs.front().entityId;
    if(source.IsNil())return false;
    const int parts=CountOfKind(window,EntityKind::Part),wires=CountOfKind(window,EntityKind::Wire);
    window.SetMode(UiMode::Part);window.Viewport().SetSelection({});window.RunCommand("part.thicken");
    auto* output=window.findChild<QComboBox*>("thickenOutput");auto* amount=window.findChild<QDoubleSpinBox*>("thickenAmount");
    if(!output||!amount)return false;
    output->setCurrentIndex(1);amount->setValue(.5);
    if(!window.ThickenDock().ClickPlacementCard(ThicknessPlacement::Inside)||!window.ThickenDock().ClickCorrection(true))return false;
    app::SelectionSet selected;selected.entityIds={source};window.Viewport().SetSelection(selected);
    if(!window.ThickenDock().ClickConfirm())return false;
    const int madeWires=CountOfKind(window,EntityKind::Wire);
    std::cout<<"owner wire-only parts="<<CountOfKind(window,EntityKind::Part)<<" old parts="<<parts<<" wires="<<madeWires<<" old wires="<<wires
        <<" status="<<window.StatusText().toStdString()<<std::endl;
    if(!Explain("actual corrected roof wire-only has native curves without a new part",CountOfKind(window,EntityKind::Part)==parts&&madeWires>wires))return false;
    window.RunCommand("edit.undo");if(CountOfKind(window,EntityKind::Wire)!=wires)return false;
    window.RunCommand("edit.redo");
    if(!window.SaveAndReopen(QStringLiteral("owner-wire-output.kcd2"))||!window.RebuildProblems().isEmpty()
        ||CountOfKind(window,EntityKind::Wire)!=madeWires||CountOfKind(window,EntityKind::Part)!=parts)return false;
    const auto directory=qEnvironmentVariable("KACHACAD_THICKEN_TEST_OUTPUT");
    if(!directory.isEmpty()){window.SetPathChooser([directory](bool){return directory+"/wire-only.kcd2";});window.RunCommand("file.save_as");}
    if(!file.open(QIODevice::ReadOnly))return false;
    return Explain("wire-only roof also leaves original KCD bytes unchanged",file.readAll()==original);
}
} // namespace

std::vector<SelfTestCase> ThickenCases()
{
    std::vector<SelfTestCase> cases{
        {"HP-TH-JIG-OUTPUT all jig outputs themes undo and saved results",CaseJigOutputMatrix},
        {"HP-TH-OUTPUT all outputs placements to-plane themes and atomic saved results",CaseThickenOutputMatrix},
        {"HP-TH-CORRECT explicit correction buttons and saved native rebuild",CaseCorrectionThemes},
        {"HP-TH-08 editable viewport and coalesced latest thickness preview",CaseInteractiveThicknessChanges},
        {"HP-TH-05 running job cancellation, UI response and saved retry",CaseCancelRunningThickness},
        {"HP-TH-06 crashed calculation process preserves document and supports retry",CaseFailedThicknessProcess},
        {"HP-TH-07 actual complex smooth surface cancellation during kernel calculation",CaseCancelComplexSurface},
        {"HP-TH-04 thickness preview release, cancel and retry",CaseThickenPreviewOwnership},
        {"HP-TH-03 面を 2 枚押すと両方に厚みが付き、1 回の取り消しで両方消える",
            CaseHumanPathThickenManySurfaces},
        {"HP-TH-01 面を3Dで拾い、作り方を選んで Enter すると部品になり、取り消しで消える",
            CaseHumanPathThickenPickPlacementConfirm},
        {"HP-TH-02 Esc は何も作らずにやめられ、同じ面を押し直すと欄が空になる",
            CaseHumanPathThickenEscAndRepick},
    };
    if(qEnvironmentVariableIsSet("KACHACAD_THICKEN_TEST_FILE"))cases.push_back({"HP-TH-OWNER actual owner surfaces",CaseOwnerThickness});
    if(qEnvironmentVariableIsSet("KACHACAD_THICKEN_TEST_FILE"))cases.push_back({"HP-TH-CORRECT-OWNER actual owner corrected inside and centered",CaseOwnerCorrectedThickness});
    if(qEnvironmentVariableIsSet("KACHACAD_THICKEN_TEST_FILE"))cases.push_back({"HP-TH-CORRECT-OWNER-WIRE actual owner wire-only saved boundaries",CaseOwnerWireOutput});
    return cases;
}

} // namespace kachakacha::v2::selftest
