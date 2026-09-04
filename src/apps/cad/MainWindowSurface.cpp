// 面・板の作成/更新/分割/開口などの操作ハンドラ。
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
#include <limits>
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

std::vector<std::string> MainWindow::SelectedSurfaceWireNames() const
{
    std::vector<std::string> names;
    for (const CadSelection& selection : viewport_->Selections()) {
        if (selection.kind != CadSelectionKind::Wire || selection.index < 0
            || selection.index >= static_cast<int>(project_.Wires().size())) {
            continue;
        }
        const std::string& name = project_.Wires()[selection.index].name;
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    return names;
}

void MainWindow::ValidateSurfaceInputGroup(
    const std::vector<std::string>& wireNames,
    SurfaceInputRole role,
    bool requireClosedBoundary) const
{
    if (wireNames.empty()) {
        throw std::invalid_argument("3D画面で線を1本以上選択してください。");
    }
    std::vector<Wire> wires;
    wires.reserve(wireNames.size());
    for (std::size_t index = 0; index < wireNames.size(); ++index) {
        if (std::find(wireNames.begin(), wireNames.begin() + index,
                wireNames[index]) != wireNames.begin() + index) {
            throw std::invalid_argument("同じ線を同じグループへ重複して追加できません。");
        }
        const auto found = std::find_if(
            project_.Wires().begin(), project_.Wires().end(),
            [&](const auto& wire) { return wire.name == wireNames[index]; });
        if (found == project_.Wires().end()) {
            throw std::invalid_argument("割り当てた元線が見つかりません。");
        }
        if (found->metadata.construction) {
            throw std::invalid_argument(
                "補助線は輪郭や断面に使えません。通常線へ戻してください。");
        }
        wires.push_back(found->wire);
    }
    Wire composite = JoinWireChain(wires);
    const int mode = surfaceType_ != nullptr ? surfaceType_->currentIndex() : 0;
    if (role == SurfaceInputRole::Boundary
        && requireClosedBoundary && !composite.IsClosed()) {
        throw std::invalid_argument(
            "輪郭グループは、選んだ全線をつないで閉じる必要があります。");
    }
    if (mode == 3 && composite.IsClosed()) {
        throw std::invalid_argument(
            "1行の外形・断面は開いた連続線にしてください。2つの外形行が両端で互いに接続する形は使用できます。");
    }
}

void MainWindow::RefreshSurfaceInputTable()
{
    if (surfaceInputTable_ == nullptr || surfaceType_ == nullptr) {
        return;
    }
    surfaceInputTable_->setRowCount(static_cast<int>(surfaceInputGroups_.size()));
    int boundaryNumber = 0;
    int guideNumber = 0;
    int sectionNumber = 0;
    for (int row = 0; row < static_cast<int>(surfaceInputGroups_.size()); ++row) {
        const SurfaceInputGroup& group = surfaceInputGroups_[row];
        QString role;
        if (group.role == SurfaceInputRole::Boundary) {
            role = QStringLiteral("輪郭%1").arg(++boundaryNumber);
        } else if (group.role == SurfaceInputRole::Guide) {
            role = QStringLiteral("外形%1").arg(++guideNumber);
        } else {
            role = QStringLiteral("断面%1").arg(++sectionNumber);
        }
        QStringList names;
        std::vector<Wire> wires;
        for (const std::string& name : group.wireNames) {
            names.push_back(ToQString(name));
            const auto found = std::find_if(
                project_.Wires().begin(), project_.Wires().end(),
                [&](const auto& wire) { return wire.name == name; });
            if (found != project_.Wires().end()) {
                wires.push_back(found->wire);
            }
        }
        surfaceInputTable_->setItem(row, 0, new QTableWidgetItem(role));
        QString state = QStringLiteral("接続エラー");
        if (wires.size() == group.wireNames.size() && !wires.empty()) {
            try {
                const Wire composite = JoinWireChain(wires);
                state = composite.IsClosed(kWireChainConnectionTolerance)
                    ? QStringLiteral("閉ループ") : QStringLiteral("開経路");
            } catch (const std::exception&) {
            }
        }
        auto* stateItem = new QTableWidgetItem(state);
        if (state == QStringLiteral("接続エラー")) {
            stateItem->setForeground(QColor("#b23a48"));
        } else if (state == QStringLiteral("閉ループ")) {
            stateItem->setForeground(QColor("#19734a"));
        }
        surfaceInputTable_->setItem(row, 1, stateItem);
        surfaceInputTable_->setItem(row, 2,
            new QTableWidgetItem(QString::number(group.wireNames.size())));
        auto* sourcesItem = new QTableWidgetItem(
            names.join(QStringLiteral(" + ")));
        sourcesItem->setToolTip(names.join(QStringLiteral("\n")));
        surfaceInputTable_->setItem(row, 3, sourcesItem);
    }

    const int mode = surfaceType_->currentIndex();
    surfaceAddBoundaryOrGuideButton_->setVisible(mode == 0 || mode == 3);
    surfaceAddBoundaryOrGuideButton_->setText(mode == 0
            ? QStringLiteral("輪郭を追加")
            : QStringLiteral("外形を追加"));
    surfaceAddSectionButton_->setVisible(mode != 0);
    surfaceAppendGroupButton_->setEnabled(!surfaceInputGroups_.empty());
    if (surfaceCreateButton_ != nullptr) {
        surfaceCreateButton_->setText(surfaceInputGroups_.empty()
                ? QStringLiteral("選択ワイヤーから面を作成")
                : QStringLiteral("表の輪郭・断面から面を作成"));
    }
}

void MainWindow::SelectConnectedSurfaceWireChain()
{
    try {
        constexpr double connectionTolerance = kWireChainConnectionTolerance;
        std::vector<int> selectedIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())
                && std::find(selectedIndices.begin(), selectedIndices.end(), selection.index)
                    == selectedIndices.end()) {
                selectedIndices.push_back(selection.index);
            }
        }
        if (selectedIndices.empty()) {
            throw std::invalid_argument(
                "起点にする線を3D画面で1本選択してください。");
        }

        const auto canUse = [&](int index) {
            const auto& wire = project_.Wires()[index];
            return wire.visible && !wire.metadata.construction
                && !wire.projection.has_value() && !wire.plateOffset.has_value();
        };
        const auto endpointCandidates = [&](Vector3 point) {
            std::vector<int> candidates;
            for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
                if (!canUse(index)) {
                    continue;
                }
                const Wire& wire = project_.Wires()[index].wire;
                if ((wire.Start() - point).Length() <= connectionTolerance
                    || (wire.End() - point).Length() <= connectionTolerance) {
                    candidates.push_back(index);
                }
            }
            return candidates;
        };

        bool changed = true;
        while (changed) {
            changed = false;
            const std::vector<int> current = selectedIndices;
            for (const int index : current) {
                if (!canUse(index)) {
                    continue;
                }
                const Wire& wire = project_.Wires()[index].wire;
                for (const Vector3 endpoint : {wire.Start(), wire.End()}) {
                    const std::vector<int> candidates = endpointCandidates(endpoint);
                    if (candidates.size() != 2) {
                        continue;
                    }
                    for (const int candidate : candidates) {
                        if (std::find(selectedIndices.begin(), selectedIndices.end(), candidate)
                            == selectedIndices.end()) {
                            selectedIndices.push_back(candidate);
                            changed = true;
                        }
                    }
                }
            }
        }

        std::vector<CadSelection> selections;
        selections.reserve(selectedIndices.size());
        for (const int index : selectedIndices) {
            selections.push_back({CadSelectionKind::Wire, index});
        }
        UpdateSelections(std::move(selections), true);
        statusBar()->showMessage(
            QStringLiteral("分岐点まで続く%1本を選択しました。続けて外形・断面へ追加できます")
                .arg(selectedIndices.size()),
            4500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::AddSelectedSurfaceInputGroup(SurfaceInputRole role)
{
    try {
        SplitSelectedWiresAtBranchPoints();
        const int mode = surfaceType_->currentIndex();
        if ((role == SurfaceInputRole::Boundary && mode != 0)
            || (role == SurfaceInputRole::Guide && mode != 3)
            || (role == SurfaceInputRole::Section && mode == 0)) {
            throw std::invalid_argument("現在の面の作り方では、その役割を追加できません。");
        }
        const int sameRoleCount = static_cast<int>(std::count_if(
            surfaceInputGroups_.begin(), surfaceInputGroups_.end(),
            [&](const auto& group) { return group.role == role; }));
        if ((role == SurfaceInputRole::Boundary && sameRoleCount >= 1)
            || (role == SurfaceInputRole::Guide && sameRoleCount >= 2)
            || (role == SurfaceInputRole::Section && mode == 1 && sameRoleCount >= 2)) {
            throw std::invalid_argument(
                "必要数はすでに表へ追加されています。線分を足す場合は行を選んで「選択行へ線を追加」を使ってください。");
        }

        const std::vector<std::string> selectedNames = SelectedSurfaceWireNames();
        std::vector<std::string> names;
        names.reserve(selectedNames.size());
        for (const std::string& name : selectedNames) {
            const bool alreadyAssigned = std::any_of(
                surfaceInputGroups_.begin(), surfaceInputGroups_.end(),
                [&](const auto& group) {
                    return std::find(group.wireNames.begin(), group.wireNames.end(), name)
                        != group.wireNames.end();
                });
            if (!alreadyAssigned) {
                names.push_back(name);
            }
        }
        if (names.empty() && !selectedNames.empty()) {
            throw std::invalid_argument(
                "選択した線はすべて表へ登録済みです。新しい外形・断面の線を追加選択してください。");
        }
        ValidateSurfaceInputGroup(names, role);

        SurfaceInputGroup group{role, names};
        auto position = surfaceInputGroups_.end();
        if (role == SurfaceInputRole::Guide) {
            position = std::find_if(
                surfaceInputGroups_.begin(), surfaceInputGroups_.end(),
                [](const auto& candidate) {
                    return candidate.role == SurfaceInputRole::Section;
                });
        }
        const int row = static_cast<int>(std::distance(
            surfaceInputGroups_.begin(),
            surfaceInputGroups_.insert(position, std::move(group))));
        RefreshSurfaceInputTable();
        surfaceInputTable_->selectRow(row);
        UpdateSelections({}, true);
        statusBar()->showMessage(
            QStringLiteral("未登録の%1本を1つの%2として追加しました")
                .arg(names.size())
                .arg(role == SurfaceInputRole::Boundary
                        ? QStringLiteral("輪郭")
                        : role == SurfaceInputRole::Guide
                        ? QStringLiteral("外形")
                        : QStringLiteral("断面")),
            3500);
    } catch (const std::exception& error) {
        const QString message = FriendlySurfaceChainError(error);
        statusBar()->showMessage(message.section('\n', 0, 0), 7000);
        QMessageBox::warning(this, QStringLiteral("経路を登録できません"), message);
    }
}

void MainWindow::AppendSelectedWiresToSurfaceInputGroup()
{
    try {
        const int row = surfaceInputTable_->currentRow();
        if (row < 0 || row >= static_cast<int>(surfaceInputGroups_.size())) {
            throw std::invalid_argument("線分を追加する表の行を先に選択してください。");
        }
        std::vector<std::string> combined = surfaceInputGroups_[row].wireNames;
        for (const std::string& name : SelectedSurfaceWireNames()) {
            if (std::find(combined.begin(), combined.end(), name) != combined.end()) {
                continue;
            }
            const bool assignedElsewhere = std::any_of(
                surfaceInputGroups_.begin(), surfaceInputGroups_.end(),
                [&](const auto& group) {
                    return &group != &surfaceInputGroups_[row]
                        && std::find(group.wireNames.begin(), group.wireNames.end(), name)
                            != group.wireNames.end();
                });
            if (assignedElsewhere) {
                continue;
            }
            combined.push_back(name);
        }
        if (combined.size() == surfaceInputGroups_[row].wireNames.size()) {
            throw std::invalid_argument("追加する新しい線を3D画面で選択してください。");
        }
        ValidateSurfaceInputGroup(combined, surfaceInputGroups_[row].role);
        surfaceInputGroups_[row].wireNames = std::move(combined);
        RefreshSurfaceInputTable();
        surfaceInputTable_->selectRow(row);
        UpdateSelections({}, true);
        statusBar()->showMessage(QStringLiteral("選択行へ線分を追加しました"), 3000);
    } catch (const std::exception& error) {
        const QString message = FriendlySurfaceChainError(error);
        statusBar()->showMessage(message.section('\n', 0, 0), 7000);
        QMessageBox::warning(this, QStringLiteral("経路へ追加できません"), message);
    }
}

void MainWindow::RemoveSelectedSurfaceInputGroup()
{
    const int row = surfaceInputTable_ != nullptr ? surfaceInputTable_->currentRow() : -1;
    if (row < 0 || row >= static_cast<int>(surfaceInputGroups_.size())) {
        statusBar()->showMessage(QStringLiteral("削除する表の行を選択してください"), 3500);
        return;
    }
    surfaceInputGroups_.erase(surfaceInputGroups_.begin() + row);
    RefreshSurfaceInputTable();
    statusBar()->showMessage(QStringLiteral("輪郭・断面の割当を1行削除しました"), 3000);
}

void MainWindow::ClearSurfaceInputGroups()
{
    surfaceInputGroups_.clear();
    RefreshSurfaceInputTable();
    statusBar()->showMessage(QStringLiteral("輪郭・断面の割当を消しました"), 3000);
}

void MainWindow::AddSurfaceFromConfiguredInputs(
    Project& candidate,
    const std::string& surfaceName,
    int surfaceMode,
    const std::vector<int>& fallbackWireIndices) const
{
    if (surfaceInputGroups_.empty()) {
        if ((surfaceMode == 0 && fallbackWireIndices.empty())
            || (surfaceMode == 1 && fallbackWireIndices.size() != 2)
            || (surfaceMode == 2 && fallbackWireIndices.size() < 3)
            || (surfaceMode == 3 && fallbackWireIndices.size() < 3)) {
            if (surfaceMode == 0) {
                throw std::invalid_argument("閉じた輪郭を作る直線・曲線を1本以上選択してください。");
            }
            if (surfaceMode == 1) {
                throw std::invalid_argument("断面ワイヤーを2本選択してください。");
            }
            if (surfaceMode == 3) {
                throw std::invalid_argument(
                    "外形ガイド2本を先に、その後に断面ワイヤーを1本以上選択してください。");
            }
            throw std::invalid_argument("通し方向の手前から奥の順に、断面ワイヤーを3本以上選択してください。");
        }
        std::vector<std::string> names;
        names.reserve(fallbackWireIndices.size());
        for (int index : fallbackWireIndices) {
            names.push_back(candidate.Wires()[index].name);
        }
        if (surfaceMode == 0) {
            candidate.AddPlanarSurface(surfaceName, std::move(names));
        } else if (surfaceMode == 1) {
            candidate.AddRuledSurface(surfaceName, names[0], names[1]);
        } else if (surfaceMode == 2) {
            candidate.AddLoftSurface(surfaceName, std::move(names));
        } else {
            std::vector<std::string> sections(names.begin() + 2, names.end());
            candidate.AddGuidedLoftSurface(
                surfaceName, names[0], names[1], std::move(sections));
        }
        return;
    }

    std::vector<std::vector<std::string>> boundaries;
    std::vector<std::vector<std::string>> guides;
    std::vector<std::vector<std::string>> sections;
    for (const SurfaceInputGroup& group : surfaceInputGroups_) {
        if (group.role == SurfaceInputRole::Boundary) {
            boundaries.push_back(group.wireNames);
        } else if (group.role == SurfaceInputRole::Guide) {
            guides.push_back(group.wireNames);
        } else {
            sections.push_back(group.wireNames);
        }
    }
    if (surfaceMode == 0) {
        if (boundaries.size() != 1 || !guides.empty() || !sections.empty()) {
            throw std::invalid_argument("閉じた輪郭を1行だけ表へ追加してください。");
        }
        ValidateSurfaceInputGroup(
            boundaries.front(), SurfaceInputRole::Boundary, true);
        candidate.AddPlanarSurface(surfaceName, std::move(boundaries.front()));
    } else if (surfaceMode == 1) {
        if (sections.size() != 2 || !boundaries.empty() || !guides.empty()) {
            throw std::invalid_argument("2つの断面を表へ追加してください。");
        }
        candidate.AddRuledSurface(
            surfaceName, std::move(sections[0]), std::move(sections[1]));
    } else if (surfaceMode == 2) {
        if (sections.size() < 3 || !boundaries.empty() || !guides.empty()) {
            throw std::invalid_argument("3つ以上の断面を順番に表へ追加してください。");
        }
        candidate.AddLoftSurface(surfaceName, std::move(sections));
    } else {
        if (guides.size() != 2 || sections.empty() || !boundaries.empty()) {
            throw std::invalid_argument("外形2つと断面1つ以上を表へ追加してください。");
        }
        candidate.AddGuidedLoftSurface(
            surfaceName, std::move(guides[0]), std::move(guides[1]),
            std::move(sections));
    }
}


namespace {

//! ワイヤ上で点に最も近いパラメータ(距離が許容内のときのみ)。
std::optional<double> ParameterOnWire(
    const kachakacha::model::Wire& wire, Vector3 point, double toleranceMillimeters)
{
    constexpr int kSamples = 256;
    double bestParameter = 0.0;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (int sample = 0; sample <= kSamples; ++sample) {
        const double parameter = static_cast<double>(sample) / kSamples;
        const double distance = (wire.Evaluate(parameter) - point).Length();
        if (distance < bestDistance) {
            bestDistance = distance;
            bestParameter = parameter;
        }
    }
    double low = std::max(0.0, bestParameter - 1.0 / kSamples);
    double high = std::min(1.0, bestParameter + 1.0 / kSamples);
    for (int iteration = 0; iteration < 60; ++iteration) {
        const double first = low + (high - low) / 3.0;
        const double second = high - (high - low) / 3.0;
        if ((wire.Evaluate(first) - point).LengthSquared()
            < (wire.Evaluate(second) - point).LengthSquared()) {
            high = second;
        } else {
            low = first;
        }
    }
    const double parameter = (low + high) * 0.5;
    if ((wire.Evaluate(parameter) - point).Length() > toleranceMillimeters) {
        return std::nullopt;
    }
    return parameter;
}

} // namespace

bool MainWindow::SplitSelectedWiresAtBranchPoints()
{
    constexpr double kTouchTolerance = 1.0e-3; // mm
    std::vector<std::string> selectedNames;
    for (const CadSelection& selection : viewport_->Selections()) {
        if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
            && selection.index < static_cast<int>(project_.Wires().size())) {
            selectedNames.push_back(project_.Wires()[selection.index].name);
        }
    }
    if (selectedNames.size() < 2) {
        return false;
    }
    const auto wireByName = [this](const std::string& name) -> const kachakacha::model::NamedWire* {
        for (const auto& wire : project_.Wires()) {
            if (wire.name == name) {
                return &wire;
            }
        }
        return nullptr;
    };
    // 端点が他の選択ワイヤの途中に接している箇所を1つずつ分割する
    // (分割後は形が変わるので毎回探し直す)。
    bool recordedUndo = false;
    std::vector<std::string> createdNames;
    QStringList warnings;
    for (int guard = 0; guard < 64; ++guard) {
        std::string splitTarget;
        double splitParameter = 0.0;
        for (const std::string& targetName : selectedNames) {
            const auto* target = wireByName(targetName);
            if (target == nullptr) {
                continue;
            }
            for (const std::string& otherName : selectedNames) {
                if (otherName == targetName) {
                    continue;
                }
                const auto* other = wireByName(otherName);
                if (other == nullptr || other->wire.IsClosed()) {
                    continue;
                }
                for (const Vector3 endpoint : {other->wire.Start(), other->wire.End()}) {
                    const auto parameter =
                        ParameterOnWire(target->wire, endpoint, kTouchTolerance);
                    if (parameter.has_value() && *parameter > 1.0e-4
                        && *parameter < 1.0 - 1.0e-4
                        && (target->wire.Start() - endpoint).Length() > kTouchTolerance
                        && (target->wire.End() - endpoint).Length() > kTouchTolerance) {
                        splitTarget = targetName;
                        splitParameter = *parameter;
                        break;
                    }
                }
                if (!splitTarget.empty()) {
                    break;
                }
            }
            if (!splitTarget.empty()) {
                break;
            }
        }
        if (splitTarget.empty()) {
            break;
        }
        const auto* source = wireByName(splitTarget);
        try {
            const auto sourceCopy = *source;
            const auto parts = sourceCopy.wire.SplitAt(splitParameter);
            const QString groupName =
                SuggestedDirectGroupName(ToQString(sourceCopy.name) + QStringLiteral("_part"));
            const std::string firstName = ToName(groupName + QStringLiteral("_1"));
            const std::string secondName = ToName(groupName + QStringLiteral("_2"));
            if (!recordedUndo) {
                RecordUndo();
                recordedUndo = true;
            }
            if (referenceWireName_.has_value() && *referenceWireName_ == sourceCopy.name) {
                referenceWireName_.reset();
            }
            project_.RemoveWire(sourceCopy.name);
            project_.AddWire(firstName, parts.first,
                RetargetLineConstraints(project_, sourceCopy.metadata, parts.first, true));
            project_.AddWire(secondName, parts.second,
                RetargetLineConstraints(project_, sourceCopy.metadata, parts.second, true));
            std::erase(selectedNames, sourceCopy.name);
            std::erase(createdNames, sourceCopy.name);
            selectedNames.push_back(firstName);
            selectedNames.push_back(secondName);
            createdNames.push_back(firstName);
            createdNames.push_back(secondName);
        } catch (const std::exception& error) {
            warnings << QString::fromUtf8(error.what());
            break;
        }
    }
    if (createdNames.empty()) {
        return false;
    }
    // 分割で生まれた断片のうち、反対側の端が他の選択ワイヤの端点に
    // 接していないもの(境界の外へ伸びていた部分)は選択から外す。
    const auto endpointTouchesSelection = [&](const std::string& selfName, Vector3 endpoint) {
        for (const std::string& otherName : selectedNames) {
            if (otherName == selfName) {
                continue;
            }
            const auto* other = wireByName(otherName);
            if (other == nullptr) {
                continue;
            }
            if (!other->wire.IsClosed()
                && ((other->wire.Start() - endpoint).Length() <= kTouchTolerance
                    || (other->wire.End() - endpoint).Length() <= kTouchTolerance)) {
                return true;
            }
        }
        return false;
    };
    QStringList droppedNames;
    for (const std::string& createdName : createdNames) {
        const auto* piece = wireByName(createdName);
        if (piece == nullptr) {
            continue;
        }
        if (!endpointTouchesSelection(createdName, piece->wire.Start())
            || !endpointTouchesSelection(createdName, piece->wire.End())) {
            std::erase(selectedNames, createdName);
            droppedNames << ToQString(createdName);
        }
    }
    MarkModified();
    RefreshModelViews(false);
    std::vector<CadSelection> keptSelections;
    for (const std::string& name : selectedNames) {
        for (int index = 0; index < static_cast<int>(project_.Wires().size()); ++index) {
            if (project_.Wires()[index].name == name) {
                keptSelections.push_back({CadSelectionKind::Wire, index});
                break;
            }
        }
    }
    UpdateSelections(std::move(keptSelections), true);
    QString message = QStringLiteral("分岐点でワイヤーを自動分割しました");
    if (!droppedNames.isEmpty()) {
        message += QStringLiteral("（境界の外側 %1 は選択から外しました）")
            .arg(droppedNames.join(QStringLiteral("、")));
    }
    if (!warnings.isEmpty()) {
        message += QStringLiteral(" / 一部は分割できません: %1")
            .arg(warnings.join(QStringLiteral("、")));
    }
    statusBar()->showMessage(message, 6000);
    return true;
}

