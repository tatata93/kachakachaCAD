// 計測・参照寸法・基準の操作ハンドラ。
// MainWindow.cpp から逐語移動(ADR 0018/0022)。

#include "MainWindow.h"
#include "CollapsibleSection.h"
#include "MainWindowUiHelpers.h"
#include "PartModelPanel.h"
#include "PlatePdfExport.h"

#include "kachakacha/io/PartPatterns.h"
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
#include <QMenu>
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
#include <QSpinBox>
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
#include <numeric>
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
using kachakacha::io::BuildAllPartPatterns;
using kachakacha::io::FabricationPanelDirection;
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
using kachakacha::model::CutPolylineCorner;
using kachakacha::model::FilletIntersectingLines;
using kachakacha::model::IntersectWires;
using kachakacha::model::RoundPolylineCorner;
using kachakacha::model::JoinLineChain;
using kachakacha::model::kWireChainConnectionTolerance;
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

using namespace mainwindow_helpers;

void MainWindow::SetReferenceFromSelection()
{
    const auto& selections = viewport_->Selections();
    if (selections.size() != 1 || selections.front().kind != CadSelectionKind::Wire
        || selections.front().index < 0
        || selections.front().index >= static_cast<int>(project_.Wires().size())
        || project_.Wires()[selections.front().index].wire.Kind() != WireKind::Line) {
        statusBar()->showMessage(QStringLiteral("基準にする直線を1本だけ選択してください"), 4000);
        return;
    }

    referenceWireName_ = project_.Wires()[selections.front().index].name;
    RefreshReference();
    UpdateSelection({}, true);
    statusBar()->showMessage(QStringLiteral("基準線を設定しました: %1").arg(ToQString(*referenceWireName_)), 3000);
}

