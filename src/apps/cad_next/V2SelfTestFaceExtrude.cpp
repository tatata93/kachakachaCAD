#include "V2SelfTest.h"
#include "V2Calculation.h"
#include "V2ExtrudeDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/kernel/OcctShellSplit.h"
#include "kachakacha/geometry/WireChain.h"
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QMouseEvent>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QObject>
#include <QPointF>
#include <QPushButton>
#include <QTimer>
#include <QTemporaryDir>
#include <QString>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>

namespace kachakacha::v2::selftest {
namespace {
base::Result<modeling::GuideSurfaceResult> CurvedSource() {
    modeling::GuideSurfaceRequest request;
    request.method = modeling::GuideSurfaceMethod::RuledSections;
    for (int i = 0; i < 2; ++i) {
        modeling::GuideChain chain; chain.role = modeling::ChainRole::Section; chain.index = i + 1;
        const auto arc = geometry::CurveSegment::MakeCircularArc({0, 0, i * 20.0}, {0, 0, 1},
            {1, 0, 0}, 10, 0, geometry::kPi / 2);
        if (!arc.HasValue()) return base::Result<modeling::GuideSurfaceResult>::Failure(arc.Diagnostics());
        chain.segments = {arc.Value()}; request.chains.push_back(chain);
    }
    const auto analysis = modeling::AnalyzeGuideSurfaceRequest(request, {});
    if (!analysis.HasValue()) return base::Result<modeling::GuideSurfaceResult>::Failure(analysis.Diagnostics());
    return kernel::BuildGuideSurface(request, analysis.Value(), {});
}
bool ImportCurvedPart(V2MainWindow& window) {
    const auto surface = CurvedSource(); if (!surface.HasValue()) return false;
    const auto solid = kernel::BuildSurfaceExtrude(surface.Value().handle, {1, 1, 0}, 0, 2, {});
    kernel::ReleaseShape(surface.Value().handle);
    if (!solid.HasValue() || solid.Value().parts.size() != 1) return false;
    const auto bytes = kernel::CaptureOutputShape(solid.Value().parts[0].handle);
    kernel::ReleaseShape(solid.Value().parts[0].handle);
    if (!bytes.HasValue()) return false;
    io::DocumentFile file; file.snapshot = window.Session().GetDocument().Snapshot();
    domain::Feature feature; feature.id = *base::FeatureId::Parse("a0000000-0000-4000-8000-000000000001");
    feature.type = domain::FeatureType::FreezeDerived; feature.displayName = "曲面フェイス試験";
    domain::FreezeDerivedDefinition definition; definition.frozenBrep = bytes.Value(); feature.definition = definition;
    domain::Entity entity; entity.id = *base::EntityId::Parse("a0000000-0000-4000-8000-000000000002");
    entity.kind = domain::EntityKind::Part; entity.editPolicy = domain::EditPolicy::Frozen;
    entity.createdBy = feature.id; entity.displayName = feature.displayName;
    feature.outputs.push_back({"part", entity.id, entity.kind});
    file.snapshot.features.push_back(feature); file.snapshot.entities.push_back(entity);
    const auto archive = io::SaveDocument(file); QTemporaryDir folder;
    if (!archive.HasValue() || !folder.isValid()) return false;
    const auto path = folder.filePath("curved-face.kcd2");
    if (!io::WriteFileAtomically(path.toStdString(), archive.Value()).HasValue()) return false;
    return window.OpenDocumentFile(path);
}
bool PickCurvedFace(V2MainWindow& window) {
    auto& viewport = window.Viewport(); viewport.SetViewDirection(ViewDirection::Isometric);
    viewport.SetViewCenter({7, 7, 10}); viewport.SetVisibleWidthMm(50);
    window.RunCommand("part.extrude");
    const auto views = viewport.ShapeViews();
    for (const auto& view : views) for (const auto& triangle : view.mesh.triangles) {
        const auto center = (triangle.points[0] + triangle.points[1] + triangle.points[2]) * (1.0 / 3);
        const auto screen = viewport.Mapping().Project(center); if (!screen) continue;
        viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        if (viewport.ExtrudeHandleShown() && window.ExtrudeDock().ProfileTextJa().contains(QStringLiteral("曲面フェイス試験")))
            return true;
        window.HandleToolKey(Qt::Key_Escape, nullptr);
    }
    return false;
}
bool CaseNativeFace(V2MainWindow& window) {
    if (!Explain("曲面の部品を読み込める", ImportCurvedPart(window))) return false;
    const auto before = window.Session().GetDocument().Snapshot();
    if (!Explain("道具を選んでから曲面フェイスを拾える", PickCurvedFace(window))) return false;
    if (!Explain("下見では文書を変えない", before.features.size() == window.Session().GetDocument().Snapshot().features.size())) return false;
    window.HandleToolKey(Qt::Key_Escape, nullptr);
    if (!Explain("取消では元部品を保つ", CountOfKind(window, domain::EntityKind::Part) == 1)) return false;
    if (!PickCurvedFace(window)) return false;
    window.ExtrudeDock().TypeDistanceMm(1);
    if (!Explain("Enterでフェイスを押し出せる", window.HandleToolKey(Qt::Key_Return, nullptr)
        && CountOfKind(window, domain::EntityKind::Part) == 2)) return false;
    if (!Explain("曲面フェイスをネイティブ面として保存", CountOfKind(window, domain::EntityKind::GuideSurface) == 1)) return false;
    if (!Explain("フェイス確定後も押し出し入力待ち", window.Viewport().ToolPickActive()
        && window.Viewport().ProfileRegionPicking() && !window.Viewport().ExtrudeHandleShown())) return false;
    window.RunCommand("edit.undo");
    if (!Explain("1回のUndoで入力面も戻る", window.Session().GetDocument().Snapshot().features.size() == before.features.size())) return false;
    window.RunCommand("edit.redo");
    return Explain("保存再読込でフェイス押し出しが残る", window.SaveAndReopen("native-face-extrude.kcd2")
        && CountOfKind(window, domain::EntityKind::Part) == 2);
}
bool CaseNativeFaceToLine(V2MainWindow& window) {
    if (!ImportCurvedPart(window) || !PickCurvedFace(window)) return false;
    const auto origin = window.Viewport().ExtrudeHandleOrigin();
    const auto direction = window.Viewport().ExtrudeHandleDirection();
    window.HandleToolKey(Qt::Key_Escape, nullptr);
    const auto p = origin + direction * 10;
    const auto line = geometry::CurveSegment::MakeLine(p - geometry::Vector3{0, 0, 20}, p + geometry::Vector3{0, 0, 20});
    if (!line.HasValue()) return false;
    const auto added = window.Session().AddWire({line.Value()}, false, "終端の線");
    if (!added.committed || added.createdEntityIds.empty() || !PickCurvedFace(window)) return false;
    app::ExtrudeChoice choice; choice.direction = modeling::ExtrudeDirectionMode::CustomXYZ;
    choice.customDirection = direction; choice.extent = modeling::ExtrudeExtentMode::ToTarget;
    choice.targetEntityId = added.createdEntityIds.front(); choice.booleanMode = modeling::ExtrudeBooleanMode::NewPart;
    window.ApplyExtrudeChoice(choice);
    if (!Explain("線までを右ペインで選べる", window.ExtrudeDock().TargetRowShown()
        && window.ExtrudeDock().TargetEntityId() == choice.targetEntityId)) return false;
    if (!Explain("線までの形を確定できる", window.HandleToolKey(Qt::Key_Return, nullptr)
        && CountOfKind(window, domain::EntityKind::Part) == 2)) return false;
    return Explain("線への参照と面を保存して再計算できる", window.SaveAndReopen("native-face-to-line.kcd2")
        && CountOfKind(window, domain::EntityKind::Part) == 2);
}
bool CaseProgressCancellation(V2MainWindow& window) {
    const auto source = CurvedSource(); if (!source.HasValue()) return false;
    const auto count = kernel::CachedShapeCount(); const auto revision = window.Session().GetDocument().Revision();
    int pulses = 0; bool seen = false; QTimer timer; timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        ++pulses;
        auto* dialog = window.findChild<QDialog*>("calculationDialog");
        if (dialog && dialog->isVisible()) {
            seen = true; if (auto* button = dialog->findChild<QPushButton*>("calculationCancel")) button->click();
        }
    }); timer.start();
    const auto result = V2Calculation::Call(&window, "面の押し出しを計算しています…",
        [](auto handle) {
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
            return kernel::BuildSurfaceExtrude(handle, {1, 1, 0}, 0, 1, {});
        }, source.Value().handle);
    timer.stop(); kernel::ReleaseShape(source.Value().handle);
    return Explain("計算中もGUIが更新され取消できる", seen && pulses >= 10 && !result.HasValue())
        && Explain("取消で生成形状を残さない", kernel::CachedShapeCount() == count - 1)
        && Explain("計算の取消では文書を変更しない", revision == window.Session().GetDocument().Revision());
}
bool CaseSplitCancellation(V2MainWindow& window) {
    const auto source = CurvedSource(); if (!source.HasValue()) return false;
    const auto solid = kernel::BuildSurfaceExtrude(source.Value().handle, {1, 1, 0}, 0, 1, {});
    kernel::ReleaseShape(source.Value().handle);
    if (!solid.HasValue() || solid.Value().parts.size() != 1) return false;
    const auto count = kernel::CachedShapeCount(); bool seen = false;
    QTimer timer; timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto* dialog = window.findChild<QDialog*>("calculationDialog");
        if (dialog && dialog->isVisible()) {
            seen = true; if (auto* button = dialog->findChild<QPushButton*>("calculationCancel")) button->click();
        }
    }); timer.start();
    const auto result = V2Calculation::Call(&window, "部品を分割しています…", [](auto handle) {
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        return kernel::SplitSolidByPlane(handle, {0, 0, 10}, {0, 0, 1}, {});
    }, solid.Value().parts[0].handle);
    timer.stop();
    const bool clean = kernel::CachedShapeCount() == count;
    kernel::ReleaseShape(solid.Value().parts[0].handle);
    return Explain("分割の取消で両側の立体を解放", seen && !result.HasValue() && clean);
}

