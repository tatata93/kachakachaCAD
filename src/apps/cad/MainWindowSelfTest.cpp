#include "MainWindow.h"
#include "PlatePdfExport.h"

#include "kachakacha/io/PlateFlatPattern.h"
#include "kachakacha/io/PlanarExport.h"
#include "kachakacha/io/ProjectScript.h"
#include "kachakacha/io/NumericExpression.h"
#include "kachakacha/model/Measurement.h"
#include "kachakacha/model/Sketch.h"
#include "kachakacha/model/WireOperations.h"
#include "kachakacha/occt/BodyExport.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QDebug>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSizePolicy>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

using kachakacha::geometry::Vector2;
using kachakacha::geometry::Vector3;
using kachakacha::io::LoadProjectScript;
using kachakacha::io::AddPlateAssemblyMotionModel;
using kachakacha::io::AddPlateFlatPatternModel;
using kachakacha::io::BuildPlateAssemblyGuide;
using kachakacha::io::BuildPlateAssemblyMotion;
using kachakacha::io::BuildPlateFlatPattern;
using kachakacha::io::PapercraftCutDirection;
using kachakacha::io::PlateAssemblyStrategy;
using kachakacha::io::PlateFlatPatternOptions;
using kachakacha::io::ReliefNotchStyle;
using kachakacha::io::WireLiesOnWorkPlane;
using kachakacha::io::WritePlateFlatPatternDxf;
using kachakacha::io::WritePlateFlatPatternSvg;
using kachakacha::io::WritePlanarDxf;
using kachakacha::io::WritePlanarSvg;
using kachakacha::io::WriteProjectScript;
using kachakacha::qtio::CalculatePlatePdfLayout;
using kachakacha::qtio::PlatePdfOptions;
using kachakacha::qtio::WritePlateFlatPatternPdf;
using kachakacha::model::DimensionReference;
using kachakacha::model::DimensionReferenceKind;
using kachakacha::model::Project;
using kachakacha::model::PlateDevelopability;
using kachakacha::model::PlateSplitAxis;
using kachakacha::model::PlateThicknessDirection;
using kachakacha::model::JigSide;
using kachakacha::model::Sketch;
using kachakacha::model::SurfaceKind;
using kachakacha::model::Wire;
using kachakacha::model::WireContinuity;
using kachakacha::model::WireKind;
using kachakacha::model::WireMetadata;
using kachakacha::model::WirePlanePolicy;
using kachakacha::model::WorkPlane;
using kachakacha::model::ChamferIntersectingLines;
using kachakacha::model::FilletIntersectingLines;
using kachakacha::model::JoinLineChain;
using kachakacha::model::MeasureDirectionToPlaneAngleDegrees;
using kachakacha::model::MeasureDirectionsAngle;
using kachakacha::model::MeasurePlaneToPlaneAngleDegrees;
using kachakacha::model::MeasurePointToWireDistance;
using kachakacha::model::MeasureSignedPointToPlaneDistance;
using kachakacha::model::MeasureThreePointAngle;
using kachakacha::model::MeasureWireCurvatureNormal;
using kachakacha::model::MeasureWireLength;
using kachakacha::model::MeasureWireRadius;
using kachakacha::model::MeasureWireTangent;
using kachakacha::model::MeasureWireToWireDistance;
using kachakacha::model::MeetLinesAtIntersection;
using kachakacha::model::ExtendWireToBoundary;
using kachakacha::model::OffsetPlanarWire;
using kachakacha::model::ReferenceDimension;
using kachakacha::model::ReferenceDimensionKind;
using kachakacha::model::RetainedLineEnd;
using kachakacha::model::TrimWireAtBoundaries;

namespace {

// Duplicated verbatim from the anonymous namespace in MainWindow.cpp: these
// helpers have internal linkage there and are still used by the rest of that
// file, so they were left in place per the split plan. RunCreationSelfTest()
// and PrepareManualScreenshot() below need them too, and an anonymous
// namespace cannot be shared across translation units, so a byte-identical
// copy is kept here rather than changing MainWindow.cpp's linkage.

constexpr int kDimensionNameRole = Qt::UserRole + 2;
constexpr double kPi = 3.14159265358979323846;

Vector3 ReadVector3(const std::array<QDoubleSpinBox*, 3>& fields)
{
    return {fields[0]->value(), fields[1]->value(), fields[2]->value()};
}

std::string ToName(const QString& text)
{
    return text.trimmed().toUtf8().toStdString();
}

} // namespace