void MainWindow::CreateSurfaceFromSelection()
{
    try {
        ValidateObjectName(surfaceName_->text());
        // T字分岐(線の途中から分岐)は、分割済みの線と同じように扱えるよう
        // 接点で自動分割してから面を作る(オーナー指示)。
        SplitSelectedWiresAtBranchPoints();
        std::vector<int> wireIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }

        for (int index : wireIndices) {
            if (project_.Wires()[index].metadata.construction) {
                throw std::invalid_argument("補助線は面の境界や断面には使えません。通常線へ戻してから選択してください。");
            }
        }

        Project candidate = project_;
        const std::string name = ToName(surfaceName_->text());
        const int surfaceMode = surfaceType_->currentIndex();
        AddSurfaceFromConfiguredInputs(
            candidate, name, surfaceMode, wireIndices);

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        const int surfaceIndex = static_cast<int>(project_.Surfaces().size() - 1);
        UpdateSelection({CadSelectionKind::Surface, surfaceIndex}, true);
        toolsTabs_->setCurrentIndex(2);
        surfaceName_->setText(SuggestedSurfaceName());
        const std::size_t logicalInputCount = surfaceInputGroups_.empty()
            ? wireIndices.size() : surfaceInputGroups_.size();
        const std::size_t sourceWireCount = surfaceInputGroups_.empty()
            ? wireIndices.size()
            : std::accumulate(
                  surfaceInputGroups_.begin(), surfaceInputGroups_.end(),
                  std::size_t{0}, [](std::size_t count, const auto& group) {
                      return count + group.wireNames.size();
                  });
        surfaceInputGroups_.clear();
        RefreshSurfaceInputTable();
        const QString message = surfaceMode == 0
            ? sourceWireCount == 1
                ? QStringLiteral("閉じたワイヤーから平面を作成しました")
                : QStringLiteral("%1本の直線・曲線をつないで平面を作成しました")
                      .arg(sourceWireCount)
            : surfaceMode == 1
            ? QStringLiteral("2断面から曲面を作成しました")
            : surfaceMode == 2
            ? QStringLiteral("%1断面からロフト面を作成しました").arg(logicalInputCount)
            : QStringLiteral("外形2本と断面%1本からガイド付き面を作成しました")
                  .arg(logicalInputCount - 2);
        statusBar()->showMessage(message, 3500);
    } catch (const std::exception& error) {
        const QString message = FriendlySurfaceChainError(error);
        statusBar()->showMessage(message.section('\n', 0, 0), 7000);
        QMessageBox::warning(this, QStringLiteral("面を作成できません"), message);
    }
}