bool CaseExtrudeToolFirstSettings(V2MainWindow& window) {
    auto& viewport = window.Viewport();
    auto& dock = window.ExtrudeDock();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter({}); viewport.SetVisibleWidthMm(60);
    const auto circle = geometry::CurveSegment::MakeCircle({0,0,0}, {0,0,1}, {1,0,0}, 5);
    const auto line = geometry::CurveSegment::MakeLine({15,0,0}, {20,0,5});
    if (!circle.HasValue() || !line.HasValue()) return false;
    if (!window.Session().AddWire({circle.Value()}, false, "押す円").committed) return false;
    const auto added = window.Session().AddWire({line.Value()}, false, "方向線");
    if (!added.committed) return false;
    viewport.SetSelection({}); window.RunCommand("part.extrude");
    const auto* distance = window.findChild<QDoubleSpinBox*>("extrudeDistance");
    const auto* direction = window.findChild<QComboBox*>("extrudeDirection");
    if (!Explain("未選択でも距離・方向・入力ボタンが表示される", distance && !distance->isHidden()
        && direction && !direction->isHidden() && dock.ReselectProfileShown())) return false;
    dock.TypeDistanceMm(0.125);
    dock.PickDirection(modeling::ExtrudeDirectionMode::SelectedVector);
    auto* button = window.findChild<QPushButton*>("extrudePickDirection");
    if (!button || button->isHidden()) return false;
    button->click();dock.ChooseDirection(modeling::ExtrudeDirectionMode::WorldZ);
    if (!Explain("詳細からの方向変更でも隠れた線選択を残さない", !viewport.ReferencePickPending())) return false;
    dock.PickDirection(modeling::ExtrudeDirectionMode::SelectedVector);
    button->click(); window.SelectTool(modeling::DrawingTool::Line);
    if (!Explain("別ツールで方向選択を解除", !viewport.ReferencePickPending())) return false;
    window.RunCommand("part.extrude");
    button->click();
    if (!Explain("方向選択だけEscで取消", viewport.ReferencePickPending()
        && window.HandleToolKey(Qt::Key_Escape, nullptr) && !viewport.ReferencePickPending()
        && viewport.Selection().entityIds.empty())) return false;
    button->click(); viewport.SelectAt({-100,-100}, Qt::NoModifier);
    if (!Explain("空振りでも方向線の入力待ちを保つ", viewport.ReferencePickPending())) return false;
    const auto hit = viewport.Mapping().Project({17.5,0,2.5}); if (!hit) return false;
    viewport.HoverAt({hit->x, hit->y});
    if (!Explain("方向線だけが候補になる", viewport.CurrentCandidate()
        && viewport.CurrentCandidate()->entityId == added.createdEntityIds.front())) return false;
    viewport.SelectAt({hit->x, hit->y}, Qt::NoModifier);
    if (!Explain("方向線が輪郭に混ざらない", viewport.Selection().entityIds.empty() && dock.DirectionReady())) return false;
    const auto center = viewport.Mapping().Project({0,0,0}); if (!center) return false;
    viewport.SelectAt({center->x,center->y}, Qt::NoModifier);
    if (!viewport.ExtrudeHandleShown()) std::cerr << "Extrusion source selection: "
        << viewport.Selection().entityIds.size() << "; " << window.StatusText().toStdString() << '\n';
    if (!Explain("対象選択で下見を表示", viewport.ExtrudeHandleShown())) return false;
    if (!Explain("対象選択後も小数距離を維持", std::abs(viewport.ExtrudeHandleDistanceMm() - 0.125) < 1e-9)) return false;
    if (!Explain("対象選択後も方向を維持", std::abs(viewport.ExtrudeHandleDirection().x - std::sqrt(0.5)) < 1e-9)) return false;
    const auto snapshot = window.Session().GetDocument().Snapshot();
    dock.PressConfirm();
    if (!Explain("指定した方向へ実際に立体を作れる", CountOfKind(window, domain::EntityKind::Part) == 1)) return false;
    window.RunCommand("edit.undo");
    return Explain("1回のUndoで元の線だけへ戻る", window.Session().GetDocument().Snapshot().features.size() == snapshot.features.size());
}