void MainWindow::UpdateMeasurement(const std::vector<MeasurementPick>& picks)
{
    if (measurementStateLabel_ == nullptr || measurementResultLabel_ == nullptr
        || measurementMetric_ == nullptr || measurementSaveButton_ == nullptr) {
        return;
    }

    lastMeasurementPicks_ = picks;
    measurementMetric_->clear();
    measurementMetric_->setEnabled(false);
    measurementSaveButton_->setEnabled(false);
    const auto addMetric = [this](QString text, ReferenceDimensionKind kind) {
        measurementMetric_->addItem(std::move(text), static_cast<int>(kind));
        measurementMetric_->setEnabled(true);
        measurementSaveButton_->setEnabled(!measurementName_->text().trimmed().isEmpty());
    };

    const bool pointMode = viewport_->CurrentMeasurementMode() == MeasurementMode::TwoPoints;
    const bool angleMode = viewport_->CurrentMeasurementMode() == MeasurementMode::ThreePointsAngle;
    if (picks.empty()) {
        measurementStateLabel_->setText(pointMode ? QStringLiteral("1点目")
            : angleMode ? QStringLiteral("角度の頂点")
                        : QStringLiteral("1つ目の要素"));
        measurementResultLabel_->setText(QStringLiteral("未測定"));
        viewport_->SetMeasurementOverlay(std::nullopt, std::nullopt, {});
        return;
    }

    const auto pointLines = [](Vector3 point) {
        return QStringList{
            QStringLiteral("X  %1 mm").arg(Number(point.x)),
            QStringLiteral("Y  %1 mm").arg(Number(point.y)),
            QStringLiteral("Z  %1 mm").arg(Number(point.z)),
        };
    };
    const auto requireWire = [this](const MeasurementPick& pick) -> const Wire& {
        if (pick.kind != MeasurementPickKind::Wire || pick.index < 0
            || pick.index >= static_cast<int>(project_.Wires().size())) {
            throw std::invalid_argument("測定するワイヤーが見つかりません。");
        }
        return project_.Wires()[pick.index].wire;
    };
    const auto requirePlane = [this](const MeasurementPick& pick) -> const WorkPlane& {
        if (pick.kind != MeasurementPickKind::WorkPlane || pick.index < 0
            || pick.index >= static_cast<int>(project_.WorkPlanes().size())) {
            throw std::invalid_argument("測定する作業平面が見つかりません。");
        }
        return project_.WorkPlanes()[pick.index].plane;
    };

    try {
        if (angleMode) {
            if (picks.size() == 1) {
                measurementStateLabel_->setText(QStringLiteral("1方向目の点"));
                measurementResultLabel_->setText(
                    QStringLiteral("角度の頂点\n%1").arg(pointLines(picks[0].point).join('\n')));
                viewport_->SetMeasurementOverlay(
                    picks[0].point, std::nullopt, QStringLiteral("頂点"));
                return;
            }
            if (picks.size() == 2) {
                const double firstLength = (picks[1].point - picks[0].point).Length();
                measurementStateLabel_->setText(QStringLiteral("2方向目の点"));
                measurementResultLabel_->setText(QStringList{
                    QStringLiteral("1方向目  %1 mm").arg(Number(firstLength)),
                    QStringLiteral("次に2方向目の点を指定"),
                }.join('\n'));
                viewport_->SetMeasurementOverlay(
                    picks[0].point, picks[1].point,
                    QStringLiteral("%1 mm").arg(Number(firstLength)));
                return;
            }
            const auto angle = MeasureThreePointAngle(
                picks[0].point, picks[1].point, picks[2].point);
            const double firstLength = (picks[1].point - picks[0].point).Length();
            const double secondLength = (picks[2].point - picks[0].point).Length();
            measurementStateLabel_->setText(QStringLiteral("3D角度"));
            measurementResultLabel_->setText(QStringList{
                QStringLiteral("3D角度  %1°").arg(Number(angle.directedDegrees)),
                QStringLiteral("最小角  %1°").arg(Number(angle.acuteDegrees)),
                QStringLiteral("頂点→1方向目  %1 mm").arg(Number(firstLength)),
                QStringLiteral("頂点→2方向目  %1 mm").arg(Number(secondLength)),
            }.join('\n'));
            viewport_->SetMeasurementAngleOverlay(
                picks[0].point, picks[1].point, picks[2].point,
                QStringLiteral("%1°").arg(Number(angle.directedDegrees)));
            return;
        }

        if (pointMode) {
            if (picks.size() == 1) {
                measurementStateLabel_->setText(QStringLiteral("2点目"));
                measurementResultLabel_->setText(pointLines(picks[0].point).join('\n'));
                viewport_->SetMeasurementOverlay(picks[0].point, std::nullopt, QStringLiteral("P1"));
                return;
            }

            const Vector3 delta = picks[1].point - picks[0].point;
            const double distance = delta.Length();
            QStringList result{
                QStringLiteral("3D距離  %1 mm").arg(Number(distance)),
                QStringLiteral("ΔX  %1 mm").arg(Number(delta.x)),
                QStringLiteral("ΔY  %1 mm").arg(Number(delta.y)),
                QStringLiteral("ΔZ  %1 mm").arg(Number(delta.z)),
                QStringLiteral("XY投影  %1 mm").arg(Number(std::hypot(delta.x, delta.y))),
                QStringLiteral("XZ投影  %1 mm").arg(Number(std::hypot(delta.x, delta.z))),
                QStringLiteral("YZ投影  %1 mm").arg(Number(std::hypot(delta.y, delta.z))),
            };
            if (distance > 1.0e-12) {
                result << QStringLiteral("X/Y/Z軸との角度  %1° / %2° / %3°")
                    .arg(Number(MeasureDirectionsAngle(delta, {1.0, 0.0, 0.0}).directedDegrees))
                    .arg(Number(MeasureDirectionsAngle(delta, {0.0, 1.0, 0.0}).directedDegrees))
                    .arg(Number(MeasureDirectionsAngle(delta, {0.0, 0.0, 1.0}).directedDegrees));
                result << QStringLiteral("XY / XZ / YZ方向角  %1° / %2° / %3°")
                    .arg(Number(std::atan2(delta.y, delta.x) * 180.0 / kPi))
                    .arg(Number(std::atan2(delta.z, delta.x) * 180.0 / kPi))
                    .arg(Number(std::atan2(delta.z, delta.y) * 180.0 / kPi));
            }
            measurementStateLabel_->setText(QStringLiteral("2点間"));
            measurementResultLabel_->setText(result.join('\n'));
            addMetric(QStringLiteral("残す値: 3D距離"), ReferenceDimensionKind::PointDistance);
            viewport_->SetMeasurementOverlay(
                picks[0].point,
                picks[1].point,
                QStringLiteral("%1 mm").arg(Number(distance)),
                {
                    QStringLiteral("ΔX  %1 mm").arg(Number(delta.x)),
                    QStringLiteral("ΔY  %1 mm").arg(Number(delta.y)),
                    QStringLiteral("ΔZ  %1 mm").arg(Number(delta.z)),
                });
            return;
        }

        if (picks.size() == 1) {
            const MeasurementPick& pick = picks[0];
            measurementStateLabel_->setText(QStringLiteral("1要素"));
            if (pick.kind == MeasurementPickKind::Point) {
                measurementResultLabel_->setText(pointLines(pick.point).join('\n'));
                viewport_->SetMeasurementOverlay(pick.point, std::nullopt, QStringLiteral("点"));
                return;
            }
            if (pick.kind == MeasurementPickKind::WorkPlane) {
                const WorkPlane& plane = requirePlane(pick);
                measurementResultLabel_->setText(QStringList{
                    ToQString(project_.WorkPlanes()[pick.index].name),
                    QStringLiteral("原点  %1").arg(VectorText(plane.Origin())),
                    QStringLiteral("法線  %1").arg(VectorText(plane.Normal())),
                }.join('\n'));
                viewport_->SetMeasurementOverlay(plane.Origin(), std::nullopt, QStringLiteral("作業平面"));
                return;
            }

            const Wire& wire = requireWire(pick);
            const double length = MeasureWireLength(wire);
            const Vector3 tangent = MeasureWireTangent(wire, pick.wireParameter);
            const std::optional<Vector3> normal = MeasureWireCurvatureNormal(
                wire, pick.wireParameter);
            QStringList result{
                ToQString(project_.Wires()[pick.index].name),
                QStringLiteral("全長  %1 mm").arg(Number(length)),
            };
            const std::optional<double> radius = MeasureWireRadius(wire);
            if (radius.has_value()) {
                addMetric(QStringLiteral("残す値: 半径"), ReferenceDimensionKind::WireRadius);
                result << QStringLiteral("半径  %1 mm").arg(Number(*radius));
                result << QStringLiteral("直径  %1 mm").arg(Number(*radius * 2.0));
                result << QStringLiteral("中心角  %1°")
                    .arg(Number(std::abs(wire.ArcData().sweepAngleRadians) * 180.0 / kPi));
            }
            result << QStringLiteral("クリック位置の接線とX/Y/Z軸  %1° / %2° / %3°")
                .arg(Number(MeasureDirectionsAngle(tangent, {1.0, 0.0, 0.0}).directedDegrees))
                .arg(Number(MeasureDirectionsAngle(tangent, {0.0, 1.0, 0.0}).directedDegrees))
                .arg(Number(MeasureDirectionsAngle(tangent, {0.0, 0.0, 1.0}).directedDegrees));
            if (normal.has_value()) {
                result << QStringLiteral("曲率法線とX/Y/Z軸  %1° / %2° / %3°")
                    .arg(Number(MeasureDirectionsAngle(*normal, {1.0, 0.0, 0.0}).directedDegrees))
                    .arg(Number(MeasureDirectionsAngle(*normal, {0.0, 1.0, 0.0}).directedDegrees))
                    .arg(Number(MeasureDirectionsAngle(*normal, {0.0, 0.0, 1.0}).directedDegrees));
            } else {
                result << QStringLiteral("曲率法線  なし（直線または曲率0）");
            }
            addMetric(QStringLiteral("残す値: ワイヤー全長"), ReferenceDimensionKind::WireLength);
            measurementResultLabel_->setText(result.join('\n'));
            if (radius.has_value()) {
                viewport_->SetMeasurementOverlay(
                    wire.ArcData().center,
                    pick.point,
                    QStringLiteral("R %1 mm").arg(Number(*radius)));
            } else {
                viewport_->SetMeasurementOverlay(
                    wire.Start(),
                    wire.End(),
                    QStringLiteral("L %1 mm").arg(Number(length)));
            }
            return;
        }

        const MeasurementPick& first = picks[0];
        const MeasurementPick& second = picks[1];
        measurementStateLabel_->setText(QStringLiteral("2要素"));
        if (first.kind == MeasurementPickKind::Wire && second.kind == MeasurementPickKind::Wire) {
            const Wire& firstWire = requireWire(first);
            const Wire& secondWire = requireWire(second);
            const auto distance = MeasureWireToWireDistance(firstWire, secondWire);
            const Vector3 firstTangent = MeasureWireTangent(firstWire, first.wireParameter);
            const Vector3 secondTangent = MeasureWireTangent(secondWire, second.wireParameter);
            const auto angle = MeasureDirectionsAngle(firstTangent, secondTangent);
            const auto firstNormal = MeasureWireCurvatureNormal(firstWire, first.wireParameter);
            const auto secondNormal = MeasureWireCurvatureNormal(secondWire, second.wireParameter);
            QStringList result{
                QStringLiteral("最短距離  %1 mm").arg(Number(distance.distanceMillimeters)),
                QStringLiteral("クリック点間  %1 mm").arg(Number((second.point - first.point).Length())),
                QStringLiteral("クリック位置の接線角  %1°").arg(Number(angle.directedDegrees)),
                QStringLiteral("最小交角  %1°").arg(Number(angle.acuteDegrees)),
            };
            if (firstNormal.has_value() && secondNormal.has_value()) {
                const auto normalAngle = MeasureDirectionsAngle(*firstNormal, *secondNormal);
                result << QStringLiteral("曲率法線同士の角度  %1°").arg(Number(normalAngle.directedDegrees));
            } else if (firstNormal.has_value()) {
                const auto normalAngle = MeasureDirectionsAngle(*firstNormal, secondTangent);
                result << QStringLiteral("1本目の曲率法線と2本目の接線  %1°")
                    .arg(Number(normalAngle.directedDegrees));
            } else if (secondNormal.has_value()) {
                const auto normalAngle = MeasureDirectionsAngle(firstTangent, *secondNormal);
                result << QStringLiteral("1本目の接線と2本目の曲率法線  %1°")
                    .arg(Number(normalAngle.directedDegrees));
            }
            measurementResultLabel_->setText(result.join('\n'));
            addMetric(QStringLiteral("残す値: 最短距離"), ReferenceDimensionKind::WireDistance);
            addMetric(QStringLiteral("残す値: クリック位置の接線角"), ReferenceDimensionKind::WireAngle);
            viewport_->SetMeasurementOverlay(
                distance.firstPoint,
                distance.secondPoint,
                QStringLiteral("最短 %1 mm").arg(Number(distance.distanceMillimeters)));
            return;
        }

        const auto pointAndWire = [&](const MeasurementPick& pointPick, const MeasurementPick& wirePick) {
            const auto distance = MeasurePointToWireDistance(pointPick.point, requireWire(wirePick));
            measurementResultLabel_->setText(QStringList{
                QStringLiteral("点からワイヤーの最短距離  %1 mm").arg(Number(distance.distanceMillimeters)),
                QStringLiteral("クリック点間  %1 mm").arg(Number((wirePick.point - pointPick.point).Length())),
            }.join('\n'));
            addMetric(QStringLiteral("残す値: 点からワイヤーの最短距離"),
                ReferenceDimensionKind::PointWireDistance);
            viewport_->SetMeasurementOverlay(
                distance.firstPoint,
                distance.secondPoint,
                QStringLiteral("%1 mm").arg(Number(distance.distanceMillimeters)));
        };
        if (first.kind == MeasurementPickKind::Point && second.kind == MeasurementPickKind::Wire) {
            pointAndWire(first, second);
            return;
        }
        if (first.kind == MeasurementPickKind::Wire && second.kind == MeasurementPickKind::Point) {
            pointAndWire(second, first);
            return;
        }

        const auto wireAndPlane = [&](const MeasurementPick& wirePick, const MeasurementPick& planePick) {
            const Wire& wire = requireWire(wirePick);
            const WorkPlane& plane = requirePlane(planePick);
            const double signedDistance = MeasureSignedPointToPlaneDistance(wirePick.point, plane);
            const double angle = MeasureDirectionToPlaneAngleDegrees(
                MeasureWireTangent(wire, wirePick.wireParameter), plane);
            measurementResultLabel_->setText(QStringList{
                QStringLiteral("線・接線と平面の角度  %1°").arg(Number(angle)),
                QStringLiteral("クリック位置から平面  %1 mm").arg(Number(std::abs(signedDistance))),
            }.join('\n'));
            addMetric(QStringLiteral("残す値: 線・接線と平面の角度"),
                ReferenceDimensionKind::WirePlaneAngle);
            addMetric(QStringLiteral("残す値: クリック位置から平面"),
                ReferenceDimensionKind::PointPlaneDistance);
            viewport_->SetMeasurementOverlay(
                wirePick.point,
                wirePick.point - plane.Normal() * signedDistance,
                QStringLiteral("%1°").arg(Number(angle)));
        };
        if (first.kind == MeasurementPickKind::Wire && second.kind == MeasurementPickKind::WorkPlane) {
            wireAndPlane(first, second);
            return;
        }
        if (first.kind == MeasurementPickKind::WorkPlane && second.kind == MeasurementPickKind::Wire) {
            wireAndPlane(second, first);
            return;
        }

        const auto pointAndPlane = [&](const MeasurementPick& pointPick, const MeasurementPick& planePick) {
            const WorkPlane& plane = requirePlane(planePick);
            const double signedDistance = MeasureSignedPointToPlaneDistance(pointPick.point, plane);
            measurementResultLabel_->setText(QStringLiteral("点から平面  %1 mm")
                .arg(Number(std::abs(signedDistance))));
            addMetric(QStringLiteral("残す値: 点から平面の距離"),
                ReferenceDimensionKind::PointPlaneDistance);
            viewport_->SetMeasurementOverlay(
                pointPick.point,
                pointPick.point - plane.Normal() * signedDistance,
                QStringLiteral("%1 mm").arg(Number(std::abs(signedDistance))));
        };
        if (first.kind == MeasurementPickKind::Point && second.kind == MeasurementPickKind::WorkPlane) {
            pointAndPlane(first, second);
            return;
        }
        if (first.kind == MeasurementPickKind::WorkPlane && second.kind == MeasurementPickKind::Point) {
            pointAndPlane(second, first);
            return;
        }

        if (first.kind == MeasurementPickKind::WorkPlane
            && second.kind == MeasurementPickKind::WorkPlane) {
            const WorkPlane& firstPlane = requirePlane(first);
            const WorkPlane& secondPlane = requirePlane(second);
            const double angle = MeasurePlaneToPlaneAngleDegrees(firstPlane, secondPlane);
            QStringList result{QStringLiteral("平面同士の角度  %1°").arg(Number(angle))};
            if (angle <= 1.0e-7) {
                result << QStringLiteral("平行間隔  %1 mm")
                    .arg(Number(std::abs(MeasureSignedPointToPlaneDistance(firstPlane.Origin(), secondPlane))));
            }
            measurementResultLabel_->setText(result.join('\n'));
            addMetric(QStringLiteral("残す値: 平面同士の角度"), ReferenceDimensionKind::PlaneAngle);
            if (angle <= 1.0e-7) {
                addMetric(QStringLiteral("残す値: 平行間隔"), ReferenceDimensionKind::PlaneDistance);
            }
            viewport_->SetMeasurementOverlay(
                firstPlane.Origin(),
                secondPlane.Origin(),
                QStringLiteral("%1°").arg(Number(angle)));
            return;
        }

        measurementResultLabel_->setText(QStringLiteral("この組合せは測定できません"));
    } catch (const std::exception& error) {
        measurementResultLabel_->setText(QString::fromUtf8(error.what()));
        viewport_->SetMeasurementOverlay(std::nullopt, std::nullopt, {});
    }
}