void MainWindow::AddSelectedGordonGuides()
{
    const auto& selections = viewport_->Selections();
    if (selections.empty()) {
        statusBar()->showMessage(QStringLiteral("外形ガイドにするワイヤーを選択してください"), 4000);
        return;
    }
    std::vector<std::string> wireNames;
    wireNames.reserve(selections.size());
    for (const CadSelection& selection : selections) {
        if (selection.kind != CadSelectionKind::Wire || selection.index < 0
            || selection.index >= static_cast<int>(project_.Wires().size())) {
            statusBar()->showMessage(QStringLiteral("外形ガイドにはワイヤーだけを選択してください"), 4000);
            return;
        }
        wireNames.push_back(project_.Wires()[selection.index].name);
    }

    int added = 0;
    for (const std::string& name : wireNames) {
        if (std::find(gordonGuideNames_.begin(), gordonGuideNames_.end(), name) == gordonGuideNames_.end()) {
            gordonGuideNames_.push_back(name);
            ++added;
        }
    }
    RefreshGordonGuideLabel();
    statusBar()->showMessage(added > 0
            ? QStringLiteral("外形ガイドに%1本追加しました").arg(added)
            : QStringLiteral("選択したワイヤーは既に外形ガイドに追加されています"),
        3000);
}

void MainWindow::ClearGordonGuides()
{
    gordonGuideNames_.clear();
    RefreshGordonGuideLabel();
    statusBar()->showMessage(QStringLiteral("外形ガイドを解除しました"), 2500);
}