bool CaseExtrudeFaceDirectionPick(V2MainWindow& window) {
    if (!ImportCurvedPart(window)) return false;
    const auto line = geometry::CurveSegment::MakeLine({20,20,0}, {25,25,5});
    if (!line.HasValue() || !window.Session().AddWire({line.Value()}, false, "斜め方向").committed
        || !PickCurvedFace(window)) return false;
    auto& viewport = window.Viewport(); auto& dock = window.ExtrudeDock();
    const auto source = viewport.Selection();
    dock.TypeDistanceMm(0.125);
    dock.PickDirection(modeling::ExtrudeDirectionMode::SelectedVector);
    const auto before = window.Session().GetDocument().Snapshot().features.size();
    window.HandleToolKey(Qt::Key_Return, nullptr);
    if (!Explain("方向未選択ではEnterでも確定しない", before == window.Session().GetDocument().Snapshot().features.size())) return false;
    auto* button = window.findChild<QPushButton*>("extrudePickDirection"); if (!button) return false;
    button->click();
    const auto hit = viewport.Mapping().Project({22.5,22.5,2.5}); if (!hit) return false;
    viewport.SelectAt({hit->x,hit->y}, Qt::NoModifier);
    if (!Explain("方向線を選んでも元の曲面フェイスが残る", viewport.Selection().entityIds == source.entityIds
        && viewport.Selection().ordered.front().pickedFaceIndex == source.ordered.front().pickedFaceIndex
        && dock.DirectionReady())) return false;
    button->click(); window.HandleToolKey(Qt::Key_Escape, nullptr);
    if (!Explain("方向再選択の取消でも面・設定を保持", viewport.Selection().entityIds == source.entityIds
        && dock.DirectionReady() && dock.DistanceMm() == 0.125)) return false;
    dock.PressConfirm();
    return Explain("方向線に沿って曲面フェイスを押し出せる", CountOfKind(window, domain::EntityKind::Part) == 2);
}