void MainWindow::SaveCurrentMeasurement()
{
    try {
        ValidateObjectName(measurementName_->text());
        if (measurementMetric_->currentIndex() < 0) {
            throw std::invalid_argument("残す測定値を選択してください。");
        }

        ReferenceDimension dimension;
        dimension.name = ToName(measurementName_->text());
        dimension.kind = static_cast<ReferenceDimensionKind>(measurementMetric_->currentData().toInt());

        const auto makeReference = [this](const MeasurementPick& pick) {
            DimensionReference reference;
            reference.wireParameter = pick.wireParameter;
            if (pick.kind == MeasurementPickKind::Wire
                || (pick.kind == MeasurementPickKind::Point && pick.index >= 0)) {
                if (pick.index < 0 || pick.index >= static_cast<int>(project_.Wires().size())) {
                    throw std::invalid_argument("寸法が参照するワイヤーが見つかりません。");
                }
                reference.kind = DimensionReferenceKind::Wire;
                reference.objectName = project_.Wires()[pick.index].name;
                return reference;
            }
            if (pick.kind == MeasurementPickKind::WorkPlane) {
                if (pick.index < 0 || pick.index >= static_cast<int>(project_.WorkPlanes().size())) {
                    throw std::invalid_argument("寸法が参照する作業平面が見つかりません。");
                }
                reference.kind = DimensionReferenceKind::WorkPlane;
                reference.objectName = project_.WorkPlanes()[pick.index].name;
                return reference;
            }
            reference.kind = DimensionReferenceKind::FixedPoint;
            reference.point = pick.point;
            return reference;
        };
        const auto findPick = [&](MeasurementPickKind kind) -> const MeasurementPick& {
            const auto position = std::find_if(
                lastMeasurementPicks_.begin(), lastMeasurementPicks_.end(),
                [&](const MeasurementPick& pick) { return pick.kind == kind; });
            if (position == lastMeasurementPicks_.end()) {
                throw std::invalid_argument("測定対象が不足しています。もう一度測定してください。");
            }
            return *position;
        };
        const auto findOtherThan = [&](MeasurementPickKind kind) -> const MeasurementPick& {
            const auto position = std::find_if(
                lastMeasurementPicks_.begin(), lastMeasurementPicks_.end(),
                [&](const MeasurementPick& pick) { return pick.kind != kind; });
            if (position == lastMeasurementPicks_.end()) {
                throw std::invalid_argument("測定対象が不足しています。もう一度測定してください。");
            }
            return *position;
        };
        const auto requireTwoPicks = [&] {
            if (lastMeasurementPicks_.size() < 2) {
                throw std::invalid_argument("測定対象が不足しています。もう一度測定してください。");
            }
        };

        switch (dimension.kind) {
        case ReferenceDimensionKind::PointDistance:
            requireTwoPicks();
            dimension.first = makeReference(lastMeasurementPicks_[0]);
            dimension.second = makeReference(lastMeasurementPicks_[1]);
            break;
        case ReferenceDimensionKind::WireLength:
        case ReferenceDimensionKind::WireRadius:
            dimension.first = makeReference(findPick(MeasurementPickKind::Wire));
            break;
        case ReferenceDimensionKind::WireDistance:
        case ReferenceDimensionKind::WireAngle:
            requireTwoPicks();
            dimension.first = makeReference(lastMeasurementPicks_[0]);
            dimension.second = makeReference(lastMeasurementPicks_[1]);
            break;
        case ReferenceDimensionKind::PointWireDistance:
            dimension.first = makeReference(findPick(MeasurementPickKind::Point));
            dimension.second = makeReference(findPick(MeasurementPickKind::Wire));
            break;
        case ReferenceDimensionKind::PointPlaneDistance:
            dimension.first = makeReference(findOtherThan(MeasurementPickKind::WorkPlane));
            dimension.second = makeReference(findPick(MeasurementPickKind::WorkPlane));
            break;
        case ReferenceDimensionKind::WirePlaneAngle:
            dimension.first = makeReference(findPick(MeasurementPickKind::Wire));
            dimension.second = makeReference(findPick(MeasurementPickKind::WorkPlane));
            break;
        case ReferenceDimensionKind::PlaneAngle:
        case ReferenceDimensionKind::PlaneDistance:
            requireTwoPicks();
            dimension.first = makeReference(lastMeasurementPicks_[0]);
            dimension.second = makeReference(lastMeasurementPicks_[1]);
            break;
        }

        Project candidate = project_;
        candidate.AddReferenceDimension(dimension);
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        measurementName_->setText(SuggestedDimensionName());
        statusBar()->showMessage(
            QStringLiteral("参照寸法を残しました: %1").arg(ToQString(dimension.name)), 3000);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("参照寸法"), QString::fromUtf8(error.what()));
    }
}