void MainWindow::RefreshGordonGuideLabel()
{
    if (gordonGuideLabel_ == nullptr) {
        return;
    }
    if (gordonGuideNames_.empty()) {
        gordonGuideLabel_->setText(QStringLiteral("外形ガイド: (なし)"));
        return;
    }
    QStringList names;
    names.reserve(static_cast<int>(gordonGuideNames_.size()));
    for (const std::string& name : gordonGuideNames_) {
        names.push_back(ToQString(name));
    }
    gordonGuideLabel_->setText(QStringLiteral("外形ガイド: %1").arg(names.join(QStringLiteral(", "))));
}

void MainWindow::CreateGordonSurfaceFromSelection()
{
    try {
        std::vector<int> wireIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }
        if (wireIndices.size() < 2) {
            throw std::invalid_argument("通し方向の手前から奥の順に、断面ワイヤーを2本以上選択してください。");
        }
        for (int index : wireIndices) {
            if (project_.Wires()[index].metadata.construction) {
                throw std::invalid_argument("補助線は断面には使えません。通常線へ戻してから選択してください。");
            }
        }
        if (gordonGuideNames_.empty()) {
            throw std::invalid_argument("外形ガイドを1本以上追加してください。");
        }

        Project candidate = project_;
        const std::string name = ToName(SuggestedSurfaceName());
        std::vector<std::string> sectionNames;
        sectionNames.reserve(wireIndices.size());
        for (int index : wireIndices) {
            sectionNames.push_back(candidate.Wires()[index].name);
        }
        candidate.AddGordonSurface(name, sectionNames, gordonGuideNames_);

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        const int surfaceIndex = static_cast<int>(project_.Surfaces().size() - 1);
        UpdateSelection({CadSelectionKind::Surface, surfaceIndex}, true);
        toolsTabs_->setCurrentIndex(2);
        const double maximumGap = project_.Surfaces()[surfaceIndex].surface.MaximumGuideGap();
        statusBar()->showMessage(
            QStringLiteral("面 %1 を作成しました（ガイド交点の最大ずれ %2 mm）")
                .arg(ToQString(name), Number(maximumGap)),
            4000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::ProjectSelectedWiresToSurface()
{
    try {
        const std::optional<WorkPlane> drawingPlane = project_.FindWorkPlane(ToName(projectionPlane_->currentText()));
        const std::optional<kachakacha::model::Surface> targetSurface = project_.FindSurface(ToName(projectionSurface_->currentText()));
        if (!drawingPlane.has_value()) {
            throw std::invalid_argument("平面図を描いた作業平面を選択してください。");
        }
        if (!targetSurface.has_value()) {
            throw std::invalid_argument("投影先の面を選択してください。");
        }

        std::vector<int> wireIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }
        if (wireIndices.empty()) {
            throw std::invalid_argument("投影する平面図ワイヤーを3D画面で選択してください。");
        }
        for (int index : wireIndices) {
            const auto& source = project_.Wires()[index];
            if (source.metadata.construction) {
                throw std::invalid_argument("補助線は面へ投影できません。通常線へ戻してから選択してください。");
            }
            if (source.projection.has_value()) {
                throw std::invalid_argument("投影後のワイヤーではなく、元の平面図を選択してください。");
            }
            if (!WireLiesOnPlane(source.wire, *drawingPlane)) {
                throw std::invalid_argument("選択ワイヤーが指定した平面図上にありません。");
            }
        }
        Project candidate = project_;
        std::vector<std::string> createdNames;
        for (int index : wireIndices) {
            const std::string sourceName = project_.Wires()[index].name;
            const QString name = SuggestedDirectGroupName(
                ToQString(sourceName) + QStringLiteral("_on_") + projectionSurface_->currentText());
            candidate.AddProjectedWire(
                ToName(name), sourceName, ToName(projectionSurface_->currentText()), drawingPlane->Normal());
            createdNames.push_back(ToName(name));
        }

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        std::vector<CadSelection> resultingSelections;
        for (const std::string& name : createdNames) {
            const auto found = std::find_if(project_.Wires().begin(), project_.Wires().end(), [&](const auto& wire) {
                return wire.name == name;
            });
            resultingSelections.push_back({
                CadSelectionKind::Wire,
                static_cast<int>(std::distance(project_.Wires().begin(), found)),
            });
        }
        UpdateSelections(std::move(resultingSelections), true);
        toolsTabs_->setCurrentIndex(2);
        statusBar()->showMessage(QStringLiteral("%1本の平面図ワイヤーを面へ投影しました").arg(createdNames.size()), 4000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::CreateProtrudingLightCase()
{
    try {
        ValidateObjectName(lightCaseRootName_->text());
        ValidateObjectName(lightCaseSurfaceName_->text());

        int frontWireIndex = -1;
        std::string targetSurfaceName;
        QString targetDisplayName;
        int targetCount = 0;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                if (frontWireIndex >= 0) {
                    throw std::invalid_argument("ライトケース最前面の閉じた輪郭は1本だけ選択してください。");
                }
                frontWireIndex = selection.index;
            } else if (selection.kind == CadSelectionKind::Surface && selection.index >= 0
                && selection.index < static_cast<int>(project_.Surfaces().size())) {
                ++targetCount;
                targetSurfaceName = project_.Surfaces()[selection.index].name;
                targetDisplayName = ToQString(targetSurfaceName);
            } else if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                && selection.index < static_cast<int>(project_.Plates().size())) {
                ++targetCount;
                targetSurfaceName = project_.Plates()[selection.index].sourceSurfaceName;
                targetDisplayName = ToQString(project_.Plates()[selection.index].name);
            }
        }
        if (frontWireIndex < 0 || targetCount != 1) {
            throw std::invalid_argument(
                "最前面の閉じた輪郭1本と、根元を置く面または板1枚を3D画面で選択してください。");
        }

        const auto& front = project_.Wires()[frontWireIndex];
        if (front.metadata.construction) {
            throw std::invalid_argument("補助線はライトケース最前面に使えません。通常線へ戻してください。");
        }
        if (front.projection.has_value() || front.plateOffset.has_value()) {
            throw std::invalid_argument("投影後や板厚位置の輪郭ではなく、ライト最前面の元輪郭を選択してください。");
        }
        if (!front.wire.IsClosed()) {
            throw std::invalid_argument("ライトケース最前面には閉じた輪郭を選択してください。");
        }

        Vector3 direction;
        if (lightCaseDirectionMode_->currentIndex() == 0) {
            try {
                const auto frontSurface = kachakacha::model::Surface::Planar(front.wire);
                if (!frontSurface.PlanarWorkPlane().has_value()) {
                    throw std::invalid_argument("planar work plane is unavailable");
                }
                direction = frontSurface.PlanarWorkPlane()->Normal();
            } catch (const std::exception&) {
                throw std::invalid_argument(
                    "最前面輪郭は、1枚の作業平面上にある閉じた輪郭にしてください。");
            }
        } else if (lightCaseDirectionMode_->currentIndex() == 1) {
            if (!referenceWireName_.has_value()) {
                throw std::invalid_argument("伸ばす方向の基準にする直線を先に設定してください。");
            }
            const auto reference = std::find_if(
                project_.Wires().begin(), project_.Wires().end(), [this](const auto& wire) {
                    return wire.name == *referenceWireName_ && wire.wire.Kind() == WireKind::Line;
                });
            if (reference == project_.Wires().end()) {
                throw std::invalid_argument("設定した基準線が見つかりません。もう一度設定してください。");
            }
            direction = reference->wire.End() - reference->wire.Start();
        } else {
            direction = ReadVector3(lightCaseDirection_);
        }
        if (!direction.IsFinite() || direction.LengthSquared() <= 1.0e-18) {
            throw std::invalid_argument("伸ばす方向には0ではないXYZ方向を指定してください。");
        }
        direction = direction.Normalized();

        const std::string rootName = ToName(lightCaseRootName_->text());
        const std::string sideName = ToName(lightCaseSurfaceName_->text());
        Project candidate = project_;
        try {
            candidate.AddProjectedWire(rootName, front.name, targetSurfaceName, direction);
        } catch (const std::invalid_argument& error) {
            const std::string message = error.what();
            if (message.starts_with("Projection") || message.starts_with("Projected")) {
                throw std::invalid_argument(
                    "最前面輪郭から伸ばした線が接続先に届きません。方向と接続先の範囲を確認してください。");
            }
            throw;
        }
        candidate.AddRuledSurface(sideName, front.name, rootName);

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        const auto root = std::find_if(project_.Wires().begin(), project_.Wires().end(),
            [&](const auto& wire) { return wire.name == rootName; });
        const auto side = std::find_if(project_.Surfaces().begin(), project_.Surfaces().end(),
            [&](const auto& surface) { return surface.name == sideName; });
        UpdateSelections({
            {CadSelectionKind::Wire, static_cast<int>(std::distance(project_.Wires().begin(), root))},
            {CadSelectionKind::Surface, static_cast<int>(std::distance(project_.Surfaces().begin(), side))},
        }, true);

        lightCaseRootName_->setText(SuggestedDirectGroupName(QStringLiteral("light_root")));
        int nextCaseNumber = 1;
        while (project_.FindSurface(ToName(QStringLiteral("light_case_%1").arg(nextCaseNumber))).has_value()) {
            ++nextCaseNumber;
        }
        lightCaseSurfaceName_->setText(QStringLiteral("light_case_%1").arg(nextCaseNumber));
        statusBar()->showMessage(
            QStringLiteral("%1へ根元輪郭を投影し、斜めライトケース側面を作成しました")
                .arg(targetDisplayName),
            4500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 6500);
    }
}

void MainWindow::CreatePlateFromSurface()
{
    try {
        ValidateObjectName(plateName_->text());
        const std::string sourceSurfaceName = ToName(plateSurface_->currentText());
        if (!project_.FindSurface(sourceSurfaceName).has_value()) {
            throw std::invalid_argument("板材にする面を3D画面または一覧で選択してください。");
        }

        Project candidate = project_;
        const std::string name = ToName(plateName_->text());
        const auto direction = static_cast<PlateThicknessDirection>(plateDirection_->currentData().toInt());
        candidate.AddPlate(
            name,
            sourceSurfaceName,
            plateThickness_->value(),
            plateVariableThickness_->isChecked()
                ? plateEndThickness_->value() : plateThickness_->value(),
            direction,
            ToName(plateMaterial_->currentData().toString()));
        candidate.SetSurfaceVisible(sourceSurfaceName, false);

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        const int plateIndex = static_cast<int>(project_.Plates().size() - 1);
        UpdateSelection({CadSelectionKind::Plate, plateIndex}, true);
        toolsTabs_->setCurrentIndex(2);
        plateName_->setText(SuggestedPlateName());
        statusBar()->showMessage(QStringLiteral("板厚 %1 mm の板材を作成しました").arg(plateThickness_->value()), 3500);
    } catch (const std::exception& error) {
        const QString message = FriendlyPlateCreationError(error);
        statusBar()->showMessage(message.section('\n', 0, 0), 8000);
        QMessageBox::warning(this, QStringLiteral("板材化できません"), message);
    }
}

void MainWindow::CreatePlateFromSelectedWires()
{
    try {
        ValidateObjectName(plateName_->text());
        std::vector<int> wireIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())
                && std::find(wireIndices.begin(), wireIndices.end(), selection.index) == wireIndices.end()) {
                wireIndices.push_back(selection.index);
            }
        }
        if (wireIndices.empty() && surfaceInputGroups_.empty()) {
            throw std::invalid_argument("3D板の輪郭または断面ワイヤーを3D画面で選択してください。");
        }
        if (surfaceInputGroups_.empty() && wireIndices.size() == 1
            && !project_.Wires()[wireIndices.front()].wire.IsClosed()) {
            throw std::invalid_argument("1本から平板を作る場合は閉じた輪郭を選択してください。");
        }

        Project candidate = project_;
        const std::string plateName = ToName(plateName_->text());
        std::string surfaceName = plateName + "_surface";
        int suffix = 2;
        while (candidate.FindSurface(surfaceName).has_value()) {
            surfaceName = plateName + "_surface" + std::to_string(suffix++);
        }
        if (!surfaceInputGroups_.empty()) {
            AddSurfaceFromConfiguredInputs(
                candidate, surfaceName, surfaceType_->currentIndex(), wireIndices);
        } else {
            std::vector<std::string> wireNames;
            wireNames.reserve(wireIndices.size());
            for (int index : wireIndices) {
                wireNames.push_back(candidate.Wires()[index].name);
            }
            bool formsPlanarClosedContour = wireNames.size() == 1;
            if (wireNames.size() > 1) {
                std::vector<Wire> boundaryWires;
                boundaryWires.reserve(wireIndices.size());
                for (int index : wireIndices) {
                    boundaryWires.push_back(candidate.Wires()[index].wire);
                }
                try {
                    const Wire joined = JoinWireChain(boundaryWires);
                    (void)kachakacha::model::Surface::Planar(joined);
                    formsPlanarClosedContour = true;
                } catch (const std::invalid_argument&) {
                    formsPlanarClosedContour = false;
                }
            }
            const bool guidedLoft = surfaceType_ != nullptr
                && surfaceType_->currentIndex() == 3 && wireNames.size() >= 3;
            if (guidedLoft) {
                std::vector<std::string> sectionNames(
                    wireNames.begin() + 2, wireNames.end());
                candidate.AddGuidedLoftSurface(
                    surfaceName, wireNames[0], wireNames[1],
                    std::move(sectionNames));
            } else if (formsPlanarClosedContour) {
                candidate.AddPlanarSurface(surfaceName, wireNames);
            } else if (wireNames.size() == 1) {
                candidate.AddPlanarSurface(surfaceName, wireNames.front());
            } else if (wireNames.size() == 2) {
                candidate.AddRuledSurface(surfaceName, wireNames[0], wireNames[1]);
            } else {
                candidate.AddLoftSurface(surfaceName, wireNames);
            }
        }
        const auto direction = static_cast<PlateThicknessDirection>(plateDirection_->currentData().toInt());
        candidate.AddPlate(
            plateName,
            surfaceName,
            plateThickness_->value(),
            plateVariableThickness_->isChecked()
                ? plateEndThickness_->value() : plateThickness_->value(),
            direction,
            ToName(plateMaterial_->currentData().toString()));
        candidate.SetSurfaceVisible(surfaceName, false);

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelection(
            {CadSelectionKind::Plate, static_cast<int>(project_.Plates().size() - 1)}, true);
        surfaceInputGroups_.clear();
        RefreshSurfaceInputTable();
        plateName_->setText(SuggestedPlateName());
        statusBar()->showMessage(
            plateVariableThickness_->isChecked()
                ? QStringLiteral("選択断面から可変板厚の3D板を作成しました")
                : QStringLiteral("選択ワイヤーから3D板を作成しました"),
            4000);
    } catch (const std::exception& error) {
        const QString message = FriendlyPlateCreationError(error);
        statusBar()->showMessage(message.section('\n', 0, 0), 8000);
        QMessageBox::warning(this, QStringLiteral("板材化できません"), message);
    }
}