bool CaseExtrudeTargetFirst(V2MainWindow& window) {
    if (!ImportCurvedPart(window)) return false;
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Isometric);
    viewport.SetViewCenter({7,7,10}); viewport.SetVisibleWidthMm(50);
    window.RunCommand("part.extrude"); window.ExtrudeDock().PressReselectTarget();
    const auto views = viewport.ShapeViews();
    for (const auto& view : views) for (const auto& triangle : view.mesh.triangles) {
        const auto point = viewport.Mapping().Project((triangle.points[0]+triangle.points[1]+triangle.points[2])*(1.0/3));
        if (!point) continue;
        viewport.HoverAt({point->x,point->y});
        const auto pick = viewport.CurrentCandidate();
        if (!pick || !pick->pickedFaceIndex) continue;
        viewport.SelectAt({point->x,point->y}, Qt::NoModifier);
        const auto plan = window.PlanExtrudeFromSelection();
        return Explain("相手の立体を先に指定しても押す面に混ざらない",
            !plan.targetSolid.IsNil() && plan.profiles.empty() && !viewport.ExtrudeHandleShown());
    }
    return false;
}


bool CaseEndCapDistance(V2MainWindow& window) {
    auto& viewport=window.Viewport();auto& dock=window.ExtrudeDock();
    viewport.SetViewDirection(ViewDirection::Top);viewport.SetViewCenter({});viewport.SetVisibleWidthMm(60);
    const auto circle=geometry::CurveSegment::MakeCircle({0,0,0},{0,0,1},{1,0,0},2);
    if(!circle.HasValue())return false;
    const auto added=window.Session().AddWire({circle.Value()},false,"終端角度の円");if(!added.committed)return false;
    app::SelectionSet selected;selected.entityIds=added.createdEntityIds;viewport.SetSelection(selected);window.RunCommand("part.extrude");
    if(!viewport.ExtrudeHandleShown())return false;
    dock.TypeDistanceMm(10);
    auto* mode=window.findChild<QComboBox*>("endCapMode");auto* tilt=window.findChild<QDoubleSpinBox*>("endCapTilt");
    auto* pick=window.findChild<QPushButton*>("endCapPick");if(!mode||!tilt||!pick)return false;
    mode->setCurrentIndex(2);pick->click();
    if(!Explain("終端向きの選択は独立して取消可能",viewport.ReferencePickPending()
        &&window.HandleToolKey(Qt::Key_Escape,nullptr)&&!viewport.ReferencePickPending()
        &&viewport.Selection().entityIds==selected.entityIds))return false;
    mode->setCurrentIndex(3);tilt->setValue(15);
    const auto before=window.Session().GetDocument().Snapshot().features.size();
    dock.PressConfirm();
    if(!Explain("傾いた終端で実際に確定",CountOfKind(window,domain::EntityKind::Part)==1))return false;
    if(!Explain("終端指定の確定後も入力待ち",viewport.ToolPickActive()&&viewport.ProfileRegionPicking()))return false;
    const auto snapshot=window.Session().GetDocument().Snapshot();bool recorded=false;
    for(const auto& f:snapshot.features)if(const auto* e=std::get_if<domain::ExtrudeDefinition>(&f.definition))
        recorded=e->endCap.mode==modeling::EndCapMode::Angle&&std::abs(e->endCap.tiltRad-geometry::kPi/12)<1e-9;
    if(!Explain("終端角度をフィーチャーに記録",recorded))return false;
    window.RunCommand("edit.undo");if(window.Session().GetDocument().Snapshot().features.size()!=before)return false;
    window.RunCommand("edit.redo");
    return Explain("角度を保持して保存・再生成",window.SaveAndReopen("end-cap-distance.kcd2")&&CountOfKind(window,domain::EntityKind::Part)==1);
}
bool CaseTargetWireOutputs(V2MainWindow& window, int output) {
    const auto circle=geometry::CurveSegment::MakeCircle({0,0,0},{0,0,1},{1,0,0},2).Value();
    const auto stop=geometry::CurveSegment::MakeLine({-5,0,10},{5,0,12}).Value();
    const auto source=window.Session().AddWire({circle},false,"押す円");
    const auto target=window.Session().AddWire({stop},false,"終端の傾き");
    if(!source.committed||!target.committed)return false;
    app::SelectionSet selected;selected.entityIds=source.createdEntityIds;
    window.Viewport().SetSelection(selected);window.RunCommand("part.extrude");
    app::ExtrudeChoice choice;choice.extent=modeling::ExtrudeExtentMode::ToTarget;
    choice.direction=modeling::ExtrudeDirectionMode::CustomXYZ;choice.customDirection={0,0,1};
    choice.targetEntityId=target.createdEntityIds.front();window.ApplyExtrudeChoice(choice);
    window.ExtrudeDock().ShowOutputs({output==1,output!=2,true,output!=2});
    window.ExtrudeDock().PressConfirm();
    const int parts=output==1?1:0, minimum=output==2?3:4;
    if(!Explain("HP-EX selected wire and body output",CountOfKind(window,domain::EntityKind::Part)==parts
        &&CountOfKind(window,domain::EntityKind::Wire)>=minimum))return false;
    int ends=0;
    for(const auto& e:window.Session().GetDocument().Snapshot().entities) {
        if(e.kind!=domain::EntityKind::Wire||e.displayName.find("押し出し先の輪郭")==std::string::npos)continue;
        app::SelectionSet pick;pick.entityIds={e.id};const auto curves=app::SelectedCurves(pick,window.Session().Scene());
        if(!geometry::SegmentsFormClosedLoop(curves,{}))return false;
        for(const auto& c:curves)for(int i=0;i<=32;++i){auto p=c.Evaluate(i/32.0);if(std::abs(p.z-(11+.2*p.x))>.001)return false;}
        ++ends;
    }
    if(ends!=1||!window.Viewport().ToolPickActive())return false;
    const int wires=CountOfKind(window,domain::EntityKind::Wire);
    window.RunCommand("edit.undo");if(CountOfKind(window,domain::EntityKind::Wire)!=2||CountOfKind(window,domain::EntityKind::Part)!=0)return false;
    window.RunCommand("edit.redo");
    return window.SaveAndReopen("target-wire-outputs.kcd2")&&CountOfKind(window,domain::EntityKind::Wire)==wires
        &&CountOfKind(window,domain::EntityKind::Part)==parts;
}
bool CaseEndCapNativeFace(V2MainWindow& window) {
    if(!ImportCurvedPart(window)||!PickCurvedFace(window))return false;
    auto& dock=window.ExtrudeDock();dock.TypeDistanceMm(20);
    auto* mode=window.findChild<QComboBox*>("endCapMode");auto* tilt=window.findChild<QDoubleSpinBox*>("endCapTilt");
    if(!mode||!tilt)return false;mode->setCurrentIndex(3);tilt->setValue(5);
    dock.PressConfirm();
    return Explain("曲面フェイスも独立角度の終端で保存・再生成",CountOfKind(window,domain::EntityKind::Part)==2
        &&window.SaveAndReopen("end-cap-native.kcd2")&&CountOfKind(window,domain::EntityKind::Part)==2);
}
bool CaseExtrudeRepeatWithoutBox(V2MainWindow& window) {
    auto& viewport=window.Viewport();auto& dock=window.ExtrudeDock();
    std::vector<base::EntityId> ids;
    for(double x:{0.0,10.0}) {
        const auto circle=geometry::CurveSegment::MakeCircle({x,0,0},{0,0,1},{1,0,0},2);
        if(!circle.HasValue())return false;
        const auto added=window.Session().AddWire({circle.Value()},false,"連続押し出し");
        if(!added.committed)return false;ids.push_back(added.createdEntityIds.front());
    }
    viewport.SetSelection({});window.RunCommand("part.extrude");
    for(std::size_t i=0;i<ids.size();++i) {
        app::SelectionSet selected;selected.entityIds={ids[i]};viewport.SetSelection(selected);
        if(!Explain("再起動せず次の輪郭を下見",viewport.ExtrudeHandleShown()))return false;
        dock.TypeDistanceMm(3);
        // Simulate a mouse release consumed by the calculation dialog.
        viewport.BeginBoxSelect({20,20},Qt::NoModifier);dock.PressConfirm();
        if(!Explain("確定後は押し出し入力待ちで矩形を残さない",viewport.ToolPickActive()
            &&viewport.ProfileRegionPicking()&&!viewport.ExtrudeHandleShown()&&!viewport.BoxSelecting()
            &&viewport.Selection().entityIds.empty()&&CountOfKind(window,domain::EntityKind::Part)==i+1))return false;
    }
    viewport.BeginBoxSelect({20,20},Qt::NoModifier);
    QMouseEvent held(QEvent::MouseMove,QPointF(60,60),QPointF(60,60),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&viewport,&held);
    if(!Explain("押している間の範囲選択は維持",viewport.BoxSelecting()&&viewport.BoxSelectKind().has_value()))return false;
    QMouseEvent released(QEvent::MouseMove,QPointF(80,80),QPointF(80,80),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&viewport,&released);
    if(!Explain("離すイベント欠落後もドラッグが残らない",!viewport.BoxSelecting()&&viewport.Selection().entityIds.empty()))return false;
    window.HandleToolKey(Qt::Key_Escape,nullptr);
    if(!viewport.ToolPickActive())return false;
    window.RunCommand("selection.activate");
    if(!Explain("明示的な選択ツールへの切替は可能",!viewport.ToolPickActive()))return false;
    window.RunCommand("part.extrude");window.SelectTool(modeling::DrawingTool::Rectangle);
    return Explain("作図への切替でも押し出しを終了",!viewport.ToolPickActive()&&!viewport.ProfileRegionPicking());
}
bool CaseExtrudeFaceHover(V2MainWindow& window) {
    if (!ImportCurvedPart(window)) return false;
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Isometric);
    viewport.SetViewCenter({7,7,10}); viewport.SetVisibleWidthMm(50);
    viewport.SetSelection({}); window.RunCommand("part.extrude");
    std::vector<std::size_t> seen;
    const auto views = viewport.ShapeViews();
    for (const auto& view : views) for (const auto& triangle : view.mesh.triangles) {
        const auto point = viewport.Mapping().Project((triangle.points[0]+triangle.points[1]+triangle.points[2])*(1.0/3));
        if (!point) continue;
        viewport.HoverAt({point->x,point->y});
        const auto pick = viewport.CurrentCandidate();
        if (!pick || !pick->pickedFaceIndex) continue;
        const auto face = *pick->pickedFaceIndex;
        if (!Explain("候補の面だけHover色で塗る", viewport.FaceRoleColorOf(pick->entityId, face)
            == viewport.SemanticColor(app::SemanticState::Hover))) return false;
        for (auto other : seen) if (other != face && viewport.FaceRoleColorOf(pick->entityId, other)) return false;
        if (std::find(seen.begin(), seen.end(), face) == seen.end()) seen.push_back(face);
        if (seen.size() >= 2) {
            const auto path = qEnvironmentVariable("KACHACAD_EXTRUDE_UI_SCREENSHOT");
            if (!path.isEmpty()) { QApplication::processEvents(); window.grab().save(path); }
            viewport.HoverAt({-100,-100});
            if (!Explain("空所へ移動すると面色を解除", !viewport.FaceRoleColorOf(pick->entityId, face))) return false;
            window.EndExtrudePreview();
            return true;
        }
    }
    return Explain("異なる2つのフェイスでホバーを確認", false);
}
}
std::vector<SelfTestCase> FaceExtrudeCases() {
    return {{"HP-EX-WIRES stop wire only Undo save",[](auto& w){return CaseTargetWireOutputs(w,0);}},
        {"HP-EX-WIRES stop wire plus solid Undo save",[](auto& w){return CaseTargetWireOutputs(w,1);}},
        {"HP-EX-WIRES stop end-only Undo save",[](auto& w){return CaseTargetWireOutputs(w,2);}},
        {"HP-EX-REPEAT commit and lost mouse release",CaseExtrudeRepeatWithoutBox},
        {"HP-EX-CAP distance orientation Undo save",CaseEndCapDistance},
        {"HP-EX-CAP native face orientation save",CaseEndCapNativeFace},
        {"HP-EX-UI tool first settings and direction pick", CaseExtrudeToolFirstSettings},
        {"HP-EX-UI target solid first", CaseExtrudeTargetFirst},
        {"HP-EX-UI face and direction independent", CaseExtrudeFaceDirectionPick},
        {"HP-EX-UI hovered face fill", CaseExtrudeFaceHover},
        {"HP-EX-NATIVE 曲面フェイスの道具先行・取消・Undo・保存", CaseNativeFace},
        {"HP-EX-LINE 曲面フェイスから線まで・保存再計算", CaseNativeFaceToLine},
        {"HP-CALC-01 計算バー・GUI応答・取消・形状の解放", CaseProgressCancellation},
        {"HP-CALC-02 分割取消で両側の内部形状を解放", CaseSplitCancellation}};
}
}