void MainWindow::DeleteSelectedReferenceDimension()
{
    QListWidgetItem* item = referenceDimensionList_->currentItem();
    if (item == nullptr) {
        statusBar()->showMessage(QStringLiteral("削除する参照寸法を選択してください"), 2500);
        return;
    }
    const std::string name = ToName(item->data(kDimensionNameRole).toString());
    const auto position = std::find_if(
        project_.ReferenceDimensions().begin(), project_.ReferenceDimensions().end(),
        [&](const ReferenceDimension& dimension) { return dimension.name == name; });
    if (position == project_.ReferenceDimensions().end()) {
        return;
    }
    RecordUndo();
    (void)project_.RemoveReferenceDimension(name);
    MarkModified();
    RefreshModelViews(false);
    statusBar()->showMessage(QStringLiteral("参照寸法を削除しました: %1").arg(ToQString(name)), 2500);
}

void MainWindow::RefreshReferenceDimensions()
{
    if (referenceDimensionList_ == nullptr || viewport_ == nullptr) {
        return;
    }

    const QString selectedName = referenceDimensionList_->currentItem() != nullptr
        ? referenceDimensionList_->currentItem()->data(kDimensionNameRole).toString()
        : QString();
    const QSignalBlocker blocker(referenceDimensionList_);
    referenceDimensionList_->clear();
    std::vector<ReferenceDimensionOverlay> overlays;
    for (const ReferenceDimension& dimension : project_.ReferenceDimensions()) {
        try {
            const auto result = project_.EvaluateReferenceDimension(dimension.name);
            const QString value = ReferenceDimensionValueText(dimension.kind, result.value);
            auto* item = new QListWidgetItem(
                QStringLiteral("%1   %2   %3")
                    .arg(ToQString(dimension.name), ReferenceDimensionKindText(dimension.kind), value),
                referenceDimensionList_);
            item->setData(kDimensionNameRole, ToQString(dimension.name));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(dimension.visible ? Qt::Checked : Qt::Unchecked);
            if (ToQString(dimension.name) == selectedName) {
                referenceDimensionList_->setCurrentItem(item);
            }
            if (dimension.visible) {
                overlays.push_back({
                    result.firstPoint,
                    result.secondPoint,
                    QStringLiteral("%1  %2").arg(ToQString(dimension.name), value),
                });
            }
        } catch (const std::exception& error) {
            auto* item = new QListWidgetItem(
                QStringLiteral("%1   [参照切れ]").arg(ToQString(dimension.name)),
                referenceDimensionList_);
            item->setData(kDimensionNameRole, ToQString(dimension.name));
            item->setToolTip(QString::fromUtf8(error.what()));
        }
    }
    referenceDimensionDeleteButton_->setEnabled(referenceDimensionList_->currentRow() >= 0);
    viewport_->SetReferenceDimensionOverlays(std::move(overlays));
}

