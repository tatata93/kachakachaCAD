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
#include <QApplication>
#include <QDialog>
#include <QObject>
#include <QPointF>
#include <QPushButton>
#include <QTimer>
#include <QTemporaryDir>
#include <QString>
#include <chrono>
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
}
std::vector<SelfTestCase> FaceExtrudeCases() {
    return {{"HP-EX-NATIVE 曲面フェイスの道具先行・取消・Undo・保存", CaseNativeFace},
        {"HP-EX-LINE 曲面フェイスから線まで・保存再計算", CaseNativeFaceToLine},
        {"HP-CALC-01 計算バー・GUI応答・取消・形状の解放", CaseProgressCancellation}};
}
}