void MainWindow::UpdateSelectedPlate()
{
    try {
        const CadSelection selection = viewport_->Selection();
        if (selection.kind != CadSelectionKind::Plate || selection.index < 0
            || selection.index >= static_cast<int>(project_.Plates().size())) {
            throw std::invalid_argument("変更する板材を3D画面またはモデル一覧で選択してください。");
        }
        const std::string sourceSurfaceName = ToName(plateSurface_->currentText());
        const auto direction = static_cast<PlateThicknessDirection>(plateDirection_->currentData().toInt());

        Project candidate = project_;
        candidate.UpdatePlate(
            candidate.Plates()[selection.index].name,
            sourceSurfaceName,
            plateThickness_->value(),
            plateVariableThickness_->isChecked()
                ? plateEndThickness_->value() : plateThickness_->value(),
            direction,
            ToName(plateMaterial_->currentData().toString()));
        candidate.SetSurfaceVisible(sourceSurfaceName, false);

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelection(selection, true);
        statusBar()->showMessage(QStringLiteral("板材の板厚・方向・材質を更新しました"), 3500);
    } catch (const std::exception& error) {
        const QString message = FriendlyPlateCreationError(error);
        statusBar()->showMessage(message.section('\n', 0, 0), 8000);
        QMessageBox::warning(this, QStringLiteral("板材を更新できません"), message);
    }
}