void MainWindow::ClearReference()
{
    referenceWireName_.reset();
    RefreshReference();
    statusBar()->showMessage(QStringLiteral("基準線を解除しました"), 2500);
}

void MainWindow::RefreshReference()
{
    int referenceIndex = -1;
    if (referenceWireName_.has_value()) {
        for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
            if (project_.Wires()[index].name == *referenceWireName_
                && project_.Wires()[index].wire.Kind() == WireKind::Line) {
                referenceIndex = index;
                break;
            }
        }
    }
    if (referenceIndex < 0) {
        referenceWireName_.reset();
    }

    viewport_->SetReference(referenceIndex >= 0
            ? CadSelection{CadSelectionKind::Wire, referenceIndex}
            : CadSelection{});
    const QString text = referenceWireName_.has_value()
        ? ToQString(*referenceWireName_)
        : QStringLiteral("未設定");
    if (planeReferenceLabel_ != nullptr) {
        planeReferenceLabel_->setText(text);
    }
    if (transformReferenceLabel_ != nullptr) {
        const QString planeName = activePlaneCombo_ != nullptr && !activePlaneCombo_->currentText().isEmpty()
            ? activePlaneCombo_->currentText()
            : QStringLiteral("未設定");
        transformReferenceLabel_->setText(referenceWireName_.has_value()
                ? QStringLiteral("基準線: %1  /  面: %2").arg(text, planeName)
                : QStringLiteral("基準線: 2点指定  /  面: %1").arg(planeName));
    }
    if (lightCaseReferenceLabel_ != nullptr) {
        lightCaseReferenceLabel_->setText(QStringLiteral("基準線: %1").arg(text));
    }
    clearReferenceAction_->setEnabled(referenceWireName_.has_value());
}