bool MainWindow::PrepareManualScreenshot(const QString& state)
{
    const auto findSelection = [this](CadSelectionKind kind, std::string_view name)
        -> std::optional<CadSelection> {
        if (kind == CadSelectionKind::WorkPlane) {
            for (int index = 0; index < static_cast<int>(project_.WorkPlanes().size()); ++index) {
                if (project_.WorkPlanes()[index].name == name) {
                    return CadSelection{kind, index};
                }
            }
        } else if (kind == CadSelectionKind::Point) {
            for (int index = 0; index < static_cast<int>(project_.Points().size()); ++index) {
                if (project_.Points()[index].name == name) {
                    return CadSelection{kind, index};
                }
            }
        } else if (kind == CadSelectionKind::Wire) {
            for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
                if (project_.Wires()[index].name == name) {
                    return CadSelection{kind, index};
                }
            }
        } else if (kind == CadSelectionKind::Surface) {
            for (int index = 0; index < static_cast<int>(project_.Surfaces().size()); ++index) {
                if (project_.Surfaces()[index].name == name) {
                    return CadSelection{kind, index};
                }
            }
        } else if (kind == CadSelectionKind::Plate) {
            for (int index = 0; index < static_cast<int>(project_.Plates().size()); ++index) {
                if (project_.Plates()[index].name == name) {
                    return CadSelection{kind, index};
                }
            }
        } else if (kind == CadSelectionKind::Body) {
            for (int index = 0; index < static_cast<int>(project_.Bodies().size()); ++index) {
                if (project_.Bodies()[index].name == name) {
                    return CadSelection{kind, index};
                }
            }
        }
        return std::nullopt;
    };
    const auto select = [&](std::initializer_list<std::pair<CadSelectionKind, std::string_view>> names) {
        std::vector<CadSelection> selections;
        for (const auto& [kind, name] : names) {
            const auto found = findSelection(kind, name);
            if (!found.has_value()) {
                qWarning() << "manual screenshot object not found:" << QString::fromUtf8(name.data(), static_cast<int>(name.size()));
                return false;
            }
            selections.push_back(*found);
        }
        UpdateSelections(std::move(selections), true);
        return true;
    };
    const auto showTab = [this](int index, double scrollFraction = 0.0) {
        toolsTabs_->setCurrentIndex(index);
        QApplication::processEvents();
        if (auto* area = qobject_cast<QScrollArea*>(toolsTabs_->widget(index))) {
            QScrollBar* scroll = area->verticalScrollBar();
            scroll->setValue(static_cast<int>(std::round(scroll->maximum()
                * std::clamp(scrollFraction, 0.0, 1.0))));
        }
    };
    const auto revealSection = [this](int tabIndex, const QString& anchorName) {
        QWidget* tab = toolsTabs_->widget(tabIndex);
        auto* area = qobject_cast<QScrollArea*>(tab);
        if (area == nullptr && tab != nullptr) {
            area = tab->findChild<QScrollArea*>();
        }
        if (area == nullptr) {
            qWarning() << "manual screenshot scroll area not found:" << tabIndex;
            return;
        }
        QApplication::processEvents();
        const auto children = tab->findChildren<QWidget*>();
        const auto anchor = std::find_if(children.begin(), children.end(), [&](QWidget* child) {
            return child->property("manualAnchor").toString() == anchorName;
        });
        if (anchor != children.end()) {
            QWidget* content = area->widget();
            QScrollBar* scroll = area->verticalScrollBar();
            const int anchorTop = (*anchor)->mapTo(content, QPoint(0, 0)).y();
            const int sectionTopValue = anchorTop - 12;
            scroll->setValue(std::clamp(sectionTopValue, scroll->minimum(), scroll->maximum()));
            QApplication::processEvents();
        } else {
            qWarning() << "manual screenshot anchor not found:" << anchorName;
        }
    };
    const auto setActivePlane = [this](const QString& name) {
        const int index = activePlaneCombo_->findText(name);
        if (index < 0) {
            return false;
        }
        activePlaneCombo_->setCurrentIndex(index);
        RefreshActiveWorkPlane();
        return true;
    };
    int finalRevealTab = -1;
    QString finalRevealAnchor;

    UpdateSelection({}, true);
    SetViewportTool(ViewportTool::Select);
    viewport_->SetIsometricView();

    if (state == QStringLiteral("overview") || state == QStringLiteral("view")) {
        showTab(0);
        viewport_->FitAll();
    } else if (state == QStringLiteral("grid") || state == QStringLiteral("drawing")
        || state == QStringLiteral("snap")) {
        if (!setActivePlane(QStringLiteral("front"))) {
            return false;
        }
        for (const auto& plane : project_.WorkPlanes()) {
            project_.SetWorkPlaneVisible(plane.name, plane.name == "front");
        }
        for (const auto& wire : project_.Wires()) {
            project_.SetWireVisible(wire.name,
                wire.metadata.sourcePlaneName.has_value()
                    && *wire.metadata.sourcePlaneName == "front");
        }
        gridPointsVisible_->setChecked(true);
        snapAction_->setChecked(true);
        snapStepField_->setValue(1.0);
        gridSubdivision_->setCurrentIndex(gridSubdivision_->findData(4));
        gridOrigin_[0]->setValue(0.5);
        gridOrigin_[1]->setValue(0.5);
        RefreshModelViews(false);
        showTab(0);
        if (state == QStringLiteral("drawing")) {
            SetViewportTool(ViewportTool::DrawLine);
        } else if (state == QStringLiteral("snap")) {
            const auto front = project_.FindWorkPlane("front");
            if (!front.has_value()) {
                return false;
            }
            for (const auto& wire : project_.Wires()) {
                project_.SetWireVisible(wire.name, false);
            }
            WireMetadata metadata;
            metadata.sourcePlaneName = "front";
            metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
            project_.AddWire("snap_horizontal", Wire::Line(
                front->ToWorld(-6.0, 1.0), front->ToWorld(4.0, 1.0)), metadata);
            project_.AddWire("snap_vertical", Wire::Line(
                front->ToWorld(-1.0, -3.0), front->ToWorld(-1.0, 5.0)), metadata);
            project_.AddPoint("snap_reference", front->ToWorld(2.0, 3.0), "front");
            RefreshModelViews(false);
            SetViewportTool(ViewportTool::DrawPoint);
        } else {
            SetViewportTool(ViewportTool::MoveGridOrigin);
        }
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
        if (state == QStringLiteral("drawing")) {
            QApplication::processEvents();
            const QPointF start(viewport_->width() * 0.38, viewport_->height() * 0.58);
            const QPointF end = start + QPointF(135.0, -70.0);
            const auto sendViewportMouse = [this](
                                               QEvent::Type type,
                                               QPointF position,
                                               Qt::MouseButton button,
                                               Qt::MouseButtons buttons) {
                const QPointF globalPosition = viewport_->mapToGlobal(position.toPoint());
                QMouseEvent event(
                    type, position, globalPosition, button, buttons, Qt::NoModifier);
                QApplication::sendEvent(viewport_, &event);
            };
            sendViewportMouse(QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
            sendViewportMouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
            sendViewportMouse(QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
            sendViewportMouse(QEvent::MouseMove, end, Qt::NoButton, Qt::NoButton);
            const QString expression = QStringLiteral("(10/2)*3");
            for (const QChar character : expression) {
                QWidget* target = QApplication::focusWidget();
                if (target == nullptr) {
                    target = viewport_;
                }
                QKeyEvent keyEvent(
                    QEvent::KeyPress, character.unicode(), Qt::NoModifier, QString(character));
                QApplication::sendEvent(target, &keyEvent);
            }
        } else if (state == QStringLiteral("snap")) {
            QApplication::processEvents();
            const QPointF intersection(viewport_->width() * 0.5, viewport_->height() * 0.5);
            const QPointF globalPosition = viewport_->mapToGlobal(intersection.toPoint());
            QMouseEvent moveEvent(
                QEvent::MouseMove, intersection, globalPosition,
                Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(viewport_, &moveEvent);
        }
    } else if (state == QStringLiteral("arc-endpoints")
        || state == QStringLiteral("arc-tangent")) {
        if (!setActivePlane(QStringLiteral("front"))) {
            return false;
        }
        for (const auto& plane : project_.WorkPlanes()) {
            project_.SetWorkPlaneVisible(plane.name, plane.name == "front");
        }
        for (const auto& wire : project_.Wires()) {
            project_.SetWireVisible(wire.name, false);
        }
        RefreshModelViews(false);
        showTab(0);
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
        const auto front = project_.FindWorkPlane("front");
        if (!front.has_value()) {
            return false;
        }
        const auto clickWorld = [this](Vector3 point) {
            const QPointF position = viewport_->ScreenPoint(point);
            const QPointF globalPosition = viewport_->mapToGlobal(position.toPoint());
            QMouseEvent moveEvent(
                QEvent::MouseMove, position, globalPosition,
                Qt::NoButton, Qt::NoButton, Qt::ControlModifier);
            QApplication::sendEvent(viewport_, &moveEvent);
            QMouseEvent pressEvent(
                QEvent::MouseButtonPress, position, globalPosition,
                Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
            QApplication::sendEvent(viewport_, &pressEvent);
            QMouseEvent releaseEvent(
                QEvent::MouseButtonRelease, position, globalPosition,
                Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
            QApplication::sendEvent(viewport_, &releaseEvent);
        };
        if (state == QStringLiteral("arc-endpoints")) {
            arcDrawingMode_->setCurrentIndex(1);
            arcRadiusField_->setValue(5.0);
            arcBulgeSide_->setCurrentIndex(arcBulgeSide_->findData(true));
            UpdateArcConfiguration();
            SetViewportTool(ViewportTool::DrawArc);
            clickWorld(front->ToWorld(-3.0, 0.0));
            clickWorld(front->ToWorld(3.0, 0.0));
        } else {
            arcDrawingMode_->setCurrentIndex(2);
            arcDirectionBasis_->setCurrentIndex(arcDirectionBasis_->findData(1));
            arcDirectionAngle_->setValue(90.0);
            arcRadiusField_->setValue(4.0);
            arcExtentMode_->setCurrentIndex(arcExtentMode_->findData(0));
            arcExtentValue_->setValue(160.0);
            arcTurnSide_->setCurrentIndex(arcTurnSide_->findData(1.0));
            UpdateArcConfiguration();
            SetViewportTool(ViewportTool::DrawArc);
            clickWorld(front->ToWorld(-2.5, -1.0));
        }
    } else if (state == QStringLiteral("workplane")) {
        for (const auto& plane : project_.WorkPlanes()) {
            project_.SetWorkPlaneVisible(plane.name, true);
        }
        RefreshModelViews(false);
        if (!select({{CadSelectionKind::WorkPlane, "free_paper"}})) {
            return false;
        }
        showTab(1);
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("numeric")) {
        if (!select({{CadSelectionKind::Wire, "origin_to_point"}})) {
            return false;
        }
        showTab(2);
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("edit")) {
        if (!select({{CadSelectionKind::Wire, "front_nose_curve"}})) {
            return false;
        }
        showTab(3);
        (void)viewport_->AlignToSelection();
    } else if (state == QStringLiteral("transforms")) {
        if (!select({
                {CadSelectionKind::Wire, "front_window_bottom"},
                {CadSelectionKind::Wire, "front_window_top"},
            })) {
            return false;
        }
        showTab(3, 0.0);
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
    } else if (state == QStringLiteral("trim") || state == QStringLiteral("extend")) {
        if (!setActivePlane(QStringLiteral("front"))) {
            return false;
        }
        for (const auto& plane : project_.WorkPlanes()) {
            project_.SetWorkPlaneVisible(plane.name, plane.name == "front");
        }
        for (const auto& wire : project_.Wires()) {
            project_.SetWireVisible(wire.name, false);
        }
        const auto front = project_.FindWorkPlane("front");
        if (!front.has_value()) {
            return false;
        }
        WireMetadata metadata;
        metadata.sourcePlaneName = "front";
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        project_.AddWire("edit_boundary_left", Wire::Line(
            front->ToWorld(-4.0, -5.0), front->ToWorld(-4.0, 5.0)), metadata);
        const bool trimState = state == QStringLiteral("trim");
        project_.AddWire("edit_boundary_right", Wire::Line(
            front->ToWorld(trimState ? 4.0 : -3.0, -5.0),
            front->ToWorld(trimState ? 4.0 : -3.0, trimState ? 5.0 : 8.0)), metadata);
        project_.AddWire("edit_target", trimState
                ? Wire::CubicBezier(
                    front->ToWorld(-8.0, 0.0), front->ToWorld(-5.0, 3.0),
                    front->ToWorld(5.0, -3.0), front->ToWorld(8.0, 0.0))
                : Wire::CircularArc(
                    front->ToWorld(0.0, 0.0), front->UAxis(), front->VAxis(),
                    5.0, 0.0, kPi * 0.5),
            metadata);
        RefreshModelViews(false);
        showTab(3, 0.0);
        SetViewportTool(trimState ? ViewportTool::TrimWire : ViewportTool::ExtendWire);
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
        QApplication::processEvents();
        const Vector3 hoverPoint = project_.Wires().back().wire.Evaluate(trimState ? 0.5 : 0.85);
        const QPointF screenPoint = viewport_->ScreenPoint(hoverPoint);
        const QPointF globalPoint = viewport_->mapToGlobal(screenPoint.toPoint());
        QMouseEvent moveEvent(
            QEvent::MouseMove, screenPoint, globalPoint,
            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport_, &moveEvent);
    } else if (state == QStringLiteral("machining")) {
        if (!select({
                {CadSelectionKind::Wire, "front_window_bottom"},
                {CadSelectionKind::Wire, "front_window_top"},
            })) {
            return false;
        }
        showTab(4);
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
    } else if (state == QStringLiteral("surface")) {
        for (const auto& wire : project_.Wires()) {
            project_.SetWireVisible(wire.name, wire.name.ends_with("_joined"));
        }
        for (const auto& plate : project_.Plates()) {
            project_.SetPlateVisible(plate.name, false);
        }
        for (const auto& body : project_.Bodies()) {
            project_.SetBodyVisible(body.name, false);
        }
        RefreshModelViews(false);
        if (!select({
                {CadSelectionKind::Wire, "nose_0_joined"},
                {CadSelectionKind::Wire, "nose_4_joined"},
                {CadSelectionKind::Wire, "nose_8_joined"},
                {CadSelectionKind::Wire, "nose_12_joined"},
                {CadSelectionKind::Wire, "nose_18_joined"},
            })) {
            return false;
        }
        showTab(5, 0.0);
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("composite-surface")) {
        if (!setActivePlane(QStringLiteral("front"))) {
            return false;
        }
        for (const auto& plane : project_.WorkPlanes()) {
            project_.SetWorkPlaneVisible(plane.name, plane.name == "front");
        }
        for (const auto& wire : project_.Wires()) {
            project_.SetWireVisible(wire.name, false);
        }
        for (const auto& surface : project_.Surfaces()) {
            project_.SetSurfaceVisible(surface.name, false);
        }
        const auto front = project_.FindWorkPlane("front");
        if (!front.has_value()) {
            return false;
        }
        WireMetadata metadata;
        metadata.sourcePlaneName = "front";
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        project_.AddWire("free_outline_bottom", Wire::Line(
            front->ToWorld(-6.0, -4.0), front->ToWorld(6.0, -4.0)), metadata);
        project_.AddWire("free_outline_arc", Wire::CircularArcThroughThreePoints(
            front->ToWorld(6.0, -4.0), front->ToWorld(8.0, 0.0),
            front->ToWorld(6.0, 4.0)), metadata);
        project_.AddWire("free_outline_top", Wire::Line(
            front->ToWorld(6.0, 4.0), front->ToWorld(-4.0, 5.0)), metadata);
        project_.AddWire("free_outline_left", Wire::Line(
            front->ToWorld(-4.0, 5.0), front->ToWorld(-6.0, -4.0)), metadata);
        project_.AddPlanarSurface("free_outline_surface", {
            "free_outline_top", "free_outline_bottom",
            "free_outline_left", "free_outline_arc",
        });
        RefreshModelViews(false);
        if (!select({
                {CadSelectionKind::Wire, "free_outline_bottom"},
                {CadSelectionKind::Wire, "free_outline_arc"},
                {CadSelectionKind::Wire, "free_outline_top"},
                {CadSelectionKind::Wire, "free_outline_left"},
                {CadSelectionKind::Surface, "free_outline_surface"},
            })) {
            return false;
        }
        surfaceType_->setCurrentIndex(0);
        showTab(5, 0.0);
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
    } else if (state == QStringLiteral("projection")) {
        for (const auto& wire : project_.Wires()) {
            project_.SetWireVisible(wire.name,
                wire.name == "light_left_plan" || wire.name == "light_right_plan"
                    || wire.name == "windscreen_plan");
        }
        for (const auto& plate : project_.Plates()) {
            project_.SetPlateVisible(plate.name, false);
        }
        project_.SetSurfaceVisible("nose_skin", true);
        RefreshModelViews(false);
        if (!select({
                {CadSelectionKind::Wire, "light_left_plan"},
                {CadSelectionKind::Wire, "light_right_plan"},
                {CadSelectionKind::Wire, "windscreen_plan"},
            })) {
            return false;
        }
        const int targetIndex = projectionSurface_->findText(QStringLiteral("nose_skin"));
        if (targetIndex >= 0) {
            projectionSurface_->setCurrentIndex(targetIndex);
        }
        showTab(5);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("surfaceProjection");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("lightcase")) {
        referenceWireName_ = "lamp_axis";
        RefreshReference();
        lightCaseDirectionMode_->setCurrentIndex(1);
        if (!select({
                {CadSelectionKind::Wire, "lamp_front"},
                {CadSelectionKind::Plate, "body_front_panel"},
            })) {
            return false;
        }
        showTab(5, 0.22);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("lightCase");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("plate") || state == QStringLiteral("direction")) {
        if (!select({{CadSelectionKind::Plate, "nose_panel_front"}})) {
            return false;
        }
        showTab(5, 0.35);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("plateOffset");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("plate-create")) {
        if (!select({{CadSelectionKind::Surface, "nose_skin"}})) {
            return false;
        }
        showTab(5);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("plateCreate");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("jig")) {
        if (!select({{CadSelectionKind::Surface, "nose_skin"}})) {
            return false;
        }
        showTab(5);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("surfaceJig");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("openings")) {
        project_.SetWireVisible("light_left_on_skin", true);
        project_.SetWireVisible("light_right_on_skin", true);
        project_.SetWireVisible("windscreen_on_skin", true);
        RefreshModelViews(false);
        if (!select({
                {CadSelectionKind::Plate, "nose_panel_front"},
                {CadSelectionKind::Wire, "light_left_on_skin"},
                {CadSelectionKind::Wire, "light_right_on_skin"},
                {CadSelectionKind::Wire, "windscreen_on_skin"},
            })) {
            return false;
        }
        showTab(5, 0.78);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("plateOpening");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("relief")) {
        project_.SetWireVisible("light_left_on_skin", true);
        RefreshModelViews(false);
        if (!select({
                {CadSelectionKind::Plate, "nose_panel_front"},
                {CadSelectionKind::Wire, "light_left_on_skin"},
            })) {
            return false;
        }
        showTab(5);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("plateRelief");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("split")) {
        if (!select({{CadSelectionKind::Plate, "nose_panel_front"}})) {
            return false;
        }
        showTab(5, 1.0);
        finalRevealTab = 5;
        finalRevealAnchor = QStringLiteral("plateSplit");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("flat-pattern")) {
        if (!select({{CadSelectionKind::Plate, "nose_panel_front"}})) {
            return false;
        }
        plateFlatPatternAutoRelief_->setChecked(true);
        showTab(6, 0.45);
        finalRevealTab = 6;
        finalRevealAnchor = QStringLiteral("plateFlatPattern");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("assembly-output")) {
        if (!select({{CadSelectionKind::Plate, "nose_panel_front"}})) {
            return false;
        }
        plateFlatPatternAutoRelief_->setChecked(true);
        plateFlatPatternCutDirection_->setCurrentIndex(2);
        plateAssemblyProgress_->setValue(45);
        if (plateAssemblyOutputPiece_->count() > 1) {
            plateAssemblyOutputPiece_->setCurrentIndex(1);
        }
        showTab(6, 0.72);
        finalRevealTab = 6;
        finalRevealAnchor = QStringLiteral("plateAssemblyOutput");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("planar-output")) {
        if (!select({
                {CadSelectionKind::Wire, "front_window_bottom"},
                {CadSelectionKind::Wire, "front_window_top"},
            })) {
            return false;
        }
        const int planeIndex = exportPlane_->findText(QStringLiteral("front"));
        if (planeIndex >= 0) {
            exportPlane_->setCurrentIndex(planeIndex);
        }
        exportScope_->setCurrentIndex(1);
        showTab(6);
        finalRevealTab = 6;
        finalRevealAnchor = QStringLiteral("planarOutput");
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();
    } else if (state == QStringLiteral("output")) {
        if (!select({
                {CadSelectionKind::Plate, "nose_panel_front"},
                {CadSelectionKind::Body, "nose_forming_jig"},
            })) {
            return false;
        }
        showTab(6, 1.0);
        finalRevealTab = 6;
        finalRevealAnchor = QStringLiteral("modelOutput");
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("display") || state == QStringLiteral("display-grid")) {
        showTab(7);
        if (state == QStringLiteral("display-grid")) {
            finalRevealTab = 7;
            finalRevealAnchor = QStringLiteral("displaySettings");
        }
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("measure-3d")) {
        showTab(8);
        measurementMode_->setCurrentIndex(1);
        UpdateMeasurement({
            {MeasurementPickKind::Point, -1, {0.0, 0.0, 0.0}, 0.0},
            {MeasurementPickKind::Point, -1, {8.0, 0.0, 0.0}, 0.0},
            {MeasurementPickKind::Point, -1, {0.0, 6.0, 4.0}, 0.0},
        });
        viewport_->SetIsometricView();
        viewport_->FitAll();
        finalRevealTab = 8;
        finalRevealAnchor = QStringLiteral("measurement");
    } else if (state == QStringLiteral("measure-normal")) {
        const auto curve = findSelection(CadSelectionKind::Wire, "front_nose_curve");
        const auto line = findSelection(CadSelectionKind::Wire, "origin_to_point");
        if (!curve.has_value() || !line.has_value()) {
            return false;
        }
        showTab(8);
        measurementMode_->setCurrentIndex(2);
        UpdateMeasurement({
            {MeasurementPickKind::Wire, curve->index,
                project_.Wires()[curve->index].wire.Evaluate(0.55), 0.55},
            {MeasurementPickKind::Wire, line->index,
                project_.Wires()[line->index].wire.Evaluate(0.5), 0.5},
        });
        viewport_->SetIsometricView();
        viewport_->FitAll();
        finalRevealTab = 8;
        finalRevealAnchor = QStringLiteral("measurement");
    } else if (state == QStringLiteral("inspection")) {
        modelFilter_->setText(QStringLiteral("nose_panel"));
        if (!select({{CadSelectionKind::Plate, "nose_panel_front"}})) {
            return false;
        }
        showTab(8);
        SetDisplayMode(ViewportDisplayMode::FinishedModel);
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else if (state == QStringLiteral("info")) {
        if (!select({{CadSelectionKind::Plate, "nose_panel_front"}})) {
            return false;
        }
        showTab(8);
        viewport_->SetIsometricView();
        viewport_->FitAll();
    } else {
        qWarning() << "unknown manual screenshot state:" << state;
        return false;
    }

    QApplication::processEvents();
    if (state == QStringLiteral("split")) {
        QTimer::singleShot(0, this, [this] {
            if (auto* area = qobject_cast<QScrollArea*>(toolsTabs_->widget(5))) {
                if (area->widget() != nullptr && area->widget()->layout() != nullptr) {
                    area->widget()->layout()->activate();
                }
                area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
            }
        });
    } else if (finalRevealTab >= 0) {
        revealSection(finalRevealTab, finalRevealAnchor);
    }
    statusBar()->showMessage(QStringLiteral("マニュアル画像: %1").arg(state));
    QApplication::processEvents();
    return true;
}

bool MainWindow::RunCreationSelfTest()
{
    const auto fail = [](const char* stage) {
        qWarning() << "self-test failed:" << stage;
        QFile report(QStringLiteral("self-test-failure.txt"));
        if (report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            report.write(stage);
            report.write("\n");
        }
        return false;
    };
    const std::string initialDrawingPlaneName = ToName(activePlaneCombo_->currentText());
    const std::optional<WorkPlane> initialDrawingPlane = project_.FindWorkPlane(initialDrawingPlaneName);
    if (!initialDrawingPlane.has_value()) {
        return fail("find initial spline drawing plane");
    }
    const std::size_t beforeInitialSpline = project_.Wires().size();
    AddViewportSpline({
        initialDrawingPlane->ToWorld(-8.0, 0.0),
        initialDrawingPlane->ToWorld(-3.0, 4.0),
        initialDrawingPlane->ToWorld(3.0, -2.0),
        initialDrawingPlane->ToWorld(8.0, 1.0),
    });
    if (project_.Wires().size() != beforeInitialSpline + 1
        || project_.Wires().back().wire.Kind() != WireKind::CubicBSpline) {
        return fail("create initial B-spline");
    }
    UpdateSelection(
        {CadSelectionKind::Wire, static_cast<int>(project_.Wires().size()) - 1}, true);
    if (editWirePointTable_->rowCount() != 4) {
        return fail("select initial B-spline");
    }
    const int initialSplineIndex = static_cast<int>(project_.Wires().size()) - 1;
    const Vector3 originalControlPoint = project_.Wires().back().wire.ControlPoints()[1];
    std::vector<Vector3> movedControlPoints = project_.Wires().back().wire.ControlPoints();
    movedControlPoints[1] = movedControlPoints[1] + initialDrawingPlane->UAxis() * 1.0;
    ApplyViewportWireEdit(initialSplineIndex, Wire::CubicBSpline(movedControlPoints));
    if (!kachakacha::geometry::AlmostEqual(
            project_.Wires().back().wire.ControlPoints()[1], movedControlPoints[1], 1.0e-9)) {
        return fail("drag B-spline control point");
    }
    Undo();
    if (project_.Wires().size() != beforeInitialSpline + 1
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires().back().wire.ControlPoints()[1], originalControlPoint, 1.0e-9)) {
        return fail("undo B-spline control point drag");
    }
    Undo();
    if (project_.Wires().size() != beforeInitialSpline) {
        return fail("undo initial B-spline");
    }
    const std::size_t initialPlaneCount = project_.WorkPlanes().size();
    const std::size_t initialWireCount = project_.Wires().size();
    const std::size_t initialDimensionCount = project_.ReferenceDimensions().size();
    if (toolsTabs_->count() != 9
        || toolsTabs_->tabText(0) != QStringLiteral("作図")
        || toolsTabs_->tabText(5) != QStringLiteral("面")
        || toolsTabs_->tabText(6) != QStringLiteral("出力")
        || toolsTabs_->tabText(7) != QStringLiteral("表示")
        || toolsTabs_->tabText(8) != QStringLiteral("情報")
        || activePlaneCombo_->count() == 0
        || plateFlatPatternSummary_ == nullptr
        || plateAssemblyGuidePreview_ == nullptr
        || plateAssemblyApproximationPreview_ == nullptr
        || platePdfPaper_ == nullptr
        || platePdfOverlap_ == nullptr
        || measurementMode_ == nullptr
        || measurementResultLabel_ == nullptr
        || measurementMetric_ == nullptr
        || measurementName_ == nullptr
        || measurementSaveButton_ == nullptr
        || referenceDimensionList_ == nullptr
        || referenceDimensionDeleteButton_ == nullptr
        || measureToolAction_ == nullptr
        || coincidentToolAction_ == nullptr
        || tangentToolAction_ == nullptr
        || curvatureToolAction_ == nullptr
        || removeCoincidentAction_ == nullptr
        || removeTangentAction_ == nullptr
        || editWireLockRadius_ == nullptr
        || drawingConstruction_ == nullptr
        || editWireConstruction_ == nullptr
        || gridPointsVisible_ == nullptr
        || gridSubdivision_ == nullptr
        || gridOrigin_[0] == nullptr
        || gridOrigin_[1] == nullptr
        || gridOriginToolAction_ == nullptr
        || wireColor_ == nullptr
        || wireWidth_ == nullptr
        || wireStyle_ == nullptr
        || surfaceFillColor_ == nullptr
        || plateFillColor_ == nullptr
        || backgroundColor_ == nullptr
        || majorGridColor_ == nullptr
        || minorGridColor_ == nullptr
        || modelFilter_ == nullptr
        || designDisplayAction_ == nullptr
        || finishedDisplayAction_ == nullptr
        || isolateDisplayAction_ == nullptr
        || pointToolAction_ == nullptr
        || modelExportScope_ == nullptr
        || plateVariableThickness_ == nullptr
        || plateEndThickness_ == nullptr
        || plateOffsetLayer_ == nullptr
        || lightCaseSelectionLabel_ == nullptr
        || lightCaseReferenceLabel_ == nullptr
        || lightCaseRootName_ == nullptr
        || lightCaseSurfaceName_ == nullptr
        || lightCaseDirectionMode_ == nullptr
        || lightCaseDirection_[0] == nullptr
        || beginnerGuideTitle_ == nullptr
        || beginnerGuideNext_ == nullptr
        || beginnerGuideSteps_ == nullptr
        || beginnerGuideContext_ == nullptr
        || beginnerGuideManualButton_ == nullptr
        || std::any_of(workflowButtons_.begin(), workflowButtons_.end(), [](QPushButton* button) {
            return button == nullptr;
        })) {
        return fail("drawing workbench is primary");
    }
    toolsTabs_->setCurrentIndex(0);
    gridSubdivision_->setCurrentIndex(gridSubdivision_->findData(4));
    if (viewport_->GridSubdivision() != 4) {
        return fail("set JWCAD-style quarter grid subdivisions");
    }
    viewport_->SetGridOrigin(1.25, -2.5);
    if (std::abs(viewport_->GridOriginU() - 1.25) > 1.0e-9
        || std::abs(viewport_->GridOriginV() + 2.5) > 1.0e-9) {
        return fail("set numeric grid origin");
    }
    gridOrigin_[0]->setValue(0.0);
    gridOrigin_[1]->setValue(0.0);
    viewport_->SetGridOrigin(0.0, 0.0);
    SetViewportTool(ViewportTool::MoveGridOrigin);
    if (viewport_->Tool() != ViewportTool::MoveGridOrigin || !gridOriginToolAction_->isChecked()) {
        return fail("activate drag grid origin tool");
    }
    SetViewportTool(ViewportTool::Select);

    SetDisplayColorButton(wireColor_, QColor("#325a6b"));
    wireWidth_->setValue(3.25);
    wireStyle_->setCurrentIndex(wireStyle_->findData(static_cast<int>(Qt::DotLine)));
    SetDisplayColorButton(backgroundColor_, QColor("#f0f3f4"));
    ApplyDisplaySettings();
    if (viewport_->WireColorSetting() != QColor("#325a6b")
        || std::abs(viewport_->WireWidthSetting() - 3.25) > 1.0e-9
        || viewport_->WireStyleSetting() != Qt::DotLine
        || viewport_->BackgroundColor() != QColor("#f0f3f4")) {
        return fail("apply display color width style and background");
    }
    SetDisplayColorButton(wireColor_, QColor("#263b44"));
    wireWidth_->setValue(2.0);
    wireStyle_->setCurrentIndex(wireStyle_->findData(static_cast<int>(Qt::SolidLine)));
    SetDisplayColorButton(backgroundColor_, QColor("#f5f6f7"));
    ApplyDisplaySettings();

    SetViewportTool(ViewportTool::DrawLine);
    toolsTabs_->setCurrentIndex(5);
    if (viewport_->Tool() != ViewportTool::Select
        || beginnerGuideTitle_->text().isEmpty()
        || !workflowButtons_[2]->isChecked()) {
        return fail("beginner workflow guide follows tab and cancels stale tools");
    }
    toolsTabs_->setCurrentIndex(0);
    SetViewportTool(ViewportTool::DrawLine);

    planeName_->setText("__ui_test_plane");
    planeMethod_->setCurrentIndex(0);
    standardPlane_->setCurrentIndex(1);
    planeOffset_->setValue(2.5);
    planeTilt_->setValue(12.0);
    AddWorkPlane();
    if (project_.WorkPlanes().size() != initialPlaneCount + 1 || !project_.FindWorkPlane("__ui_test_plane").has_value()) {
        return fail("add workplane");
    }
    const std::size_t planeCountBeforeViewOnly = project_.WorkPlanes().size();
    const WorkPlane viewOnlyPlane = WorkPlaneFromInputs();
    AlignViewportFromPlaneInputs();
    if (project_.WorkPlanes().size() != planeCountBeforeViewOnly
        || !kachakacha::geometry::AlmostEqual(viewport_->ViewDirection(), viewOnlyPlane.Normal(), 1.0e-8)) {
        return fail("view without creating workplane");
    }

    wireName_->setText("__ui_test_line3d");
    wireKind_->setCurrentIndex(0);
    lineStart_[0]->setValue(0.0);
    lineStart_[1]->setValue(0.0);
    lineStart_[2]->setValue(0.0);
    lineEnd_[0]->setValue(2.0);
    lineEnd_[1]->setValue(7.0);
    lineEnd_[2]->setValue(4.0);
    AddWire();

    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(initialWireCount)}, false);
    qobject_cast<QDoubleSpinBox*>(editWirePointTable_->cellWidget(1, 0))->setValue(3.0);
    qobject_cast<QDoubleSpinBox*>(editWirePointTable_->cellWidget(1, 1))->setValue(8.0);
    qobject_cast<QDoubleSpinBox*>(editWirePointTable_->cellWidget(1, 2))->setValue(5.0);
    ApplySelectedEdit();
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[initialWireCount].wire.End(), {3.0, 8.0, 5.0})) {
        return fail("edit wire");
    }
    Undo();
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[initialWireCount].wire.End(), {2.0, 7.0, 4.0})) {
        return fail("undo wire edit");
    }
    Redo();
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[initialWireCount].wire.End(), {3.0, 8.0, 5.0})) {
        return fail("redo wire edit");
    }

    wireName_->setText("__ui_test_sketch_line");
    wireKind_->setCurrentIndex(2);
    wirePlane_->setCurrentText("__ui_test_plane");
    sketchLineStart_[0]->setValue(1.0);
    sketchLineStart_[1]->setValue(2.0);
    sketchLineEnd_[0]->setValue(5.0);
    sketchLineEnd_[1]->setValue(6.0);
    AddWire();

    const std::size_t constrainedLineIndex = initialWireCount + 1;
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(constrainedLineIndex)}, false);
    editWireLockLength_->setChecked(true);
    editWireConstraintLength_->setValue(8.0);
    editWireLockAngle_->setChecked(true);
    editWireConstraintAngle_->setValue(0.0);
    ApplySelectedEdit();
    if (project_.Wires()[constrainedLineIndex].metadata.lineConstraints.lengthMillimeters != 8.0
        || project_.Wires()[constrainedLineIndex].metadata.lineConstraints.angleDegrees != 0.0
        || std::abs((project_.Wires()[constrainedLineIndex].wire.End()
                - project_.Wires()[constrainedLineIndex].wire.Start()).Length() - 8.0) > 1.0e-8) {
        return fail("apply line dimensions");
    }
    Undo();
    if (!project_.Wires()[constrainedLineIndex].metadata.lineConstraints.Empty()) {
        return fail("undo line dimensions");
    }
    Redo();
    if (project_.Wires()[constrainedLineIndex].metadata.lineConstraints.lengthMillimeters != 8.0
        || project_.Wires()[constrainedLineIndex].metadata.lineConstraints.angleDegrees != 0.0) {
        return fail("redo line dimensions");
    }

    wireName_->setText("__ui_test_lineB");
    wireKind_->setCurrentIndex(0);
    lineStart_[0]->setValue(0.0);
    lineStart_[1]->setValue(0.0);
    lineStart_[2]->setValue(0.0);
    lineEnd_[0]->setValue(0.0);
    lineEnd_[1]->setValue(10.0);
    lineEnd_[2]->setValue(0.0);
    AddWire();

    chamferName_->setText("__ui_test_chamfer");
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(initialWireCount)},
        {CadSelectionKind::Wire, static_cast<int>(initialWireCount + 2)},
    }, true);
    if (chamferFirstWire_->currentText() != "__ui_test_line3d"
        || chamferSecondWire_->currentText() != "__ui_test_lineB") {
        return fail("multi-select chamfer inputs");
    }
    chamferFirstBranch_->setCurrentIndex(0);
    chamferSecondBranch_->setCurrentIndex(0);
    chamferFirstDistance_->setValue(0.5);
    chamferSecondDistance_->setValue(0.75);
    ApplyLineChamfer();

    if (project_.Wires().size() != initialWireCount + 4) {
        return fail("create chamfer count");
    }
    if (project_.Wires()[initialWireCount + 3].name != "__ui_test_chamfer") {
        return fail("create chamfer name");
    }

    Undo();
    machiningType_->setCurrentIndex(1);
    chamferName_->setText("__ui_test_fillet");
    machiningPickFirstButton_->click();
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(initialWireCount)}, true);
    machiningPickSecondButton_->click();
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(initialWireCount + 2)}, true);
    if (chamferFirstWire_->currentText() != "__ui_test_line3d"
        || chamferSecondWire_->currentText() != "__ui_test_lineB") {
        return fail("individual machining picks");
    }
    filletRadius_->setValue(0.5);
    ApplyLineFillet();

    if (project_.Wires().size() != initialWireCount + 4) {
        return fail("create fillet count");
    }
    const auto& line3d = project_.Wires()[initialWireCount];
    const auto& sketchLine = project_.Wires()[initialWireCount + 1];
    const auto& fillet = project_.Wires()[initialWireCount + 3];
    const bool result = kachakacha::geometry::AlmostEqual(line3d.wire.End(), {3.0, 8.0, 5.0})
        && sketchLine.metadata.sourcePlaneName == "__ui_test_plane"
        && sketchLine.metadata.planePolicy == WirePlanePolicy::ReferenceOnly
        && fillet.name == "__ui_test_fillet"
        && fillet.wire.Kind() == WireKind::CircularArc;
    if (!result) {
        return fail("pre-direct drawing checks");
    }

    int drawingPlaneIndex = activePlaneCombo_->findText(QStringLiteral("top_XY"));
    if (drawingPlaneIndex < 0 && activePlaneCombo_->count() > 1) {
        drawingPlaneIndex = 1;
    }
    if (drawingPlaneIndex < 0) {
        return fail("find drawing workplane");
    }
    activePlaneCombo_->setCurrentIndex(drawingPlaneIndex);
    const std::string drawingPlaneName = ToName(activePlaneCombo_->currentText());
    const auto selectedPlaneIterator = std::find_if(
        project_.WorkPlanes().begin(), project_.WorkPlanes().end(),
        [&](const auto& plane) { return plane.name == drawingPlaneName; });
    if (selectedPlaneIterator == project_.WorkPlanes().end()) {
        return fail("find selected drawing workplane");
    }
    const WorkPlane selectedDrawingPlane = selectedPlaneIterator->plane;
    const int selectedPlaneIndex = static_cast<int>(
        std::distance(project_.WorkPlanes().begin(), selectedPlaneIterator));
    viewport_->AlignToActiveWorkPlane();
    snapAction_->setChecked(true);
    snapStepField_->setValue(1.0);

    const auto sendMouse = [this](
                               QEvent::Type type,
                               QPointF position,
                               Qt::MouseButton button,
                               Qt::MouseButtons buttons,
                               Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPointF globalPosition(viewport_->mapToGlobal(position.toPoint()));
        QMouseEvent event(type, position, globalPosition, button, buttons, modifiers);
        QApplication::sendEvent(viewport_, &event);
    };
    const auto click = [&](QPointF position, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        sendMouse(QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton);
        sendMouse(QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, modifiers);
        sendMouse(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, modifiers);
    };
    const auto rightClick = [&](QPointF position, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        sendMouse(QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton, modifiers);
        sendMouse(QEvent::MouseButtonPress, position, Qt::RightButton, Qt::RightButton, modifiers);
        sendMouse(QEvent::MouseButtonRelease, position, Qt::RightButton, Qt::NoButton, modifiers);
    };
    const auto dragMouse = [&](QPointF start, QPointF end, Qt::MouseButton button,
                               Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        sendMouse(QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
        sendMouse(QEvent::MouseButtonPress, start, button, button, modifiers);
        sendMouse(QEvent::MouseMove, end, Qt::NoButton, button, modifiers);
        sendMouse(QEvent::MouseButtonRelease, end, button, Qt::NoButton, modifiers);
    };
    const auto drag = [&](QPointF start, QPointF end) {
        dragMouse(start, end, Qt::LeftButton);
    };
    const QPointF center(viewport_->width() * 0.5, viewport_->height() * 0.5);
    const QPointF cubeCenter(viewport_->width() - 60.0, 49.0);
    const QPointF cubeHome(viewport_->width() - 60.0, 102.0);
    const QPointF cubeSelection(viewport_->width() - 60.0, 135.0);
    const auto cubePointForDirection = [this, cubeCenter](Vector3 direction) {
        const Vector3 view = viewport_->ViewDirection();
        const Vector3 up = viewport_->ViewUpDirection();
        const Vector3 right = kachakacha::geometry::Cross(up, view).Normalized();
        return cubeCenter + QPointF(
            kachakacha::geometry::Dot(direction, right) * 22.0,
            -kachakacha::geometry::Dot(direction, up) * 22.0);
    };
    const std::size_t beforeViewCube = project_.Wires().size();
    SetViewportTool(ViewportTool::DrawLine);
    click(cubeHome);
    const QPointF cubeTop = cubePointForDirection({0.0, 0.0, 1.0});
    click(cubeTop);
    if (!kachakacha::geometry::AlmostEqual(viewport_->ViewDirection(), {0.0, 0.0, 1.0}, 1.0e-8)) {
        return fail("view cube top face");
    }
    click(cubeHome);
    const Vector3 homeDirection = viewport_->ViewDirection();
    const Vector3 cornerDirection{
        homeDirection.x >= 0.0 ? 1.0 : -1.0,
        homeDirection.y >= 0.0 ? 1.0 : -1.0,
        homeDirection.z >= 0.0 ? 1.0 : -1.0,
    };
    click(cubePointForDirection(cornerDirection));
    if (!kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), cornerDirection.Normalized(), 1.0e-8)) {
        return fail("view cube corner view");
    }
    const Vector3 directionBeforeRoll = viewport_->ViewDirection();
    const Vector3 upBeforeRoll = viewport_->ViewUpDirection();
    click(QPointF(viewport_->width() - 40.0, 10.0));
    if (!kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), directionBeforeRoll, 1.0e-8)
        || kachakacha::geometry::AlmostEqual(
            viewport_->ViewUpDirection(), upBeforeRoll, 1.0e-8)) {
        return fail("view cube roll control");
    }
    click(cubeHome);
    const Vector3 directionBeforeWorldX = viewport_->ViewDirection();
    click(QPointF(cubeCenter.x() - 146.0, 30.0));
    if (kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), directionBeforeWorldX, 1.0e-8)
        || std::abs(viewport_->ViewDirection().x - directionBeforeWorldX.x) > 1.0e-8) {
        return fail("view cube absolute X rotation");
    }
    const Vector3 directionBeforeRelativeX = viewport_->ViewDirection();
    const Vector3 rightBeforeRelativeX = viewport_->ViewRightDirection();
    click(QPointF(cubeCenter.x() - 82.0, 30.0));
    if (kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), directionBeforeRelativeX, 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(
            viewport_->ViewRightDirection(), rightBeforeRelativeX, 1.0e-8)) {
        return fail("view cube relative X rotation");
    }
    const Vector3 directionBeforeFineRotation = viewport_->ViewDirection();
    click(QPointF(cubeCenter.x() - 118.0, 30.0), Qt::ShiftModifier);
    const double fineAngle = std::acos(std::clamp(
        kachakacha::geometry::Dot(directionBeforeFineRotation, viewport_->ViewDirection()),
        -1.0, 1.0));
    if (std::abs(fineAngle - 5.0 * kPi / 180.0) > 1.0e-8) {
        return fail("view cube five degree fine rotation");
    }
    click(cubeHome);
    const Vector3 beforeTinyCubeMotion = viewport_->ViewDirection();
    drag(cubeHome, cubeHome + QPointF(2.0, 0.0));
    if (!kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), beforeTinyCubeMotion, 1.0e-8)) {
        return fail("view cube ignores accidental motion");
    }
    const Vector3 beforeCubeDrag = viewport_->ViewDirection();
    const QPointF draggableCubeFace = cubePointForDirection({0.0, 0.0, 1.0});
    drag(draggableCubeFace, draggableCubeFace + QPointF(18.0, -10.0));
    if (kachakacha::geometry::AlmostEqual(viewport_->ViewDirection(), beforeCubeDrag, 1.0e-8)
        || project_.Wires().size() != beforeViewCube
        || viewport_->DrawingPointCount() != 0) {
        return fail("view cube drag without drawing");
    }
    const Vector3 directionBeforeFit = viewport_->ViewDirection();
    const Vector3 upBeforeFit = viewport_->ViewUpDirection();
    viewport_->FitAll();
    if (!kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), directionBeforeFit, 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(
            viewport_->ViewUpDirection(), upBeforeFit, 1.0e-8)) {
        return fail("fit all preserves view cube orientation");
    }
    SetViewportTool(ViewportTool::Select);
    const Vector3 beforeLeftDrag = viewport_->ViewDirection();
    drag(center + QPointF(-180.0, 150.0), center + QPointF(-130.0, 120.0));
    if (!kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), beforeLeftDrag, 1.0e-8)) {
        return fail("left drag must not orbit");
    }
    dragMouse(
        center + QPointF(-180.0, 150.0), center + QPointF(-130.0, 120.0),
        Qt::MiddleButton, Qt::ShiftModifier);
    if (kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), beforeLeftDrag, 1.0e-8)) {
        return fail("shift middle drag orbits");
    }
    SetViewportTool(ViewportTool::DrawLine);
    const Vector3 viewBeforeSelectionAlignment = viewport_->ViewDirection();
    const Vector3 selectedPlaneNormal = selectedDrawingPlane.Normal();
    const Vector3 expectedSelectionView = kachakacha::geometry::Dot(
            selectedPlaneNormal, viewBeforeSelectionAlignment) < 0.0
        ? -selectedPlaneNormal
        : selectedPlaneNormal;
    UpdateSelection({CadSelectionKind::WorkPlane, selectedPlaneIndex}, true);
    click(cubeSelection);
    if (!kachakacha::geometry::AlmostEqual(
            viewport_->ViewDirection(), expectedSelectionView, 1.0e-8)
        || project_.Wires().size() != beforeViewCube
        || viewport_->DrawingPointCount() != 0) {
        return fail("view cube align selected workplane");
    }
    viewport_->AlignToActiveWorkPlane();

    const std::size_t directStart = project_.Wires().size();

    SetViewportTool(ViewportTool::DrawLine);
    click(center + QPointF(-150.0, 90.0));
    click(center + QPointF(-60.0, 90.0));
    const std::size_t afterLine = project_.Wires().size();
    SetViewportTool(ViewportTool::DrawRectangle);
    drag(center + QPointF(15.0, -105.0), center + QPointF(120.0, -35.0));
    const std::size_t afterRectangle = project_.Wires().size();
    SetViewportTool(ViewportTool::DrawCircle);
    click(center + QPointF(90.0, 80.0));
    click(center + QPointF(135.0, 80.0));

    Undo();
    if (project_.Wires().size() != directStart + 2) {
        return fail("undo direct circle");
    }
    Redo();

    SetViewportTool(ViewportTool::DrawPolyline);
    const std::size_t beforeCancelledPolyline = project_.Wires().size();
    click(center + QPointF(-175.0, -75.0));
    click(center + QPointF(-135.0, -105.0));
    cancelDrawingAction_->trigger();
    if (viewport_->DrawingPointCount() != 0 || project_.Wires().size() != beforeCancelledPolyline) {
        return fail("cancel direct polyline");
    }
    click(center + QPointF(-150.0, -20.0));
    click(center + QPointF(-110.0, -60.0));
    click(center + QPointF(-65.0, -20.0));
    if (viewport_->DrawingPointCount() != 3 || !finishDrawingAction_->isEnabled()) {
        return fail("polyline drawing state");
    }
    finishDrawingAction_->trigger();

    SetViewportTool(ViewportTool::DrawArc);
    click(center + QPointF(-20.0, 70.0));
    click(center + QPointF(20.0, 35.0));
    click(center + QPointF(60.0, 70.0));

    SetViewportTool(ViewportTool::DrawBezier);
    click(center + QPointF(-145.0, 35.0));
    click(center + QPointF(-125.0, -5.0));
    click(center + QPointF(-85.0, -5.0));
    click(center + QPointF(-65.0, 35.0));
    if (project_.Wires().size() != directStart + 6
        || project_.Wires()[directStart].wire.Kind() != WireKind::Line
        || project_.Wires()[directStart + 1].wire.Kind() != WireKind::Polyline
        || !project_.Wires()[directStart + 1].wire.IsClosed()
        || project_.Wires()[directStart + 2].wire.Kind() != WireKind::Circle
        || project_.Wires()[directStart + 3].wire.Kind() != WireKind::Polyline
        || project_.Wires()[directStart + 4].wire.Kind() != WireKind::CircularArc
        || project_.Wires()[directStart + 5].wire.Kind() != WireKind::CubicBezier
        || project_.Wires()[directStart].metadata.sourcePlaneName != drawingPlaneName
        || project_.Wires()[directStart].metadata.planePolicy != WirePlanePolicy::ReferenceOnly) {
        qWarning() << "direct drawing self-test failed"
                   << "initial" << directStart
                   << "line" << afterLine
                   << "rectangle" << afterRectangle
                   << "circle" << project_.Wires().size();
        return fail("direct drawing result");
    }

    {
        const Project snapTestProject = project_;
        const auto snapTestUndo = undoStack_;
        const auto snapTestRedo = redoStack_;
        const std::size_t snapWireStart = project_.Wires().size();

        SetViewportTool(ViewportTool::DrawLine);
        click(center + QPointF(-120.0, 0.0), Qt::ControlModifier);
        click(center + QPointF(120.0, 0.0), Qt::ControlModifier);
        SetViewportTool(ViewportTool::DrawLine);
        click(center + QPointF(0.0, -105.0), Qt::ControlModifier);
        click(center + QPointF(0.0, 105.0), Qt::ControlModifier);
        if (project_.Wires().size() != snapWireStart + 2) {
            return fail("create crossing snap test lines");
        }
        SetViewportTool(ViewportTool::DrawPoint);
        sendMouse(QEvent::MouseMove, center + QPointF(5.0, 0.0), Qt::NoButton, Qt::NoButton);
        if (!viewport_->DrawingSnapHover().has_value()
            || viewport_->DrawingSnapHover()->kind != DrawingSnapKind::Intersection) {
            return fail("moderate intersection snap candidate");
        }
        const std::size_t pointStart = project_.Points().size();
        click(center + QPointF(5.0, 0.0));
        if (project_.Points().size() != pointStart + 1) {
            return fail("create point at intersection");
        }

        const WorkPlane& snapPlane = selectedDrawingPlane;
        const auto firstStart = snapPlane.Project(project_.Wires()[snapWireStart].wire.Start());
        const auto firstEnd = snapPlane.Project(project_.Wires()[snapWireStart].wire.End());
        const auto secondStart = snapPlane.Project(project_.Wires()[snapWireStart + 1].wire.Start());
        const auto secondEnd = snapPlane.Project(project_.Wires()[snapWireStart + 1].wire.End());
        const double firstU = firstEnd.u - firstStart.u;
        const double firstV = firstEnd.v - firstStart.v;
        const double secondU = secondEnd.u - secondStart.u;
        const double secondV = secondEnd.v - secondStart.v;
        const double denominator = firstU * secondV - firstV * secondU;
        if (std::abs(denominator) <= 1.0e-12) {
            return fail("crossing snap test geometry");
        }
        const double deltaU = secondStart.u - firstStart.u;
        const double deltaV = secondStart.v - firstStart.v;
        const double parameter = (deltaU * secondV - deltaV * secondU) / denominator;
        const Vector3 expectedIntersection = snapPlane.ToWorld(
            firstStart.u + firstU * parameter,
            firstStart.v + firstV * parameter);
        if (!kachakacha::geometry::AlmostEqual(
                project_.Points().back().point, expectedIntersection, 1.0e-8)) {
            return fail("point uses exact line intersection");
        }
        SetViewportTool(ViewportTool::DrawLine);
        const std::size_t nearbyStartWireCount = project_.Wires().size();
        rightClick(center + QPointF(11.0, 0.0));
        if (viewport_->DrawingPointCount() != 1) {
            return fail("right click begins at nearby structural point");
        }
        click(center + QPointF(90.0, 55.0), Qt::ControlModifier);
        if (project_.Wires().size() != nearbyStartWireCount + 1
            || !kachakacha::geometry::AlmostEqual(
                project_.Wires().back().wire.Start(), expectedIntersection, 1.0e-8)) {
            return fail("right click line uses exact nearby intersection");
        }

        SetViewportTool(ViewportTool::DrawPoint);
        const QPointF bypassPosition = center + QPointF(1.0, 0.0);
        sendMouse(
            QEvent::MouseMove,
            bypassPosition,
            Qt::NoButton,
            Qt::NoButton,
            Qt::ControlModifier);
        if (viewport_->DrawingSnapHover().has_value()) {
            return fail("ctrl bypass hides snap candidate");
        }
        click(bypassPosition, Qt::ControlModifier);
        if (project_.Points().size() != pointStart + 2
            || kachakacha::geometry::AlmostEqual(
                project_.Points().back().point, expectedIntersection, 1.0e-8)) {
            return fail("ctrl bypass creates unsnapped point");
        }
        std::optional<QPointF> freeScreenPoint;
        for (int y = 25; y <= 85 && !freeScreenPoint.has_value(); ++y) {
            for (int x = 25; x <= 85; ++x) {
                const QPointF candidate = center + QPointF(x, y);
                sendMouse(QEvent::MouseMove, candidate, Qt::NoButton, Qt::NoButton);
                if (!viewport_->DrawingSnapHover().has_value()) {
                    freeScreenPoint = candidate;
                    break;
                }
            }
        }
        if (!freeScreenPoint.has_value()) {
            return fail("find unsnapped drawing position");
        }
        click(*freeScreenPoint);
        if (project_.Points().size() != pointStart + 3) {
            return fail("create freely positioned point");
        }
        const auto freeCoordinates = snapPlane.Project(project_.Points().back().point);
        const double minorStep = snapStepField_->value()
            / static_cast<double>(viewport_->GridSubdivision());
        const double nearestGridU = viewport_->GridOriginU()
            + std::round((freeCoordinates.u - viewport_->GridOriginU()) / minorStep) * minorStep;
        const double nearestGridV = viewport_->GridOriginV()
            + std::round((freeCoordinates.v - viewport_->GridOriginV()) / minorStep) * minorStep;
        if (std::abs(freeCoordinates.u - nearestGridU) <= 1.0e-9
            && std::abs(freeCoordinates.v - nearestGridV) <= 1.0e-9) {
            return fail("free point must not be forced onto grid");
        }

        project_ = snapTestProject;
        undoStack_ = snapTestUndo;
        redoStack_ = snapTestRedo;
        RefreshModelViews(false);
        UpdateHistoryActions();
    }

    {
        const Project directEditSavedProject = project_;
        const auto directEditSavedUndo = undoStack_;
        const auto directEditSavedRedo = redoStack_;
        const auto directEditSavedReference = referenceWireName_;
        const bool directEditSavedModified = modified_;
        const QString directEditSavedTitle = windowTitle();
        const Vector3 directEditSavedViewTarget = viewport_->ViewTarget();
        const double directEditSavedViewScale = viewport_->ViewScale();

        Project directEditProject;
        directEditProject.AddWorkPlane(
            "direct_edit_XY",
            WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}));
        directEditProject.AddWire("boundary_left", Wire::Line({-4.0, -5.0, 0.0}, {-4.0, 5.0, 0.0}));
        directEditProject.AddWire("boundary_right", Wire::Line({4.0, -5.0, 0.0}, {4.0, 5.0, 0.0}));
        directEditProject.AddWire("trim_target", Wire::Line({-8.0, 0.0, 0.0}, {8.0, 0.0, 0.0}));
        directEditProject.AddWire("extend_target", Wire::Line({-2.0, 3.0, 0.0}, {2.0, 3.0, 0.0}));
        project_ = std::move(directEditProject);
        undoStack_.clear();
        redoStack_.clear();
        referenceWireName_.reset();
        RefreshModelViews(true);
        SetDisplayMode(ViewportDisplayMode::Design);
        const int directEditPlane = activePlaneCombo_->findText(QStringLiteral("direct_edit_XY"));
        if (directEditPlane < 0) {
            return fail("find direct trim test plane");
        }
        activePlaneCombo_->setCurrentIndex(directEditPlane);
        viewport_->AlignToActiveWorkPlane();
        viewport_->FitAll();

        SetViewportTool(ViewportTool::TrimWire);
        click(viewport_->ScreenPoint({0.0, 0.0, 0.0}));
        if (project_.Wires().size() != 5
            || !kachakacha::geometry::AlmostEqual(project_.Wires()[3].wire.End(), {-4.0, 0.0, 0.0}, 1.0e-8)
            || !kachakacha::geometry::AlmostEqual(project_.Wires()[4].wire.Start(), {4.0, 0.0, 0.0}, 1.0e-8)) {
            return fail("direct middle line trim");
        }
        Undo();
        if (project_.Wires().size() != 4
            || project_.Wires()[2].name != "trim_target") {
            return fail("undo direct line trim");
        }

        SetViewportTool(ViewportTool::ExtendWire);
        const QPointF extendPick = viewport_->ScreenPoint({1.7, 3.0, 0.0});
        sendMouse(QEvent::MouseMove, extendPick, Qt::NoButton, Qt::NoButton);
        if (viewport_->HoveredSelection().kind != CadSelectionKind::Wire
            || viewport_->HoveredSelection().index != 3) {
            return fail("direct line extend target hover");
        }
        if (!viewport_->DirectLineEditPreviewReady()) {
            return fail("direct line extend preview");
        }
        click(extendPick);
        if (project_.Wires().size() != 4) {
            return fail("direct line extend wire count");
        }
        if (kachakacha::geometry::AlmostEqual(
                project_.Wires()[3].wire.End(), {2.0, 3.0, 0.0}, 1.0e-8)) {
            return fail("direct line extend unchanged");
        }
        if (!kachakacha::geometry::AlmostEqual(
                project_.Wires()[3].wire.End(), {4.0, 3.0, 0.0}, 1.0e-8)) {
            return fail("direct line extend wrong endpoint");
        }
        Undo();
        if (!kachakacha::geometry::AlmostEqual(
                project_.Wires()[3].wire.End(), {2.0, 3.0, 0.0}, 1.0e-8)) {
            return fail("undo direct line extend");
        }
        rightClick(viewport_->ScreenPoint({0.0, 3.0, 0.0}));
        if (viewport_->Tool() != ViewportTool::Select) {
            return fail("right click exits direct line edit");
        }

        project_.AddWire("curve_trim_target", Wire::CubicBezier(
            {-8.0, -3.0, 0.0}, {-5.0, -1.0, 0.0},
            {5.0, -5.0, 0.0}, {8.0, -3.0, 0.0}));
        RefreshModelViews(false);
        viewport_->FitAll();
        const int curveTrimIndex = 4;
        SetViewportTool(ViewportTool::TrimWire);
        const QPointF curveTrimPick = viewport_->ScreenPoint(
            project_.Wires()[curveTrimIndex].wire.Evaluate(0.5));
        sendMouse(QEvent::MouseMove, curveTrimPick, Qt::NoButton, Qt::NoButton);
        if (viewport_->HoveredSelection().kind != CadSelectionKind::Wire
            || viewport_->HoveredSelection().index != curveTrimIndex
            || !viewport_->DirectLineEditPreviewReady()) {
            return fail("direct curve trim preview");
        }
        click(curveTrimPick);
        if (project_.Wires().size() != 6
            || project_.Wires()[4].wire.Kind() != WireKind::CubicBezier
            || project_.Wires()[5].wire.Kind() != WireKind::CubicBezier) {
            return fail("direct cubic Bezier middle trim");
        }
        Undo();
        if (project_.Wires().size() != 5
            || project_.Wires()[curveTrimIndex].name != "curve_trim_target") {
            return fail("undo direct curve trim");
        }
        project_.SetWireVisible("curve_trim_target", false);

        project_.AddWire("curve_extend_boundary", Wire::Line(
            {14.0, -5.0, 0.0}, {14.0, 5.0, 0.0}));
        project_.AddWire("curve_extend_target", Wire::CubicBezier(
            {0.0, -4.0, 0.0}, {3.0, -4.0, 0.0},
            {7.0, -4.0, 0.0}, {10.0, -2.0, 0.0}));
        RefreshModelViews(false);
        viewport_->FitAll();
        const int curveExtendIndex = 6;
        SetViewportTool(ViewportTool::ExtendWire);
        const QPointF curveExtendPick = viewport_->ScreenPoint(
            project_.Wires()[curveExtendIndex].wire.Evaluate(0.9));
        sendMouse(QEvent::MouseMove, curveExtendPick, Qt::NoButton, Qt::NoButton);
        if (viewport_->HoveredSelection().kind != CadSelectionKind::Wire
            || viewport_->HoveredSelection().index != curveExtendIndex
            || !viewport_->DirectLineEditPreviewReady()) {
            return fail("direct curve extend preview");
        }
        click(curveExtendPick);
        if (project_.Wires()[curveExtendIndex].wire.Kind() != WireKind::CubicBezier
            || std::abs(project_.Wires()[curveExtendIndex].wire.End().x - 14.0) > 1.0e-7
            || project_.Wires()[curveExtendIndex].wire.End().y <= -2.0) {
            return fail("direct cubic Bezier extend");
        }
        Undo();
        if (!kachakacha::geometry::AlmostEqual(
                project_.Wires()[curveExtendIndex].wire.End(), {10.0, -2.0, 0.0}, 1.0e-8)) {
            return fail("undo direct curve extend");
        }

        const std::size_t configuredArcStart = project_.Wires().size();
        arcDrawingMode_->setCurrentIndex(1);
        arcRadiusField_->setValue(4.0);
        arcBulgeSide_->setCurrentIndex(arcBulgeSide_->findData(true));
        UpdateArcConfiguration();
        SetViewportTool(ViewportTool::DrawArc);
        click(viewport_->ScreenPoint({-3.0, 1.0, 0.0}), Qt::ControlModifier);
        click(viewport_->ScreenPoint({3.0, 1.0, 0.0}), Qt::ControlModifier);
        if (viewport_->DrawingPointCount() != 2 || !drawingDimensionCommitButton_->isEnabled()) {
            return fail("endpoint radius arc input state");
        }
        CommitDrawingDimensions();
        if (project_.Wires().size() != configuredArcStart + 1
            || project_.Wires().back().wire.Kind() != WireKind::CircularArc
            || std::abs(project_.Wires().back().wire.ArcData().radius - 4.0) > 1.0e-8) {
            return fail("endpoint radius arc drawing");
        }

        arcDrawingMode_->setCurrentIndex(2);
        arcDirectionBasis_->setCurrentIndex(arcDirectionBasis_->findData(0));
        arcDirectionAngle_->setValue(0.0);
        arcRadiusField_->setValue(2.0);
        arcExtentMode_->setCurrentIndex(arcExtentMode_->findData(0));
        arcExtentValue_->setValue(90.0);
        arcTurnSide_->setCurrentIndex(arcTurnSide_->findData(1.0));
        UpdateArcConfiguration();
        SetViewportTool(ViewportTool::DrawArc);
        click(viewport_->ScreenPoint({0.0, 2.0, 0.0}), Qt::ControlModifier);
        if (viewport_->DrawingPointCount() != 1 || !drawingDimensionCommitButton_->isEnabled()) {
            return fail("start tangent arc input state");
        }
        CommitDrawingDimensions();
        const Wire& tangentArc = project_.Wires().back().wire;
        if (project_.Wires().size() != configuredArcStart + 2
            || tangentArc.Kind() != WireKind::CircularArc
            || std::abs(tangentArc.ArcData().radius - 2.0) > 1.0e-8
            || std::abs(tangentArc.ArcData().sweepAngleRadians - kPi * 0.5) > 1.0e-8
            || kachakacha::geometry::Dot(MeasureWireTangent(tangentArc, 0.0), {1.0, 0.0, 0.0}) < 0.999999) {
            return fail("start tangent arc drawing");
        }
        arcDrawingMode_->setCurrentIndex(0);

        project_ = directEditSavedProject;
        undoStack_ = directEditSavedUndo;
        redoStack_ = directEditSavedRedo;
        referenceWireName_ = directEditSavedReference;
        modified_ = directEditSavedModified;
        setWindowTitle(directEditSavedTitle);
        RefreshModelViews(false);
        UpdateHistoryActions();
        activePlaneCombo_->setCurrentIndex(drawingPlaneIndex);
        viewport_->AlignToActiveWorkPlane();
        viewport_->RestoreViewFraming(directEditSavedViewTarget, directEditSavedViewScale);
    }

    SetViewportTool(ViewportTool::Select);
    sendMouse(QEvent::MouseMove, center + QPointF(-105.0, 90.0), Qt::NoButton, Qt::NoButton);
    if (viewport_->HoveredSelection().kind != CadSelectionKind::Wire
        || viewport_->HoveredSelection().index != static_cast<int>(directStart)) {
        return fail("wire hover hit");
    }
    SetViewportTool(ViewportTool::Coincident);
    click(center + QPointF(-60.0, 90.0));
    if (viewport_->CoincidencePicks().size() != 1
        || viewport_->CoincidencePicks().front().wireIndex != static_cast<int>(directStart)
        || viewport_->CoincidencePicks().front().endpoint != kachakacha::model::WireEndpoint::End) {
        return fail("direct endpoint coincidence pick");
    }
    viewport_->ClearCoincidencePicks();
    if (!viewport_->CoincidencePicks().empty()) {
        return fail("clear endpoint coincidence pick");
    }
    SetViewportTool(ViewportTool::Tangent);
    click(center + QPointF(-60.0, 90.0));
    if (viewport_->CoincidencePicks().size() != 1
        || viewport_->CoincidencePicks().front().wireIndex != static_cast<int>(directStart)) {
        return fail("direct tangent anchor pick");
    }
    const std::size_t tangentCountBeforeArc = project_.TangentConstraints().size();
    const std::size_t coincidenceCountBeforeArc = project_.CoincidentConstraints().size();
    const Wire arcBeforeTangent = project_.Wires()[directStart + 4].wire;
    click(center + QPointF(-20.0, 70.0));
    if (project_.TangentConstraints().size() != tangentCountBeforeArc + 1
        || project_.CoincidentConstraints().size() != coincidenceCountBeforeArc + 1
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart].wire.End(),
            project_.Wires()[directStart + 4].wire.Start(), 1.0e-8)
        || kachakacha::geometry::Dot(
            MeasureWireTangent(project_.Wires()[directStart].wire, 1.0),
            MeasureWireTangent(project_.Wires()[directStart + 4].wire, 0.0)) < 0.999999) {
        return fail("direct arc tangent connection");
    }
    Undo();
    if (project_.TangentConstraints().size() != tangentCountBeforeArc
        || project_.CoincidentConstraints().size() != coincidenceCountBeforeArc
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart + 4].wire.Start(), arcBeforeTangent.Start(), 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart + 4].wire.End(), arcBeforeTangent.End(), 1.0e-8)) {
        return fail("undo direct arc tangent connection");
    }
    viewport_->ClearCoincidencePicks();
    SetViewportTool(ViewportTool::Curvature);
    click(center + QPointF(-60.0, 90.0));
    if (viewport_->CoincidencePicks().size() != 1
        || viewport_->CoincidencePicks().front().wireIndex != static_cast<int>(directStart)) {
        return fail("direct curvature anchor pick");
    }
    viewport_->ClearCoincidencePicks();

    const std::size_t exactStart = project_.Wires().size();
    SetViewportTool(ViewportTool::DrawLine);
    click(center + QPointF(-205.0, 135.0));
    QLineEdit* exactLengthEditor = drawingLengthField_->findChild<QLineEdit*>();
    if (exactLengthEditor == nullptr) {
        return fail("find expression dimension editor");
    }
    exactLengthEditor->setText(QStringLiteral("(10/2)*3"));
    drawingLengthField_->interpretText();
    if (!kachakacha::geometry::AlmostEqual(drawingLengthField_->value(), 15.0, 1.0e-10)) {
        return fail("expression dimension field");
    }
    drawingLengthField_->setValue(12.5);
    drawingAngleField_->setValue(30.0);
    CommitDrawingDimensions();

    SetViewportTool(ViewportTool::DrawRectangle);
    const QPointF exactRectangleStart = center + QPointF(155.0, -120.0);
    click(exactRectangleStart);
    sendMouse(QEvent::MouseMove, exactRectangleStart + QPointF(-45.0, 30.0), Qt::NoButton, Qt::NoButton);
    drawingWidthField_->setValue(8.0);
    drawingHeightField_->setValue(4.0);
    CommitDrawingDimensions();

    SetViewportTool(ViewportTool::DrawCircle);
    click(center + QPointF(175.0, 125.0));
    drawingRadiusField_->setValue(3.25);
    CommitDrawingDimensions();

    SetViewportTool(ViewportTool::DrawLine);
    const QPointF constrainedStart = center + QPointF(-205.0, -135.0);
    const QPointF constrainedEnd = constrainedStart + QPointF(65.0, 25.0);
    click(constrainedStart);
    sendMouse(QEvent::MouseMove, constrainedEnd, Qt::NoButton, Qt::NoButton, Qt::ShiftModifier);
    sendMouse(QEvent::MouseButtonPress, constrainedEnd, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    sendMouse(QEvent::MouseButtonRelease, constrainedEnd, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);

    const std::optional<WorkPlane> exactPlane = project_.FindWorkPlane(drawingPlaneName);
    kachakacha::model::PlaneCoordinates constrainedStartCoordinates;
    kachakacha::model::PlaneCoordinates constrainedEndCoordinates;
    if (exactPlane.has_value() && project_.Wires().size() == exactStart + 4) {
        constrainedStartCoordinates = exactPlane->Project(project_.Wires()[exactStart + 3].wire.Start());
        constrainedEndCoordinates = exactPlane->Project(project_.Wires()[exactStart + 3].wire.End());
    }
    if (project_.Wires().size() != exactStart + 4
        || !kachakacha::geometry::AlmostEqual(
            (project_.Wires()[exactStart].wire.End() - project_.Wires()[exactStart].wire.Start()).Length(),
            12.5, 1.0e-8)
        || project_.Wires()[exactStart + 1].wire.Kind() != WireKind::Polyline
        || !kachakacha::geometry::AlmostEqual(
            (project_.Wires()[exactStart + 1].wire.ControlPoints()[1]
                - project_.Wires()[exactStart + 1].wire.ControlPoints()[0]).Length(),
            8.0, 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(
            (project_.Wires()[exactStart + 1].wire.ControlPoints()[2]
                - project_.Wires()[exactStart + 1].wire.ControlPoints()[1]).Length(),
            4.0, 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[exactStart + 2].wire.ArcData().radius, 3.25, 1.0e-8)
        || !exactPlane.has_value()
        || (std::abs(constrainedEndCoordinates.u - constrainedStartCoordinates.u) > 1.0e-8
            && std::abs(constrainedEndCoordinates.v - constrainedStartCoordinates.v) > 1.0e-8)) {
        return fail("dimension-driven direct drawing");
    }
    Undo();
    Undo();
    Undo();
    Undo();
    if (project_.Wires().size() != exactStart) {
        return fail("undo dimension-driven drawing");
    }

    const auto sendKey = [this](int key, const QString& text = QString()) {
        QWidget* target = QApplication::focusWidget();
        if (target == nullptr) {
            target = viewport_;
        }
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
        QApplication::sendEvent(target, &press);
        QWidget* releaseTarget = QApplication::focusWidget();
        if (releaseTarget == nullptr) {
            releaseTarget = target;
        }
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
        QApplication::sendEvent(releaseTarget, &release);
    };
    const auto typeExpression = [&sendKey](const QString& expression) {
        for (const QChar character : expression) {
            sendKey(character.unicode(), QString(character));
        }
    };
    SetViewportTool(ViewportTool::DrawLine);
    click(center + QPointF(-205.0, 155.0));
    typeExpression(QStringLiteral("(10/2)*3"));
    sendKey(Qt::Key_Tab);
    typeExpression(QStringLiteral("(45/3)*2"));
    sendKey(Qt::Key_Return);
    if (project_.Wires().size() != exactStart + 1
        || !kachakacha::geometry::AlmostEqual(
            (project_.Wires().back().wire.End() - project_.Wires().back().wire.Start()).Length(),
            15.0, 1.0e-8)) {
        return fail("cursor expression drawing");
    }
    const auto expressionStart = exactPlane->Project(project_.Wires().back().wire.Start());
    const auto expressionEnd = exactPlane->Project(project_.Wires().back().wire.End());
    if (!kachakacha::geometry::AlmostEqual(
            std::atan2(
                expressionEnd.v - expressionStart.v,
                expressionEnd.u - expressionStart.u) * 180.0 / kPi,
            30.0, 1.0e-8)) {
        return fail("cursor expression angle");
    }
    Undo();
    if (project_.Wires().size() != exactStart) {
        return fail("undo cursor expression drawing");
    }

    const std::size_t radiusCircleIndex = directStart + 2;
    const double radiusBeforeConstraint = project_.Wires()[radiusCircleIndex].wire.ArcData().radius;
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(radiusCircleIndex)}, true);
    editWireLockRadius_->setChecked(true);
    editArcRadius_->setValue(6.5);
    ApplySelectedEdit();
    if (project_.Wires()[radiusCircleIndex].metadata.curveConstraints.radiusMillimeters != 6.5
        || std::abs(project_.Wires()[radiusCircleIndex].wire.ArcData().radius - 6.5) > 1.0e-8) {
        return fail("apply radius constraint");
    }
    Undo();
    if (!project_.Wires()[radiusCircleIndex].metadata.curveConstraints.Empty()
        || std::abs(project_.Wires()[radiusCircleIndex].wire.ArcData().radius - radiusBeforeConstraint) > 1.0e-8) {
        return fail("undo radius constraint");
    }
    Redo();
    if (project_.Wires()[radiusCircleIndex].metadata.curveConstraints.radiusMillimeters != 6.5
        || std::abs(project_.Wires()[radiusCircleIndex].wire.ArcData().radius - 6.5) > 1.0e-8) {
        return fail("redo radius constraint");
    }

    const Wire offsetLineSource = project_.Wires()[directStart].wire;
    const double offsetCircleRadius = project_.Wires()[directStart + 2].wire.ArcData().radius;
    const std::size_t offsetStart = project_.Wires().size();
    SetViewportTool(ViewportTool::Select);
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(directStart)},
        {CadSelectionKind::Wire, static_cast<int>(directStart + 2)},
    }, true);
    wireOffsetDistance_->setValue(1.25);
    wireOffsetSide_->setCurrentIndex(0);
    UpdateWireOffsetPreview();
    if (!wireOffsetApplyButton_->isEnabled() || viewport_->WireOffsetPreviewCount() != 2) {
        return fail("multi-wire offset preview");
    }
    ApplyWireOffset();
    if (project_.Wires().size() != offsetStart + 2
        || !kachakacha::geometry::AlmostEqual(
            (project_.Wires()[offsetStart].wire.Start() - offsetLineSource.Start()).Length(), 1.25, 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(
            std::abs(project_.Wires()[offsetStart + 1].wire.ArcData().radius - offsetCircleRadius),
            1.25, 1.0e-8)
        || project_.Wires()[offsetStart + 1].metadata.curveConstraints.radiusMillimeters
            != project_.Wires()[offsetStart + 1].wire.ArcData().radius
        || project_.Wires()[offsetStart].metadata.sourcePlaneName != drawingPlaneName
        || project_.Wires()[offsetStart + 1].metadata.planePolicy != WirePlanePolicy::ReferenceOnly) {
        return fail("multi-wire offset result");
    }
    Undo();
    if (project_.Wires().size() != offsetStart) {
        return fail("undo multi-wire offset");
    }

    SetViewportTool(ViewportTool::Select);
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(directStart)},
        {CadSelectionKind::Wire, static_cast<int>(directStart + 1)},
    }, true);
    const Wire beforeMove = project_.Wires()[directStart].wire;
    SetViewportTool(ViewportTool::MoveSelection);
    click(center + QPointF(-35.0, -150.0));
    click(center + QPointF(35.0, -150.0));
    if (project_.Wires().size() != directStart + 6) {
        return fail("direct move count");
    }
    const Vector3 moveDelta = project_.Wires()[directStart].wire.Start() - beforeMove.Start();
    if (moveDelta.LengthSquared() <= 1.0e-9
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart].wire.End() - beforeMove.End(), moveDelta, 1.0e-8)) {
        return fail("direct move geometry");
    }
    Undo();
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[directStart].wire.Start(), beforeMove.Start())) {
        return fail("undo direct move");
    }
    Redo();
    if (!kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart].wire.Start(), beforeMove.Start() + moveDelta)) {
        return fail("redo direct move");
    }

    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(directStart)},
        {CadSelectionKind::Wire, static_cast<int>(directStart + 1)},
    }, true);
    const Wire beforeRotation = project_.Wires()[directStart].wire;
    SetViewportTool(ViewportTool::RotateSelection);
    click(center + QPointF(5.0, 145.0));
    click(center + QPointF(75.0, 145.0));
    click(center + QPointF(5.0, 75.0));
    if (kachakacha::geometry::AlmostEqual(project_.Wires()[directStart].wire.Start(), beforeRotation.Start(), 1.0e-8)
        || std::abs((project_.Wires()[directStart].wire.End() - project_.Wires()[directStart].wire.Start()).Length()
                - (beforeRotation.End() - beforeRotation.Start()).Length())
            > 1.0e-8) {
        return fail("direct rotation geometry");
    }
    const Wire afterRotation = project_.Wires()[directStart].wire;
    Undo();
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[directStart].wire.Start(), beforeRotation.Start(), 1.0e-8)) {
        return fail("undo direct rotation");
    }
    Redo();
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[directStart].wire.Start(), afterRotation.Start(), 1.0e-8)) {
        return fail("redo direct rotation");
    }

    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(directStart)},
        {CadSelectionKind::Wire, static_cast<int>(directStart + 1)},
    }, true);
    const std::size_t copyStart = project_.Wires().size();
    SetViewportTool(ViewportTool::CopySelection);
    click(center + QPointF(-170.0, 145.0));
    click(center + QPointF(-170.0, 75.0));
    if (project_.Wires().size() != copyStart + 2
        || project_.Wires()[copyStart].wire.Kind() != WireKind::Line
        || project_.Wires()[copyStart + 1].wire.Kind() != WireKind::Polyline
        || project_.Wires()[copyStart].metadata.sourcePlaneName != drawingPlaneName) {
        return fail("direct copy");
    }
    Undo();
    if (project_.Wires().size() != copyStart) {
        return fail("undo direct copy");
    }
    Redo();
    if (project_.Wires().size() != copyStart + 2) {
        return fail("redo direct copy");
    }

    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(copyStart)},
        {CadSelectionKind::Wire, static_cast<int>(copyStart + 1)},
    }, true);
    const std::size_t mirrorStart = project_.Wires().size();
    SetViewportTool(ViewportTool::MirrorSelection);
    click(center + QPointF(175.0, -125.0));
    click(center + QPointF(175.0, 125.0));
    if (project_.Wires().size() != mirrorStart + 2
        || project_.Wires()[mirrorStart].wire.Kind() != WireKind::Line
        || project_.Wires()[mirrorStart + 1].wire.Kind() != WireKind::Polyline
        || project_.Wires()[mirrorStart].name.find("mirror") == std::string::npos) {
        return fail("direct mirror copy");
    }

    const std::size_t meetStart = project_.Wires().size();
    AddViewportLine({-14.0, -9.0, 0.0}, {-5.0, -9.0, 0.0});
    AddViewportLine({-2.0, -5.0, 0.0}, {-2.0, 3.0, 0.0});
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(meetStart)},
        {CadSelectionKind::Wire, static_cast<int>(meetStart + 1)},
    }, true);
    meetLinesAction_->trigger();
    const Vector3 expectedIntersection{-2.0, -9.0, 0.0};
    if (!kachakacha::geometry::AlmostEqual(project_.Wires()[meetStart].wire.End(), expectedIntersection)
        || !kachakacha::geometry::AlmostEqual(project_.Wires()[meetStart + 1].wire.Start(), expectedIntersection)) {
        return fail("meet selected lines");
    }
    Undo();
    if (kachakacha::geometry::AlmostEqual(project_.Wires()[meetStart].wire.End(), expectedIntersection)) {
        return fail("undo meet selected lines");
    }
    Redo();

    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(meetStart)}, true);
    SetReferenceFromSelection();
    if (!referenceWireName_.has_value()
        || *referenceWireName_ != project_.Wires()[meetStart].name
        || !viewport_->Selections().empty()) {
        return fail("set explicit reference line");
    }
    UseReferenceForPlaneRotation();
    if (planeMethod_->currentIndex() != 4
        || !kachakacha::geometry::AlmostEqual(ReadVector3(rotateAxisPoint_), project_.Wires()[meetStart].wire.Start())
        || !kachakacha::geometry::AlmostEqual(
            ReadVector3(rotateAxisDirection_),
            project_.Wires()[meetStart].wire.End() - project_.Wires()[meetStart].wire.Start())) {
        return fail("reference line as plane rotation axis");
    }
    const std::size_t referenceMirrorStart = project_.Wires().size();
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(directStart + 2)}, true);
    mirrorToolAction_->trigger();
    if (project_.Wires().size() != referenceMirrorStart + 1
        || project_.Wires()[referenceMirrorStart].wire.Kind() != WireKind::Circle
        || project_.Wires()[referenceMirrorStart].name.find("mirror") == std::string::npos) {
        return fail("mirror from explicit reference line");
    }

    const QPointF splitLineStart = center + QPointF(20.0, 145.0);
    const QPointF splitLineEnd = center + QPointF(140.0, 145.0);
    const std::size_t splitStart = project_.Wires().size();
    SetViewportTool(ViewportTool::DrawLine);
    click(splitLineStart);
    click(splitLineEnd);
    if (project_.Wires().size() != splitStart + 1) {
        return fail("create line for direct split");
    }
    const Wire splitSource = project_.Wires()[splitStart].wire;
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(splitStart)}, true);
    SetViewportTool(ViewportTool::SplitWire);
    click((splitLineStart + splitLineEnd) * 0.5);
    if (project_.Wires().size() != splitStart + 2
        || !kachakacha::geometry::AlmostEqual(project_.Wires()[splitStart].wire.End(), project_.Wires()[splitStart + 1].wire.Start(), 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(project_.Wires()[splitStart].wire.Start(), splitSource.Start(), 1.0e-8)
        || !kachakacha::geometry::AlmostEqual(project_.Wires()[splitStart + 1].wire.End(), splitSource.End(), 1.0e-8)) {
        return fail("direct split geometry");
    }
    Undo();
    if (project_.Wires().size() != splitStart + 1
        || !kachakacha::geometry::AlmostEqual(project_.Wires()[splitStart].wire.Start(), splitSource.Start(), 1.0e-8)) {
        return fail("undo direct split");
    }
    Redo();
    if (project_.Wires().size() != splitStart + 2) {
        return fail("redo direct split");
    }
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(splitStart)},
        {CadSelectionKind::Wire, static_cast<int>(splitStart + 1)},
    }, true);
    joinWiresAction_->trigger();
    if (project_.Wires().size() != splitStart + 1
        || project_.Wires()[splitStart].wire.Kind() != WireKind::Polyline
        || (!kachakacha::geometry::AlmostEqual(project_.Wires()[splitStart].wire.Start(), splitSource.Start(), 1.0e-8)
            && !kachakacha::geometry::AlmostEqual(project_.Wires()[splitStart].wire.End(), splitSource.Start(), 1.0e-8))) {
        return fail("join selected split parts");
    }
    Undo();
    if (project_.Wires().size() != splitStart + 2) {
        return fail("undo direct join");
    }
    Redo();
    if (project_.Wires().size() != splitStart + 1) {
        return fail("redo direct join");
    }

    const auto exportPlane = project_.FindWorkPlane(drawingPlaneName);
    if (!exportPlane.has_value()) {
        return fail("find planar export workplane");
    }
    std::vector<kachakacha::model::NamedWire> exportWires;
    for (const auto& wire : project_.Wires()) {
        if (WireLiesOnWorkPlane(wire.wire, *exportPlane)) {
            exportWires.push_back(wire);
        }
    }
    std::ostringstream svgOutput;
    std::ostringstream dxfOutput;
    WritePlanarSvg(svgOutput, *exportPlane, exportWires);
    WritePlanarDxf(dxfOutput, *exportPlane, exportWires);
    if (svgOutput.str().find("mm\" height=") == std::string::npos
        || svgOutput.str().find("<polyline") == std::string::npos
        || dxfOutput.str().find("$INSUNITS\n70\n4") == std::string::npos) {
        return fail("planar output from UI project");
    }

    const std::size_t compositeWireStart = project_.Wires().size();
    const std::size_t compositeSurfaceStart = project_.Surfaces().size();
    project_.AddWire("__ui_composite_bottom", Wire::Line(
        {0.0, 0.0, 0.0}, {8.0, 0.0, 0.0}));
    project_.AddWire("__ui_composite_right", Wire::CircularArcThroughThreePoints(
        {8.0, 0.0, 0.0}, {10.0, 3.0, 0.0}, {8.0, 6.0, 0.0}));
    project_.AddWire("__ui_composite_top", Wire::Line(
        {0.0, 6.0, 0.0}, {8.0, 6.0, 0.0}));
    project_.AddWire("__ui_composite_left", Wire::Line(
        {0.0, 6.0, 0.0}, {0.0, 0.0, 0.0}));
    RefreshModelViews(false);
    surfaceType_->setCurrentIndex(0);
    surfaceName_->setText("__ui_composite_surface");
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(compositeWireStart + 2)},
        {CadSelectionKind::Wire, static_cast<int>(compositeWireStart)},
        {CadSelectionKind::Wire, static_cast<int>(compositeWireStart + 3)},
        {CadSelectionKind::Wire, static_cast<int>(compositeWireStart + 1)},
    }, true);
    CreateSurfaceFromSelection();
    if (project_.Surfaces().size() != compositeSurfaceStart + 1
        || project_.Surfaces().back().surface.Kind() != SurfaceKind::Planar
        || project_.Surfaces().back().sourceWireNames.size() != 4
        || !project_.Surfaces().back().surface.FirstBoundary().IsClosed()) {
        return fail("create planar surface from separate lines and curve");
    }
    Undo();
    if (project_.Surfaces().size() != compositeSurfaceStart) {
        return fail("undo composite planar surface");
    }

    const std::size_t surfaceWireStart = project_.Wires().size();
    const std::size_t surfaceStart = project_.Surfaces().size();
    const std::size_t plateStart = project_.Plates().size();
    const std::size_t bodyStart = project_.Bodies().size();
    project_.AddWorkPlane("__ui_section_a_plane", WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));
    project_.AddWorkPlane("__ui_section_mid_plane", WorkPlane::FromPointNormal({6.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));
    project_.AddWorkPlane("__ui_section_b_plane", WorkPlane::FromPointNormal({12.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));
    project_.AddWorkPlane("__ui_light_plan", WorkPlane::FromPointNormal({0.0, 0.0, 12.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}));
    project_.AddWire("__ui_section_a", Wire::CubicBezier(
        {0.0, -6.0, 0.0}, {0.0, -2.0, 3.0}, {0.0, 2.0, 3.0}, {0.0, 6.0, 0.0}),
        WireMetadata{"__ui_section_a_plane", WirePlanePolicy::ReferenceOnly, {}, {}});
    project_.AddWire("__ui_section_mid", Wire::CubicBezier(
        {6.0, -6.0, 0.0}, {6.0, -2.0, 6.0}, {6.0, 2.0, 6.0}, {6.0, 6.0, 0.0}),
        WireMetadata{"__ui_section_mid_plane", WirePlanePolicy::ReferenceOnly, {}, {}});
    project_.AddWire("__ui_section_b", Wire::CubicBezier(
        {12.0, -6.0, 0.0}, {12.0, -2.0, 5.0}, {12.0, 2.0, 5.0}, {12.0, 6.0, 0.0}),
        WireMetadata{"__ui_section_b_plane", WirePlanePolicy::ReferenceOnly, {}, {}});
    project_.AddWire("__ui_light_plan_circle", Wire::Circle(
        {6.0, 0.0, 12.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1.25),
        WireMetadata{"__ui_light_plan", WirePlanePolicy::ReferenceOnly, {}, {}});
    RefreshModelViews(false);

    plateName_->setText("__ui_direct_variable_plate");
    plateThickness_->setValue(0.4);
    plateVariableThickness_->setChecked(true);
    plateEndThickness_->setValue(0.9);
    plateDirection_->setCurrentIndex(1);
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart)},
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart + 1)},
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart + 2)},
    }, true);
    CreatePlateFromSelectedWires();
    if (project_.Surfaces().size() != surfaceStart + 1
        || project_.Plates().size() != plateStart + 1
        || !project_.Plates().back().plate.HasVariableThickness()
        || std::abs(project_.Plates().back().plate.EndThickness() - 0.9) > 1.0e-12) {
        return fail("direct variable plate from selected wires");
    }
    Undo();
    if (project_.Surfaces().size() != surfaceStart || project_.Plates().size() != plateStart) {
        return fail("undo direct variable plate");
    }
    plateVariableThickness_->setChecked(false);

    surfaceType_->setCurrentIndex(2);
    surfaceName_->setText("__ui_nose_skin");
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart)},
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart + 1)},
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart + 2)},
    }, true);
    CreateSurfaceFromSelection();
    if (project_.Surfaces().size() != surfaceStart + 1
        || project_.Surfaces()[surfaceStart].surface.Kind() != SurfaceKind::Loft
        || viewport_->Selection().kind != CadSelectionKind::Surface) {
        return fail("create loft surface from selected sections");
    }
    Undo();
    if (project_.Surfaces().size() != surfaceStart) {
        return fail("undo loft surface");
    }
    Redo();
    if (project_.Surfaces().size() != surfaceStart + 1) {
        return fail("redo loft surface");
    }

    projectionSurface_->setCurrentText("__ui_nose_skin");
    projectionPlane_->setCurrentText("__ui_light_plan");
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart + 3)},
        {CadSelectionKind::Surface, static_cast<int>(surfaceStart)},
    }, true);
    ProjectSelectedWiresToSurface();
    const std::size_t projectedLightIndex = surfaceWireStart + 4;
    if (project_.Wires().size() != surfaceWireStart + 5
        || !project_.Wires()[projectedLightIndex].projection.has_value()
        || project_.Wires()[projectedLightIndex].projection->sourceWireName != "__ui_light_plan_circle"
        || !project_.Wires()[projectedLightIndex].wire.IsClosed()) {
        return fail("project planar light drawing to selected surface");
    }
    Undo();
    if (project_.Wires().size() != surfaceWireStart + 4) {
        return fail("undo surface projection");
    }
    Redo();
    if (project_.Wires().size() != surfaceWireStart + 5
        || !project_.Wires()[projectedLightIndex].projection.has_value()) {
        return fail("redo surface projection");
    }
    const std::string projectedLightName = project_.Wires()[projectedLightIndex].name;

    plateSurface_->setCurrentText("__ui_nose_skin");
    plateName_->setText("__ui_nose_plate");
    plateThickness_->setValue(0.5);
    plateDirection_->setCurrentIndex(1);
    plateMaterial_->setCurrentIndex(0);
    CreatePlateFromSurface();
    if (project_.Plates().size() != plateStart + 1
        || project_.Plates()[plateStart].sourceSurfaceName != "__ui_nose_skin"
        || std::abs(project_.Plates()[plateStart].plate.Thickness() - 0.5) > 1.0e-12
        || project_.Surfaces()[surfaceStart].visible
        || viewport_->Selection().kind != CadSelectionKind::Plate) {
        return fail("create plate from loft surface");
    }
    Undo();
    if (project_.Plates().size() != plateStart || !project_.Surfaces()[surfaceStart].visible) {
        return fail("undo plate creation");
    }
    Redo();
    if (project_.Plates().size() != plateStart + 1 || project_.Surfaces()[surfaceStart].visible) {
        return fail("redo plate creation");
    }

    const std::size_t lightCaseWireCount = project_.Wires().size();
    const std::size_t lightCaseSurfaceCount = project_.Surfaces().size();
    lightCaseRootName_->setText(QStringLiteral("__ui_light_root"));
    lightCaseSurfaceName_->setText(QStringLiteral("__ui_light_case_side"));
    lightCaseDirectionMode_->setCurrentIndex(2);
    lightCaseDirection_[0]->setValue(0.0);
    lightCaseDirection_[1]->setValue(0.0);
    lightCaseDirection_[2]->setValue(-1.0);
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(surfaceWireStart + 3)},
        {CadSelectionKind::Plate, static_cast<int>(plateStart)},
    }, true);
    CreateProtrudingLightCase();
    if (project_.Wires().size() != lightCaseWireCount + 1
        || project_.Surfaces().size() != lightCaseSurfaceCount + 1
        || !project_.Wires().back().projection.has_value()
        || project_.Wires().back().projection->targetSurfaceName != "__ui_nose_skin"
        || project_.Surfaces().back().sourceWireNames
            != std::vector<std::string>{"__ui_light_plan_circle", "__ui_light_root"}) {
        return fail("create protruding light case from selected front and plate");
    }
    Undo();
    if (project_.Wires().size() != lightCaseWireCount
        || project_.Surfaces().size() != lightCaseSurfaceCount) {
        return fail("undo protruding light case");
    }
    Redo();
    if (project_.Wires().size() != lightCaseWireCount + 1
        || project_.Surfaces().size() != lightCaseSurfaceCount + 1
        || !project_.Wires().back().projection.has_value()) {
        return fail("redo protruding light case");
    }

    jigSurface_->setCurrentText(QStringLiteral("__ui_nose_skin"));
    jigName_->setText(QStringLiteral("__ui_nose_jig"));
    jigSide_->setCurrentIndex(jigSide_->findData(static_cast<int>(JigSide::Negative)));
    jigClearance_->setValue(0.15);
    jigThickness_->setValue(3.0);
    jigMinimumWall_->setValue(1.2);
    CreateSurfaceJig();
    if (project_.Bodies().size() != bodyStart + 1
        || project_.Bodies()[bodyStart].sourceSurfaceName != "__ui_nose_skin"
        || project_.Bodies()[bodyStart].body.Side() != JigSide::Negative
        || std::abs(project_.Bodies()[bodyStart].body.ClearanceMillimeters() - 0.15) > 1.0e-12
        || viewport_->Selection().kind != CadSelectionKind::Body) {
        return fail("create forming jig from loft surface");
    }
    Undo();
    if (project_.Bodies().size() != bodyStart) {
        return fail("undo forming jig creation");
    }
    Redo();
    if (project_.Bodies().size() != bodyStart + 1) {
        return fail("redo forming jig creation");
    }
    UpdateSelection({CadSelectionKind::Body, static_cast<int>(bodyStart)}, true);
    jigThickness_->setValue(4.0);
    UpdateSelectedBody();
    if (std::abs(project_.Bodies()[bodyStart].body.ThicknessMillimeters() - 4.0) > 1.0e-12
        || !jigAnalysisLabel_->text().contains(QStringLiteral("満たします"))) {
        return fail("update forming jig and analyze wall");
    }
    Undo();
    if (std::abs(project_.Bodies()[bodyStart].body.ThicknessMillimeters() - 3.0) > 1.0e-12) {
        return fail("undo forming jig update");
    }
    Redo();

    UpdateSelection({CadSelectionKind::Plate, static_cast<int>(plateStart)}, true);
    plateThickness_->setValue(0.7);
    plateVariableThickness_->setChecked(true);
    plateEndThickness_->setValue(1.1);
    plateDirection_->setCurrentIndex(0);
    plateMaterial_->setCurrentIndex(1);
    UpdateSelectedPlate();
    if (std::abs(project_.Plates()[plateStart].plate.Thickness() - 0.7) > 1.0e-12
        || std::abs(project_.Plates()[plateStart].plate.EndThickness() - 1.1) > 1.0e-12
        || project_.Plates()[plateStart].plate.Direction() != PlateThicknessDirection::Positive
        || project_.Plates()[plateStart].material != "paper") {
        return fail("update selected plate properties");
    }
    Undo();
    if (std::abs(project_.Plates()[plateStart].plate.Thickness() - 0.5) > 1.0e-12) {
        return fail("undo plate property update");
    }
    Redo();
    if (std::abs(project_.Plates()[plateStart].plate.Thickness() - 0.7) > 1.0e-12
        || std::abs(project_.Plates()[plateStart].plate.EndThickness() - 1.1) > 1.0e-12) {
        return fail("redo plate property update");
    }

    const std::size_t beforePlateOffsetWire = project_.Wires().size();
    UpdateSelections({
        {CadSelectionKind::Plate, static_cast<int>(plateStart)},
        {CadSelectionKind::Wire, static_cast<int>(projectedLightIndex)},
    }, true);
    plateOffsetLayer_->setCurrentIndex(0);
    CreatePlateOffsetWires();
    if (project_.Wires().size() != beforePlateOffsetWire + 1
        || !project_.Wires().back().plateOffset.has_value()
        || project_.Wires().back().plateOffset->plateName != "__ui_nose_plate") {
        return fail("create plate thickness position wire");
    }
    Undo();
    if (project_.Wires().size() != beforePlateOffsetWire) {
        return fail("undo plate thickness position wire");
    }

    UpdateSelections({
        {CadSelectionKind::Plate, static_cast<int>(plateStart)},
        {CadSelectionKind::Wire, static_cast<int>(projectedLightIndex)},
    }, true);
    AddSelectedPlateOpenings();
    if (project_.Plates()[plateStart].openingWireNames
        != std::vector<std::string>{projectedLightName}) {
        return fail("add selected plate opening");
    }
    Undo();
    if (!project_.Plates()[plateStart].openingWireNames.empty()) {
        return fail("undo plate opening");
    }
    Redo();
    if (project_.Plates()[plateStart].openingWireNames.size() != 1) {
        return fail("redo plate opening");
    }
    UpdateSelections({
        {CadSelectionKind::Plate, static_cast<int>(plateStart)},
        {CadSelectionKind::Wire, static_cast<int>(projectedLightIndex)},
    }, true);
    RemoveSelectedPlateOpenings();
    if (!project_.Plates()[plateStart].openingWireNames.empty()) {
        return fail("remove selected plate opening");
    }
    Undo();
    if (project_.Plates()[plateStart].openingWireNames.size() != 1) {
        return fail("undo removing plate opening");
    }
    UpdateSelection({CadSelectionKind::Plate, static_cast<int>(plateStart)}, true);
    if (!infoLabel_->text().contains(QStringLiteral("工作判定"))
        || !infoLabel_->text().contains(QStringLiteral("開口"))) {
        return fail("plate opening and forming information");
    }

    plateSplitAxis_->setCurrentIndex(1);
    plateSplitPosition_->setValue(25.0);
    UpdateSelection({CadSelectionKind::Plate, static_cast<int>(plateStart)}, true);
    SplitSelectedPlate();
    if (project_.Plates().size() != plateStart + 2
        || std::abs(project_.Plates()[plateStart].plate.Range().maximumV - 0.25) > 1.0e-12
        || std::abs(project_.Plates()[plateStart + 1].plate.Range().minimumV - 0.25) > 1.0e-12
        || !project_.Plates()[plateStart].openingWireNames.empty()
        || project_.Plates()[plateStart + 1].openingWireNames
            != std::vector<std::string>{projectedLightName}) {
        return fail("split selected plate and assign opening");
    }
    if (std::abs(project_.Plates()[plateStart].plate.EndThickness() - 0.8) > 1.0e-12
        || std::abs(project_.Plates()[plateStart + 1].plate.Thickness() - 0.8) > 1.0e-12
        || std::abs(project_.Plates()[plateStart + 1].plate.EndThickness() - 1.1) > 1.0e-12) {
        return fail("split variable plate thickness profile");
    }
    const std::string firstPieceName = project_.Plates()[plateStart].name;
    const std::string secondPieceName = project_.Plates()[plateStart + 1].name;
    Undo();
    if (project_.Plates().size() != plateStart + 1
        || !project_.Plates()[plateStart].plate.Range().IsFull()) {
        return fail("undo plate split");
    }
    Redo();
    if (project_.Plates().size() != plateStart + 2
        || project_.Plates()[plateStart].name != firstPieceName
        || project_.Plates()[plateStart + 1].name != secondPieceName) {
        return fail("redo plate split");
    }
    try {
        PlateFlatPatternOptions selfTestOptions;
        selfTestOptions.uSegments = 40;
        selfTestOptions.vSegments = 16;
        selfTestOptions.openingSamples = 48;
        const auto firstPattern = BuildPlateFlatPattern(project_, project_.Plates()[plateStart], selfTestOptions);
        const auto secondPattern = BuildPlateFlatPattern(project_, project_.Plates()[plateStart + 1], selfTestOptions);
        PlatePdfOptions pdfOptions;
        pdfOptions.overlapMillimeters = platePdfOverlap_->value();
        const auto pdfLayout = CalculatePlatePdfLayout(secondPattern, pdfOptions);
        std::ostringstream flatSvg;
        std::ostringstream flatDxf;
        WritePlateFlatPatternSvg(flatSvg, secondPattern, selfTestOptions);
        WritePlateFlatPatternDxf(flatDxf, secondPattern);
        if (firstPattern.openings.size() + secondPattern.openings.size() != 1
            || flatSvg.str().find("CUT_OUTER") == std::string::npos
            || flatSvg.str().find("CUT_OPENING") == std::string::npos
            || flatDxf.str().find("$INSUNITS\n70\n4") == std::string::npos
            || pdfLayout.PageCount() < 1) {
            return fail("split plate flat-pattern output");
        }
    } catch (const std::exception&) {
        return fail("build split plate flat patterns");
    }

    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(projectedLightIndex)}, true);
    HideSelected();
    if (project_.Wires()[projectedLightIndex].visible) {
        return fail("hide selected projected wire");
    }
    Undo();
    if (!project_.Wires()[projectedLightIndex].visible) {
        return fail("undo hide selected");
    }
    Redo();
    if (project_.Wires()[projectedLightIndex].visible) {
        return fail("redo hide selected");
    }
    ShowAllObjects();
    if (!project_.Wires()[projectedLightIndex].visible || !project_.Surfaces()[surfaceStart].visible) {
        return fail("show all objects");
    }

    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(initialWireCount)}, false);
    editWireConstruction_->setChecked(true);
    ApplySelectedEdit();
    if (!project_.Wires()[initialWireCount].metadata.construction) {
        return fail("set construction wire");
    }
    Undo();
    if (project_.Wires()[initialWireCount].metadata.construction) {
        return fail("undo construction wire");
    }
    Redo();
    if (!project_.Wires()[initialWireCount].metadata.construction) {
        return fail("redo construction wire");
    }

    std::ostringstream directDrawingScript;
    WriteProjectScript(directDrawingScript, project_);
    std::istringstream directDrawingInput(directDrawingScript.str());
    const Project reloadedProject = LoadProjectScript(directDrawingInput, "direct-editing-self-test");
    if (reloadedProject.Wires().size() != project_.Wires().size()
        || reloadedProject.Wires()[directStart + 5].wire.Kind() != WireKind::CubicBezier
        || reloadedProject.Wires()[constrainedLineIndex].metadata.lineConstraints.lengthMillimeters != 8.0
        || reloadedProject.Wires()[constrainedLineIndex].metadata.lineConstraints.angleDegrees != 0.0
        || !reloadedProject.Wires()[initialWireCount].metadata.construction
        || reloadedProject.Wires()[mirrorStart].metadata.sourcePlaneName != drawingPlaneName
        || reloadedProject.Wires()[referenceMirrorStart].wire.Kind() != WireKind::Circle
        || reloadedProject.Wires()[splitStart].wire.Kind() != WireKind::Polyline
        || reloadedProject.Surfaces().size() != project_.Surfaces().size()
        || reloadedProject.Surfaces()[surfaceStart].surface.Kind() != SurfaceKind::Loft
        || reloadedProject.Plates().size() != project_.Plates().size()
        || reloadedProject.Plates()[plateStart].sourceSurfaceName != "__ui_nose_skin"
        || std::abs(reloadedProject.Plates()[plateStart].plate.EndThickness() - 0.8) > 1.0e-12
        || std::abs(reloadedProject.Plates()[plateStart].plate.Range().maximumV - 0.25) > 1.0e-12
        || reloadedProject.Plates()[plateStart + 1].openingWireNames
            != std::vector<std::string>{projectedLightName}
        || reloadedProject.Bodies().size() != project_.Bodies().size()
        || reloadedProject.Bodies()[bodyStart].sourceSurfaceName != "__ui_nose_skin"
        || std::abs(reloadedProject.Bodies()[bodyStart].body.ThicknessMillimeters() - 4.0) > 1.0e-12
        || !reloadedProject.Wires()[projectedLightIndex].projection.has_value()
        || !kachakacha::geometry::AlmostEqual(reloadedProject.Wires()[meetStart].wire.End(), expectedIntersection)) {
        return fail("save and reload direct editing");
    }

    SetViewportTool(ViewportTool::Select);
    viewport_->SetIsometricView();
    viewport_->FitAll();
    toolsTabs_->setCurrentIndex(6);
    UpdateSelection({CadSelectionKind::Plate, static_cast<int>(plateStart + 1)}, true);
    if (!plateFlatPatternSummary_->text().contains(QStringLiteral("PDF"))) {
        const std::string failure = "plate PDF output summary: "
            + plateFlatPatternSummary_->text().toUtf8().toStdString();
        return fail(failure.c_str());
    }
    try {
        const auto expectedGuide = BuildPlateAssemblyGuide(
            project_, project_.Plates()[plateStart + 1], PlateFlatPatternOptionsFromUi());
        const auto expectedMotion = BuildPlateAssemblyMotion(
            project_,
            project_.Plates()[plateStart + 1],
            static_cast<double>(plateAssemblyProgress_->value()) / 100.0,
            PlateFlatPatternOptionsFromUi());
        const std::optional<int> expectedPiece = SelectedPlateAssemblyPiece();
        const std::size_t expectedPanelCount = expectedPiece.has_value()
            ? static_cast<std::size_t>(std::count(
                expectedMotion.pieceIndices.begin(), expectedMotion.pieceIndices.end(),
                *expectedPiece))
            : expectedMotion.panels.size();
        if (!plateAssemblyGuidePreview_->isChecked()
            || viewport_->PlateAssemblyFoldGuideCount() != expectedGuide.foldLines.size()
            || viewport_->PlateAssemblyReliefGuideCount()
                != expectedGuide.reliefCuts.size() + expectedGuide.splitLines.size()
            || !plateAssemblyApproximationPreview_->isChecked()
            || viewport_->PlateAssemblyApproximationPanelCount()
                != expectedPanelCount) {
            return fail("assembled fold and relief preview");
        }
        plateAssemblyGuidePreview_->setChecked(false);
        if (viewport_->PlateAssemblyFoldGuideCount() != 0
            || viewport_->PlateAssemblyReliefGuideCount() != 0) {
            return fail("hide assembled fold and relief preview");
        }
        plateAssemblyGuidePreview_->setChecked(true);
    } catch (const std::exception&) {
        return fail("build assembled fold and relief preview");
    }
    if (auto* outputScrollArea = qobject_cast<QScrollArea*>(toolsTabs_->widget(6))) {
        outputScrollArea->widget()->adjustSize();
        QApplication::processEvents();
        const auto buttons = outputScrollArea->findChildren<QPushButton*>();
        const auto pdfButton = std::find_if(buttons.begin(), buttons.end(), [](const QPushButton* button) {
            return button->property("platePdfAction").toBool();
        });
        const auto flatModelButton = std::find_if(buttons.begin(), buttons.end(), [](const QPushButton* button) {
            return button->property("plateFlatPatternModelAction").toBool();
        });
        if (pdfButton == buttons.end()) {
            return fail("plate PDF output button");
        }
        if (flatModelButton == buttons.end() || plateFlatPatternAutoRelief_ == nullptr
            || plateAssemblyGuidePreview_ == nullptr
            || plateAssemblyApproximationPreview_ == nullptr
            || plateFlatPatternPlane_ == nullptr || plateFlatPatternPlane_->count() == 0
            || plateFlatPatternCutWidth_ == nullptr) {
            return fail("flat-pattern wire and 3D plate controls");
        }
        QPushButton* pdfButtonPointer = *pdfButton;
        QTimer::singleShot(0, outputScrollArea, [outputScrollArea, pdfButtonPointer] {
            outputScrollArea->ensureWidgetVisible(pdfButtonPointer, 0, 12);
        });
    }
    toolsTabs_->setCurrentIndex(3);
    UpdateSelections({
        {CadSelectionKind::Wire, static_cast<int>(directStart)},
        {CadSelectionKind::Wire, static_cast<int>(directStart + 2)},
    }, true);
    wireOffsetDistance_->setValue(1.25);
    wireOffsetSide_->setCurrentIndex(0);
    UpdateWireOffsetPreview();
    if (viewport_->WireOffsetPreviewCount() != 2) {
        return fail("final offset preview state");
    }

    measurementMode_->setCurrentIndex(2);
    UpdateMeasurement({
        {MeasurementPickKind::Wire, static_cast<int>(directStart + 2), project_.Wires()[directStart + 2].wire.Start(), 0.0},
    });
    if (!measurementResultLabel_->text().contains(QStringLiteral("半径"))) {
        return fail("measure circle radius");
    }
    const int radiusMetric = measurementMetric_->findData(
        static_cast<int>(ReferenceDimensionKind::WireRadius));
    if (radiusMetric < 0 || measurementMetric_->findData(
            static_cast<int>(ReferenceDimensionKind::WireLength)) < 0) {
        return fail("offer persistent circle dimensions");
    }
    measurementMetric_->setCurrentIndex(radiusMetric);
    measurementName_->setText(QStringLiteral("__ui_radius_dimension"));
    SaveCurrentMeasurement();
    if (project_.ReferenceDimensions().size() != initialDimensionCount + 1
        || referenceDimensionList_->count() != static_cast<int>(initialDimensionCount + 1)
        || viewport_->ReferenceDimensionOverlayCount() != initialDimensionCount + 1
        || std::abs(project_.EvaluateReferenceDimension("__ui_radius_dimension").value
            - project_.Wires()[directStart + 2].wire.ArcData().radius) > 1.0e-9) {
        return fail("save persistent radius dimension");
    }
    Undo();
    if (project_.ReferenceDimensions().size() != initialDimensionCount) {
        return fail("undo persistent dimension");
    }
    Redo();
    if (project_.ReferenceDimensions().size() != initialDimensionCount + 1
        || viewport_->ReferenceDimensionOverlayCount() != initialDimensionCount + 1) {
        return fail("redo persistent dimension");
    }
    for (int row = 0; row < referenceDimensionList_->count(); ++row) {
        QListWidgetItem* item = referenceDimensionList_->item(row);
        if (item->data(kDimensionNameRole).toString() == QStringLiteral("__ui_radius_dimension")) {
            item->setCheckState(Qt::Unchecked);
            break;
        }
    }
    if (project_.ReferenceDimensions().back().visible
        || viewport_->ReferenceDimensionOverlayCount() != initialDimensionCount) {
        return fail("hide persistent dimension");
    }
    Undo();
    if (!project_.ReferenceDimensions().back().visible
        || viewport_->ReferenceDimensionOverlayCount() != initialDimensionCount + 1) {
        return fail("undo hidden persistent dimension");
    }
    UpdateMeasurement({
        {MeasurementPickKind::Wire, static_cast<int>(directStart), project_.Wires()[directStart].wire.Start(), 0.0},
        {MeasurementPickKind::Wire, static_cast<int>(meetStart), project_.Wires()[meetStart].wire.Start(), 0.0},
    });
    if (!measurementResultLabel_->text().contains(QStringLiteral("最短距離"))
        || !measurementResultLabel_->text().contains(QStringLiteral("最小交角"))) {
        return fail("measure wire relation");
    }
    measurementMode_->setCurrentIndex(0);
    UpdateMeasurement({
        {MeasurementPickKind::Point, -1, {0.0, 0.0, 0.0}, 0.0},
        {MeasurementPickKind::Point, -1, {3.0, 4.0, 12.0}, 0.0},
    });
    if (!measurementResultLabel_->text().contains(QStringLiteral("13.000 mm"))
        || !measurementResultLabel_->text().contains(QStringLiteral("XY投影"))
        || measurementMetric_->findData(static_cast<int>(ReferenceDimensionKind::PointDistance)) < 0) {
        return fail("measure two points");
    }
    measurementMode_->setCurrentIndex(1);
    UpdateMeasurement({
        {MeasurementPickKind::Point, -1, {0.0, 0.0, 0.0}, 0.0},
        {MeasurementPickKind::Point, -1, {3.0, 0.0, 0.0}, 0.0},
        {MeasurementPickKind::Point, -1, {0.0, 0.0, 4.0}, 0.0},
    });
    if (!measurementResultLabel_->text().contains(QStringLiteral("3D角度  90.000°"))) {
        return fail("measure three point 3D angle");
    }
    measurementMode_->setCurrentIndex(2);
    UpdateMeasurement({
        {MeasurementPickKind::Wire, static_cast<int>(directStart + 2),
            project_.Wires()[directStart + 2].wire.Start(), 0.0},
        {MeasurementPickKind::Wire, static_cast<int>(directStart),
            project_.Wires()[directStart].wire.Start(), 0.0},
    });
    if (!measurementResultLabel_->text().contains(QStringLiteral("曲率法線"))) {
        return fail("measure curve normal against line");
    }
    for (int row = 0; row < referenceDimensionList_->count(); ++row) {
        if (referenceDimensionList_->item(row)->data(kDimensionNameRole).toString()
            == QStringLiteral("__ui_radius_dimension")) {
            referenceDimensionList_->setCurrentRow(row);
            break;
        }
    }
    DeleteSelectedReferenceDimension();
    if (project_.ReferenceDimensions().size() != initialDimensionCount
        || viewport_->ReferenceDimensionOverlayCount() != initialDimensionCount) {
        return fail("delete persistent dimension");
    }

    const std::size_t coincidenceCount = project_.CoincidentConstraints().size();
    ApplyEndpointCoincidence(
        {static_cast<int>(directStart), kachakacha::model::WireEndpoint::End, project_.Wires()[directStart].wire.End()},
        {static_cast<int>(meetStart), kachakacha::model::WireEndpoint::Start, project_.Wires()[meetStart].wire.Start()});
    if (project_.CoincidentConstraints().size() != coincidenceCount + 1
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart].wire.End(), project_.Wires()[meetStart].wire.Start(), 1.0e-8)) {
        return fail("apply endpoint coincidence");
    }
    Undo();
    if (project_.CoincidentConstraints().size() != coincidenceCount) {
        return fail("undo endpoint coincidence");
    }
    Redo();
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(meetStart)}, true);
    RemoveSelectedCoincidences();
    if (project_.CoincidentConstraints().size() != coincidenceCount) {
        return fail("remove endpoint coincidence");
    }

    const std::size_t tangentCount = project_.TangentConstraints().size();
    ApplyEndpointTangency(
        {static_cast<int>(directStart), kachakacha::model::WireEndpoint::End, project_.Wires()[directStart].wire.End()},
        {static_cast<int>(directStart + 5), kachakacha::model::WireEndpoint::Start,
            project_.Wires()[directStart + 5].wire.Start()});
    if (project_.TangentConstraints().size() != tangentCount + 1
        || project_.CoincidentConstraints().size() != coincidenceCount + 1
        || !kachakacha::geometry::AlmostEqual(
            project_.Wires()[directStart].wire.End(), project_.Wires()[directStart + 5].wire.Start(), 1.0e-8)
        || kachakacha::geometry::Dot(
            MeasureWireTangent(project_.Wires()[directStart].wire, 1.0),
            MeasureWireTangent(project_.Wires()[directStart + 5].wire, 0.0)) < 0.999999) {
        return fail("apply endpoint tangency");
    }
    Undo();
    if (project_.TangentConstraints().size() != tangentCount
        || project_.CoincidentConstraints().size() != coincidenceCount) {
        return fail("undo endpoint tangency");
    }
    Redo();
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(directStart + 5)}, true);
    RemoveSelectedTangencies();
    if (project_.TangentConstraints().size() != tangentCount
        || project_.CoincidentConstraints().size() != coincidenceCount + 1) {
        return fail("remove endpoint tangency only");
    }
    ApplyEndpointCurvature(
        {static_cast<int>(directStart), kachakacha::model::WireEndpoint::End, project_.Wires()[directStart].wire.End()},
        {static_cast<int>(directStart + 5), kachakacha::model::WireEndpoint::Start,
            project_.Wires()[directStart + 5].wire.Start()});
    const auto& curvatureConstraint = project_.TangentConstraints().back();
    const auto& curvaturePoints = project_.Wires()[directStart + 5].wire.ControlPoints();
    const Vector3 curvatureTangent = (curvaturePoints[1] - curvaturePoints[0]).Normalized();
    const Vector3 curvatureSecondDerivative =
        (curvaturePoints[2] - curvaturePoints[1] * 2.0 + curvaturePoints[0]) * 6.0;
    const Vector3 normalSecondDerivative = curvatureSecondDerivative
        - curvatureTangent * kachakacha::geometry::Dot(curvatureSecondDerivative, curvatureTangent);
    if (project_.TangentConstraints().size() != tangentCount + 1
        || curvatureConstraint.continuity != WireContinuity::G2Curvature
        || normalSecondDerivative.Length() > 1.0e-8) {
        return fail("apply endpoint curvature");
    }
    Undo();
    if (project_.TangentConstraints().size() != tangentCount) {
        return fail("undo endpoint curvature");
    }
    Redo();
    UpdateSelection({CadSelectionKind::Wire, static_cast<int>(directStart + 5)}, true);
    RemoveSelectedTangencies();
    if (project_.TangentConstraints().size() != tangentCount) {
        return fail("remove endpoint curvature only");
    }
    RemoveSelectedCoincidences();
    if (project_.CoincidentConstraints().size() != coincidenceCount) {
        return fail("remove tangent endpoint coincidence");
    }

    modelFilter_->setText(QStringLiteral("__ui_nose_skin"));
    bool matchingTreeItemVisible = false;
    QTreeWidgetItemIterator filteredIterator(modelTree_);
    while (*filteredIterator) {
        QTreeWidgetItem* item = *filteredIterator;
        if (item->text(0) == QStringLiteral("__ui_nose_skin") && !item->isHidden()) {
            matchingTreeItemVisible = true;
            break;
        }
        ++filteredIterator;
    }
    if (!matchingTreeItemVisible) {
        return fail("filter model tree by object name");
    }
    modelFilter_->clear();

    const std::size_t historySizeBeforeDisplayMode = undoStack_.size();
    UpdateSelections({
        {CadSelectionKind::Surface, static_cast<int>(surfaceStart)},
        {CadSelectionKind::Plate, static_cast<int>(plateStart)},
    }, true);
    SetDisplayMode(ViewportDisplayMode::IsolatedSelection);
    if (viewport_->DisplayMode() != ViewportDisplayMode::IsolatedSelection) {
        return fail("isolate selected model objects");
    }
    SetDisplayMode(ViewportDisplayMode::FinishedModel);
    if (viewport_->DisplayMode() != ViewportDisplayMode::FinishedModel
        || viewport_->Selections().size() != 1
        || viewport_->Selections().front().kind != CadSelectionKind::Plate) {
        return fail("show completed plates and bodies only");
    }
    SetDisplayMode(ViewportDisplayMode::Design);
    if (viewport_->DisplayMode() != ViewportDisplayMode::Design
        || undoStack_.size() != historySizeBeforeDisplayMode) {
        return fail("restore design display without history change");
    }

    toolsTabs_->setCurrentIndex(8);
    QApplication::processEvents();
    return true;
}