void MainWindow::CreatePlateOffsetWires()
{
    try {
        int plateIndex = -1;
        std::vector<int> wireIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                && selection.index < static_cast<int>(project_.Plates().size())) {
                if (plateIndex >= 0 && plateIndex != selection.index) {
                    throw std::invalid_argument("板厚位置の基準にする板材は1枚だけ選択してください。");
                }
                plateIndex = selection.index;
            } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }
        if (plateIndex < 0 || wireIndices.empty()) {
            throw std::invalid_argument("板材1枚と、その元の面へ投影したワイヤーを選択してください。");
        }

        Project candidate = project_;
        const std::string plateName = candidate.Plates()[plateIndex].name;
        const double throughThickness = plateOffsetLayer_->currentData().toDouble();
        const std::string layerSuffix = throughThickness > 0.75 ? "_plus"
            : throughThickness < 0.25 ? "_minus" : "_center";
        std::vector<std::string> createdNames;
        for (int wireIndex : wireIndices) {
            const std::string sourceName = candidate.Wires()[wireIndex].name;
            std::string name = sourceName + layerSuffix;
            int suffix = 2;
            const auto nameExists = [&](const std::string& candidateName) {
                return std::any_of(candidate.Wires().begin(), candidate.Wires().end(),
                    [&](const auto& wire) { return wire.name == candidateName; });
            };
            while (nameExists(name)) {
                name = sourceName + layerSuffix + std::to_string(suffix++);
            }
            candidate.AddPlateOffsetWire(name, sourceName, plateName, throughThickness);
            createdNames.push_back(std::move(name));
        }

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        std::vector<CadSelection> createdSelections;
        for (const std::string& name : createdNames) {
            const auto position = std::find_if(project_.Wires().begin(), project_.Wires().end(),
                [&](const auto& wire) { return wire.name == name; });
            createdSelections.push_back({CadSelectionKind::Wire,
                static_cast<int>(std::distance(project_.Wires().begin(), position))});
        }
        UpdateSelections(std::move(createdSelections), true);
        statusBar()->showMessage(
            QStringLiteral("板厚位置へ%1本のワイヤーを作成しました").arg(createdNames.size()), 4000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 6000);
    }
}