void MainWindow::UseReferenceForPlaneRotation()
{
    if (!referenceWireName_.has_value()) {
        statusBar()->showMessage(QStringLiteral("先に直線を選択して基準線に設定してください"), 3500);
        return;
    }
    const auto found = std::find_if(project_.Wires().begin(), project_.Wires().end(), [this](const auto& wire) {
        return wire.name == *referenceWireName_ && wire.wire.Kind() == WireKind::Line;
    });
    if (found == project_.Wires().end()) {
        ClearReference();
        return;
    }

    const Vector3 point = found->wire.Start();
    const Vector3 direction = found->wire.End() - found->wire.Start();
    const std::array<double, 3> pointValues = {point.x, point.y, point.z};
    const std::array<double, 3> directionValues = {direction.x, direction.y, direction.z};
    for (int axis = 0; axis < 3; ++axis) {
        rotateAxisPoint_[axis]->setValue(pointValues[axis]);
        rotateAxisDirection_[axis]->setValue(directionValues[axis]);
    }
    planeMethod_->setCurrentIndex(4);
    const int activeIndex = rotateSourcePlane_->findText(activePlaneCombo_->currentText());
    if (activeIndex >= 0) {
        rotateSourcePlane_->setCurrentIndex(activeIndex);
    }
    statusBar()->showMessage(QStringLiteral("基準線を作業平面の回転軸に設定しました"), 3000);
}