void MainWindow::CreateSurfaceJig()
{
    try {
        ValidateObjectName(jigName_->text());
        const std::string sourceSurfaceName = ToName(jigSurface_->currentText());
        if (!project_.FindSurface(sourceSurfaceName).has_value()) {
            throw std::invalid_argument("治具の元にする面を3D画面または一覧で選択してください。");
        }

        Project candidate = project_;
        const std::string name = ToName(jigName_->text());
        const auto side = static_cast<JigSide>(jigSide_->currentData().toInt());
        candidate.AddSurfaceJig(
            name,
            sourceSurfaceName,
            {},
            side,
            jigClearance_->value(),
            jigThickness_->value());
        candidate.SetSurfaceVisible(sourceSurfaceName, false);

        const auto analysis = candidate.Bodies().back().body.AnalyzePrintability(jigMinimumWall_->value());
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        const int bodyIndex = static_cast<int>(project_.Bodies().size() - 1);
        UpdateSelection({CadSelectionKind::Body, bodyIndex}, true);
        toolsTabs_->setCurrentIndex(2);
        jigName_->setText(SuggestedBodyName());
        jigAnalysisLabel_->setStyleSheet(
            analysis.meetsMinimumWall ? "color: #35664a;" : "color: #a32734;");
        jigAnalysisLabel_->setText(analysis.meetsMinimumWall
            ? QStringLiteral("造形確認: 最小肉厚 %1 mm を満たします").arg(analysis.minimumWallMillimeters, 0, 'f', 2)
            : QStringLiteral("造形警告: 厚み %1 mm は必要最小肉厚に不足します").arg(analysis.minimumWallMillimeters, 0, 'f', 2));
        statusBar()->showMessage(QStringLiteral("曲面から成形治具を作成しました"), 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::UpdateSelectedBody()
{
    try {
        const CadSelection selection = viewport_->Selection();
        if (selection.kind != CadSelectionKind::Body || selection.index < 0
            || selection.index >= static_cast<int>(project_.Bodies().size())) {
            throw std::invalid_argument("変更する治具を3D画面またはモデル一覧で選択してください。");
        }
        const std::string sourceSurfaceName = ToName(jigSurface_->currentText());
        const auto side = static_cast<JigSide>(jigSide_->currentData().toInt());

        Project candidate = project_;
        const auto& current = candidate.Bodies()[selection.index];
        candidate.UpdateSurfaceJig(
            current.name,
            sourceSurfaceName,
            current.body.Range(),
            side,
            jigClearance_->value(),
            jigThickness_->value());
        candidate.SetSurfaceVisible(sourceSurfaceName, false);
        const auto analysis = candidate.Bodies()[selection.index].body.AnalyzePrintability(jigMinimumWall_->value());

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelection(selection, true);
        jigAnalysisLabel_->setStyleSheet(
            analysis.meetsMinimumWall ? "color: #35664a;" : "color: #a32734;");
        jigAnalysisLabel_->setText(analysis.meetsMinimumWall
            ? QStringLiteral("造形確認: 最小肉厚 %1 mm を満たします").arg(analysis.minimumWallMillimeters, 0, 'f', 2)
            : QStringLiteral("造形警告: 厚み %1 mm は必要最小肉厚に不足します").arg(analysis.minimumWallMillimeters, 0, 'f', 2));
        statusBar()->showMessage(QStringLiteral("治具の側・隙間・厚みを更新しました"), 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::ModifySelectedPlateWires(
    void (*applyToPlate)(Project& candidate, std::string_view plateName, const std::string& wireName),
    const char* onlyOnePlateMessage, const char* selectionRequiredMessage,
    const QString& successMessageTemplate)
{
    try {
        const std::vector<CadSelection> selections = viewport_->Selections();
        int plateIndex = -1;
        std::vector<int> wireIndices;
        for (const CadSelection& selection : selections) {
            if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                && selection.index < static_cast<int>(project_.Plates().size())) {
                if (plateIndex >= 0 && plateIndex != selection.index) {
                    throw std::invalid_argument(onlyOnePlateMessage);
                }
                plateIndex = selection.index;
            } else if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                wireIndices.push_back(selection.index);
            }
        }
        if (plateIndex < 0 || wireIndices.empty()) {
            throw std::invalid_argument(selectionRequiredMessage);
        }

        Project candidate = project_;
        const std::string plateName = candidate.Plates()[plateIndex].name;
        for (int wireIndex : wireIndices) {
            applyToPlate(candidate, plateName, candidate.Wires()[wireIndex].name);
        }
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        UpdateSelections(selections, true);
        statusBar()->showMessage(successMessageTemplate.arg(wireIndices.size()), 3500);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::AddSelectedPlateOpenings()
{
    ModifySelectedPlateWires(
        [](Project& candidate, std::string_view plateName, const std::string& wireName) {
            candidate.AddPlateOpening(plateName, wireName);
        },
        "開口を作る板材は1枚だけ選択してください。",
        "板材1枚と、その面へ投影した閉じた輪郭を選択してください。",
        QStringLiteral("板材へ%1個の開口を追加しました"));
}

void MainWindow::RemoveSelectedPlateOpenings()
{
    ModifySelectedPlateWires(
        [](Project& candidate, std::string_view plateName, const std::string& wireName) {
            candidate.RemovePlateOpening(plateName, wireName);
        },
        "開口を外す板材は1枚だけ選択してください。",
        "板材1枚と、開口から外す輪郭を選択してください。",
        QStringLiteral("板材から%1個の開口を外しました"));
}

void MainWindow::AddSelectedPlateReliefCuts()
{
    ModifySelectedPlateWires(
        [](Project& candidate, std::string_view plateName, const std::string& wireName) {
            candidate.AddPlateReliefCut(plateName, wireName);
        },
        "切れ目を設定する板材は1枚だけ選択してください。",
        "板材1枚と、その面へ投影した切れ目ワイヤーを選択してください。",
        QStringLiteral("展開用の手動切れ目を%1本追加しました"));
}

void MainWindow::RemoveSelectedPlateReliefCuts()
{
    ModifySelectedPlateWires(
        [](Project& candidate, std::string_view plateName, const std::string& wireName) {
            candidate.RemovePlateReliefCut(plateName, wireName);
        },
        "切れ目を外す板材は1枚だけ選択してください。",
        "板材1枚と、切れ目から外すワイヤーを選択してください。",
        QStringLiteral("展開用の切れ目から%1本外しました"));
}

void MainWindow::AddSelectedPlateSplitLines()
{
    ModifySelectedPlateWires(
        [](Project& candidate, std::string_view plateName, const std::string& wireName) {
            candidate.AddPlateSplitLine(plateName, wireName);
        },
        "分割線を設定する板材は1枚だけ選択してください。",
        "板材1枚と、その面へ投影した分割ワイヤーを選択してください。",
        QStringLiteral("展開片を分ける分割線を%1本追加しました"));
}

void MainWindow::RemoveSelectedPlateSplitLines()
{
    ModifySelectedPlateWires(
        [](Project& candidate, std::string_view plateName, const std::string& wireName) {
            candidate.RemovePlateSplitLine(plateName, wireName);
        },
        "分割線を解除する板材は1枚だけ選択してください。",
        "板材1枚と、解除する分割ワイヤーを選択してください。",
        QStringLiteral("展開片の分割線を%1本解除しました"));
}

void MainWindow::SplitSelectedPlate()
{
    try {
        std::vector<int> selectedPlateIndices;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
                && selection.index < static_cast<int>(project_.Plates().size())) {
                selectedPlateIndices.push_back(selection.index);
            }
        }
        std::sort(selectedPlateIndices.begin(), selectedPlateIndices.end());
        selectedPlateIndices.erase(
            std::unique(selectedPlateIndices.begin(), selectedPlateIndices.end()),
            selectedPlateIndices.end());
        if (selectedPlateIndices.size() != 1) {
            throw std::invalid_argument("分割する板材を1枚だけ選択してください。");
        }
        const int plateIndex = selectedPlateIndices.front();
        const std::string sourceName = project_.Plates()[plateIndex].name;
        std::vector<std::string> reservedNames;
        const auto uniquePieceName = [this, &sourceName, &reservedNames](std::string suffix) {
            std::string candidate = sourceName + std::move(suffix);
            int number = 2;
            while (project_.FindPlate(candidate).has_value()
                || std::find(reservedNames.begin(), reservedNames.end(), candidate) != reservedNames.end()) {
                candidate = sourceName + "_part" + std::to_string(number++);
            }
            reservedNames.push_back(candidate);
            return candidate;
        };
        const std::string firstName = uniquePieceName("_part1");
        const std::string secondName = uniquePieceName("_part2");
        const auto axis = static_cast<PlateSplitAxis>(plateSplitAxis_->currentData().toInt());
        const double parameter = plateSplitPosition_->value() / 100.0;

        Project candidate = project_;
        candidate.SplitPlate(sourceName, axis, parameter, firstName, secondName);
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);

        std::vector<CadSelection> pieces;
        for (int index = 0; index < static_cast<int>(project_.Plates().size()); ++index) {
            if (project_.Plates()[index].name == firstName || project_.Plates()[index].name == secondName) {
                pieces.push_back({CadSelectionKind::Plate, index});
            }
        }
        UpdateSelections(std::move(pieces), true);
        statusBar()->showMessage(
            QStringLiteral("板材を%1%の位置で2部品に分割しました").arg(plateSplitPosition_->value()),
            4000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::UpdatePlateSplitPreview()
{
    if (viewport_ == nullptr || plateSplitAxis_ == nullptr || plateSplitPosition_ == nullptr) {
        return;
    }
    if (toolsTabs_ == nullptr || toolsTabs_->currentIndex() != 2) {
        viewport_->SetPlateSplitPreview(std::nullopt, 0.5);
        return;
    }
    viewport_->SetPlateSplitPreview(
        static_cast<PlateSplitAxis>(plateSplitAxis_->currentData().toInt()),
        plateSplitPosition_->value() / 100.0);
}

// ---- 積層(重ね板、合意9) --------------------------------------------------

namespace {

//! 3D画面で選択されている板材のインデックスを選択順で返す(重複除去)。
[[nodiscard]] std::vector<int> SelectedPlateIndicesInOrder(
    const std::vector<CadSelection>& selections, std::size_t plateCount)
{
    std::vector<int> indices;
    for (const CadSelection& selection : selections) {
        if (selection.kind != CadSelectionKind::Plate || selection.index < 0
            || selection.index >= static_cast<int>(plateCount)) {
            continue;
        }
        if (std::find(indices.begin(), indices.end(), selection.index) == indices.end()) {
            indices.push_back(selection.index);
        }
    }
    return indices;
}

} // namespace

void MainWindow::AddLaminationToSelectedPlate()
{
    try {
        const std::vector<int> indices = SelectedPlateIndicesInOrder(
            viewport_->Selections(), project_.Plates().size());
        if (indices.size() != 1) {
            throw std::invalid_argument("積層の土台になる板材を1枚だけ選択してください。");
        }
        const double thickness = laminateThicknessSpin_->value();
        const int layers = laminateCountSpin_->value();
        std::string baseName = project_.Plates()[indices.front()].name;

        Project candidate = project_;
        std::vector<std::string> created;
        for (int layer = 0; layer < layers; ++layer) {
            // 名前は <土台>_L2, _L3, ...(空き番号)。
            std::string layerName;
            for (int suffix = 2;; ++suffix) {
                layerName = baseName + "_L" + std::to_string(suffix);
                if (!candidate.FindPlate(layerName).has_value()) {
                    break;
                }
            }
            candidate.AddLaminatedPlate(layerName, baseName, thickness, {});
            created.push_back(layerName);
            baseName = layerName; // 次の層はこの層の上に重ねる。
        }

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(
            QStringLiteral("積層を%1層追加しました（%2 ほか）")
                .arg(created.size())
                .arg(QString::fromStdString(created.front())),
            5000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::LinkSelectedPlatesAsLaminate()
{
    try {
        const std::vector<int> indices = SelectedPlateIndicesInOrder(
            viewport_->Selections(), project_.Plates().size());
        if (indices.size() != 2) {
            throw std::invalid_argument(
                "積層関係にする板材を2枚選択してください（先=下、後=上）。");
        }
        const std::string baseName = project_.Plates()[indices[0]].name;
        const std::string upperName = project_.Plates()[indices[1]].name;

        Project candidate = project_;
        candidate.SetPlateLaminate(upperName, baseName);

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(
            QStringLiteral("%1 を %2 の上の積層として関連付けました")
                .arg(QString::fromStdString(upperName), QString::fromStdString(baseName)),
            5000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::ClearSelectedPlateLaminate()
{
    try {
        const std::vector<int> indices = SelectedPlateIndicesInOrder(
            viewport_->Selections(), project_.Plates().size());
        if (indices.empty()) {
            throw std::invalid_argument("積層関係を解除する板材を選択してください。");
        }
        Project candidate = project_;
        int cleared = 0;
        for (const int index : indices) {
            const auto& plate = project_.Plates()[index];
            if (plate.laminateBaseName.empty()) {
                continue;
            }
            candidate.SetPlateLaminate(plate.name, {});
            ++cleared;
        }
        if (cleared == 0) {
            throw std::invalid_argument("選択中の板材に積層関係はありません。");
        }
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(
            QStringLiteral("積層関係を%1件解除しました").arg(cleared), 4000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}
