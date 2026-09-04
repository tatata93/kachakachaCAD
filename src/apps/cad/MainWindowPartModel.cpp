// 部材タブ(部材近似モデル)の配線。UI部品は PartModelPanel、型紙表示は
// PartPatternViewDialog に分離し、ここではプロジェクト操作だけを行う
// (ADR 0018: MainWindow.cpp を太らせない)。

#include "MainWindow.h"
#include "CadViewport.h"
#include "PartModelPanel.h"
#include "PartPatternViewDialog.h"

#include "kachakacha/io/PartFoldState.h"
#include "kachakacha/io/PartPatterns.h"
#include "kachakacha/io/ProjectScript.h"
#include "kachakacha/occt/BodyExport.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QStatusBar>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using kachakacha::geometry::Vector3;
using kachakacha::model::NamedPartModel;
using kachakacha::model::ObjectSetState;
using kachakacha::model::PartApproximationOptions;
using kachakacha::model::Project;

namespace {

[[nodiscard]] QString ToQString(const std::string& text)
{
    return QString::fromStdString(text);
}

[[nodiscard]] std::string ToName(const QString& text)
{
    return text.trimmed().toStdString();
}

[[nodiscard]] const NamedPartModel* FindPartModel(
    const Project& project, const std::string& name)
{
    for (const NamedPartModel& model : project.PartModels()) {
        if (model.name == name) {
            return &model;
        }
    }
    return nullptr;
}

} // namespace

QWidget* MainWindow::BuildPartModelPanelTab()
{
    partModelPanel_ = new PartModelPanel;
    partModelPanel_->onCreate = [this] { CreatePartModelFromPanel(); };
    partModelPanel_->onCollectUnitMembers = [this] { CollectUnitMembersFromSelection(); };
    partModelPanel_->onCreateUnit = [this] { CreateApproximationUnitFromPanel(); };
    partModelPanel_->onRecalculate = [this] { RecalculateSelectedPartModel(); };
    partModelPanel_->onRemove = [this] { RemoveSelectedPartModel(); };
    partModelPanel_->onExtract = [this] { ExtractSelectedPartModelBoundaries(); };
    partModelPanel_->onShowPatterns = [this] { ShowSelectedPartPatterns(); };
    partModelPanel_->onOverlayVisibility = [this](bool visible) {
        SetApproximationSetsVisible(visible);
    };
    partModelPanel_->onSetStateChange = [this](int state) { ChangeSelectedSetState(state); };
    partModelPanel_->onMakePlate = [this] { CreatePlateFromSelectedPart(); };
    partModelPanel_->onFoldStateChanged = [this] { UpdatePartFoldPreview(); };
    partModelPanel_->onRealizeFoldState = [this] { RealizePartFoldState(); };
    partModelPanel_->onRailFoldEdited = [this](int railIndex, double value) {
        SetSelectedPartModelRailFold(railIndex, value);
    };
    partModelPanel_->onPickBoundariesFromWires = [this] {
        PickPartBoundariesFromSelectedWires();
    };
    partModelPanel_->onExportFoldMesh = [this](bool step) { ExportPartFoldMesh(step); };
    partModelPanel_->onExportFoldKcd = [this] { ExportPartFoldKcd(); };
    return partModelPanel_;
}

void MainWindow::CreatePartModelFromPanel()
{
    try {
        const std::string sourceObjectName = ToName(partModelPanel_->SelectedSourceName());
        const bool fromSurface = partModelPanel_->SelectedSourceIsSurface();
        if (sourceObjectName.empty()) {
            throw std::invalid_argument("近似する板材または面を選択してください。");
        }
        const PartApproximationOptions options = partModelPanel_->CurrentOptions();
        if (!options.automaticBoundaries && options.manualBoundaryParameters.empty()) {
            throw std::invalid_argument(
                "手動境界の位置(0～1)をカンマ区切りで入力するか、自動に切り替えてください。");
        }

        // 名前は 近似1, 近似2, ... の空き番号。
        std::string name;
        for (int suffix = 1;; ++suffix) {
            name = "近似" + std::to_string(suffix);
            const bool taken = std::any_of(
                project_.PartModels().begin(), project_.PartModels().end(),
                [&name](const kachakacha::model::NamedPartModel& model) {
                    return model.name == name;
                });
            if (!taken) {
                break;
            }
        }

        // 接続スコープ(合意13): 近似元と一緒に選ばれていたワイヤ・面は、
        // 近似の実形状へ接続できるよう自動変形した派生「_接続」の対象になる。
        std::vector<std::string> scopeWires;
        std::vector<std::string> scopeSurfaces;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                const auto& wire = project_.Wires()[selection.index];
                if (!wire.partModelSourceName.has_value()) {
                    scopeWires.push_back(wire.name);
                }
            } else if (selection.kind == CadSelectionKind::Surface && selection.index >= 0
                && selection.index < static_cast<int>(project_.Surfaces().size())) {
                const auto& surface = project_.Surfaces()[selection.index];
                if (!surface.partModelSourceName.has_value()
                    && !(fromSurface && surface.name == sourceObjectName)) {
                    scopeSurfaces.push_back(surface.name);
                }
            }
        }

        Project candidate = project_;
        if (fromSurface) {
            candidate.AddPartModelFromSurface(name, sourceObjectName, options);
        } else {
            candidate.AddPartModel(name, sourceObjectName, options);
        }
        if (!scopeWires.empty() || !scopeSurfaces.empty()) {
            candidate.SetPartModelConnectionScope(
                name, std::move(scopeWires), std::move(scopeSurfaces));
        }

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        const auto& model = project_.PartModels().back();
        const std::size_t adaptedCount
            = model.adaptedWireNames.size() + model.adaptedSurfaceNames.size();
        statusBar()->showMessage(
            QStringLiteral("部材近似モデル %1 を作成しました（部材 %2、最大偏差 %3 mm%4）")
                .arg(ToQString(name))
                .arg(model.result.parts.size())
                .arg(model.result.maximumDeviationMillimeters, 0, 'f', 3)
                .arg(adaptedCount > 0
                        ? QStringLiteral("、接続用に%1個を自動変形").arg(adaptedCount)
                        : QString()),
            6000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::CollectUnitMembersFromSelection()
{
    std::vector<PartModelPanel::UnitMember> members;
    for (const CadSelection& selection : viewport_->Selections()) {
        if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
            && selection.index < static_cast<int>(project_.Wires().size())) {
            const auto& wire = project_.Wires()[selection.index];
            if (!wire.partModelSourceName.has_value() && !wire.metadata.construction) {
                members.push_back({ToQString(wire.name), 0, 1});
            }
        } else if (selection.kind == CadSelectionKind::Surface && selection.index >= 0
            && selection.index < static_cast<int>(project_.Surfaces().size())) {
            const auto& surface = project_.Surfaces()[selection.index];
            if (!surface.partModelSourceName.has_value()) {
                members.push_back({ToQString(surface.name), 1, 0});
            }
        } else if (selection.kind == CadSelectionKind::Plate && selection.index >= 0
            && selection.index < static_cast<int>(project_.Plates().size())) {
            members.push_back({ToQString(project_.Plates()[selection.index].name), 2, 0});
        }
    }
    if (members.empty()) {
        statusBar()->showMessage(QStringLiteral(
            "3D画面で近似したい部品周辺の面・板材・ワイヤーを選んでから押してください"), 4500);
        return;
    }
    partModelPanel_->AddUnitMembers(members);
    statusBar()->showMessage(QStringLiteral(
        "%1件を表へ取り込みました。各行の役割（近似する / 形状維持 / 対象外）を確認してください")
            .arg(members.size()), 5000);
}

void MainWindow::CreateApproximationUnitFromPanel()
{
    try {
        const std::string unitName = ToName(partModelPanel_->UnitName());
        if (unitName.empty()) {
            throw std::invalid_argument("ユニット名を入力してください。");
        }
        for (const auto& set : project_.ObjectSets()) {
            if (set.name == unitName) {
                throw std::invalid_argument("同じ名前の部材グループがあります: " + unitName);
            }
        }
        const std::vector<PartModelPanel::UnitMember> members
            = partModelPanel_->UnitMembers();
        struct ApproxTarget {
            std::string name;
            bool isSurface = true;
        };
        std::vector<ApproxTarget> targets;
        std::vector<std::string> keepWires;
        std::vector<std::string> keepSurfaces;
        QStringList ignored;
        for (const auto& member : members) {
            const std::string memberName = ToName(member.name);
            if (member.role == 2) {
                continue;
            }
            if (member.role == 0) {
                if (member.kind == 1) {
                    targets.push_back({memberName, true});
                } else if (member.kind == 2) {
                    targets.push_back({memberName, false});
                } else {
                    ignored << member.name; // ワイヤは近似できない
                }
            } else { // 形状維持
                if (member.kind == 0) {
                    keepWires.push_back(memberName);
                } else if (member.kind == 1) {
                    keepSurfaces.push_back(memberName);
                } else {
                    ignored << QStringLiteral("%1(板材は形状維持にできません)").arg(member.name);
                }
            }
        }
        if (targets.empty()) {
            throw std::invalid_argument(
                "「近似する」役割の面または板材を表に1つ以上入れてください。");
        }
        const PartApproximationOptions options = partModelPanel_->CurrentOptions();

        Project candidate = project_;

        // #17a: 近似元の面上にある閉じた投影輪郭は、開口として自動登録してから近似する
        // (登録済み・開いた輪郭は対象外)。
        int autoOpenings = 0;
        for (const ApproxTarget& target : targets) {
            if (!target.isSurface) {
                continue;
            }
            const auto surface = std::find_if(
                candidate.Surfaces().begin(), candidate.Surfaces().end(),
                [&](const kachakacha::model::NamedSurface& entry) {
                    return entry.name == target.name;
                });
            if (surface == candidate.Surfaces().end()) {
                throw std::invalid_argument("面が見つかりません: " + target.name);
            }
            std::vector<std::string> openingCandidates;
            for (const auto& wire : candidate.Wires()) {
                if (wire.projection.has_value()
                    && wire.projection->targetSurfaceName == target.name
                    && wire.wire.IsClosed()
                    && std::find(surface->openingWireNames.begin(),
                           surface->openingWireNames.end(), wire.name)
                        == surface->openingWireNames.end()) {
                    openingCandidates.push_back(wire.name);
                }
            }
            for (const std::string& wireName : openingCandidates) {
                try {
                    candidate.AddSurfaceOpening(target.name, wireName);
                    ++autoOpenings;
                } catch (const std::exception&) {
                }
            }
        }

        // 近似モデルを面・板材ごとに作る。
        std::vector<std::string> modelNames;
        for (const ApproxTarget& target : targets) {
            std::string modelName = unitName + "_" + target.name;
            for (int suffix = 2;; ++suffix) {
                const bool taken = std::any_of(
                    candidate.PartModels().begin(), candidate.PartModels().end(),
                    [&](const NamedPartModel& model) { return model.name == modelName; });
                if (!taken) {
                    break;
                }
                modelName = unitName + "_" + target.name + "_" + std::to_string(suffix);
            }
            if (target.isSurface) {
                candidate.AddPartModelFromSurface(modelName, target.name, options);
            } else {
                candidate.AddPartModel(modelName, target.name, options);
            }
            modelNames.push_back(modelName);
        }

        // 形状維持(接続)対象を最寄りの近似モデルへ割り当てる(モデルごとに
        // 「〜_接続」の派生を作るため、対象は1つのモデルにだけ属させる)。
        const auto distanceToTarget = [&](const ApproxTarget& target,
                                          const std::vector<Vector3>& probes) {
            double best = std::numeric_limits<double>::infinity();
            const auto consider = [&](const Vector3& point) {
                for (const Vector3& probe : probes) {
                    best = std::min(best, (point - probe).Length());
                }
            };
            if (target.isSurface) {
                const auto surface = std::find_if(
                    candidate.Surfaces().begin(), candidate.Surfaces().end(),
                    [&](const kachakacha::model::NamedSurface& entry) {
                        return entry.name == target.name;
                    });
                if (surface == candidate.Surfaces().end()) {
                    return best;
                }
                for (int uIndex = 0; uIndex <= 8; ++uIndex) {
                    for (int vIndex = 0; vIndex <= 8; ++vIndex) {
                        consider(surface->surface.Evaluate(uIndex / 8.0, vIndex / 8.0));
                    }
                }
            } else {
                const auto plate = std::find_if(
                    candidate.Plates().begin(), candidate.Plates().end(),
                    [&](const kachakacha::model::NamedPlate& entry) {
                        return entry.name == target.name;
                    });
                if (plate == candidate.Plates().end()) {
                    return best;
                }
                for (int uIndex = 0; uIndex <= 8; ++uIndex) {
                    for (int vIndex = 0; vIndex <= 8; ++vIndex) {
                        consider(plate->plate.Evaluate(uIndex / 8.0, vIndex / 8.0, 0.5));
                    }
                }
            }
            return best;
        };
        const auto probesOfWire = [&](const std::string& wireName) {
            std::vector<Vector3> probes;
            for (const auto& wire : candidate.Wires()) {
                if (wire.name == wireName) {
                    for (int sample = 0; sample <= 8; ++sample) {
                        probes.push_back(wire.wire.Evaluate(sample / 8.0));
                    }
                }
            }
            return probes;
        };
        const auto probesOfSurface = [&](const std::string& surfaceName) {
            std::vector<Vector3> probes;
            for (const auto& surface : candidate.Surfaces()) {
                if (surface.name == surfaceName) {
                    for (int uIndex = 0; uIndex <= 3; ++uIndex) {
                        for (int vIndex = 0; vIndex <= 3; ++vIndex) {
                            probes.push_back(
                                surface.surface.Evaluate(uIndex / 3.0, vIndex / 3.0));
                        }
                    }
                }
            }
            return probes;
        };
        const auto nearestTargetIndex = [&](const std::vector<Vector3>& probes) {
            std::size_t best = 0;
            double bestDistance = std::numeric_limits<double>::infinity();
            for (std::size_t index = 0; index < targets.size(); ++index) {
                const double distance = distanceToTarget(targets[index], probes);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = index;
                }
            }
            return best;
        };
        std::vector<std::vector<std::string>> scopeWiresPerModel(targets.size());
        std::vector<std::vector<std::string>> scopeSurfacesPerModel(targets.size());
        for (const std::string& wireName : keepWires) {
            scopeWiresPerModel[nearestTargetIndex(probesOfWire(wireName))]
                .push_back(wireName);
        }
        for (const std::string& surfaceName : keepSurfaces) {
            scopeSurfacesPerModel[nearestTargetIndex(probesOfSurface(surfaceName))]
                .push_back(surfaceName);
        }
        std::size_t adaptedTotal = 0;
        for (std::size_t index = 0; index < modelNames.size(); ++index) {
            if (scopeWiresPerModel[index].empty() && scopeSurfacesPerModel[index].empty()) {
                continue;
            }
            candidate.SetPartModelConnectionScope(modelNames[index],
                scopeWiresPerModel[index], scopeSurfacesPerModel[index]);
            const auto model = std::find_if(
                candidate.PartModels().begin(), candidate.PartModels().end(),
                [&](const NamedPartModel& entry) { return entry.name == modelNames[index]; });
            if (model != candidate.PartModels().end()) {
                adaptedTotal += model->adaptedWireNames.size()
                    + model->adaptedSurfaceNames.size();
            }
        }

        // ユニットのグループを作り、各モデルの自動セットをその下へ入れる(#16)。
        candidate.CreateObjectSet(unitName);
        for (const std::string& modelName : modelNames) {
            candidate.SetObjectSetParent("近似:" + modelName, unitName);
        }

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        partModelPanel_->ClearUnitMembers();
        QString message = QStringLiteral(
            "ユニット %1: 近似モデル%2件を作成しました")
                .arg(ToQString(unitName)).arg(modelNames.size());
        if (adaptedTotal > 0) {
            message += QStringLiteral("、接続用に%1個を自動変形").arg(adaptedTotal);
        }
        if (autoOpenings > 0) {
            message += QStringLiteral("、開口%1件を自動で写しました").arg(autoOpenings);
        }
        if (!ignored.isEmpty()) {
            message += QStringLiteral("（対象外にした行: %1）")
                .arg(ignored.join(QStringLiteral("、")));
        }
        statusBar()->showMessage(message, 8000);

        if (partModelPanel_->UnitWantsNewKcd()) {
            // v1: ユニットを含むプロジェクト全体を新しい.kcdへ保存する
            // (ユニットだけに絞る場合は部材グループの書き出し除外と
            //  「出力対象のみで書き出し」を使う)。
            const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("ユニットを含むプロジェクトを保存"),
                ToQString(unitName) + QStringLiteral(".kcd"),
                QStringLiteral("kachakachaCAD (*.kcd)"));
            if (!path.isEmpty()) {
                const std::filesystem::path nativePath(path.toStdWString());
                std::ofstream output(nativePath, std::ios::binary);
                if (output) {
                    kachakacha::io::WriteProjectScript(output, project_);
                    statusBar()->showMessage(
                        QStringLiteral("ユニットを含むプロジェクトを保存しました: %1").arg(path),
                        5000);
                }
            }
        }
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 6000);
    }
}

void MainWindow::RecalculateSelectedPartModel()
{
    try {
        const std::string name = ToName(partModelPanel_->SelectedModelName());
        if (name.empty()) {
            throw std::invalid_argument("再計算する部材近似モデルを一覧で選択してください。");
        }
        Project candidate = project_;
        candidate.UpdatePartModelOptions(name, partModelPanel_->CurrentOptions());

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(
            QStringLiteral("部材近似モデル %1 を再計算しました").arg(ToQString(name)), 4000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::RemoveSelectedPartModel()
{
    try {
        const std::string name = ToName(partModelPanel_->SelectedModelName());
        if (name.empty()) {
            throw std::invalid_argument("削除する部材近似モデルを一覧で選択してください。");
        }
        Project candidate = project_;
        if (!candidate.RemovePartModel(name)) {
            throw std::invalid_argument("部材近似モデルが見つかりません: " + name);
        }

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(
            QStringLiteral("部材近似モデル %1 を削除しました").arg(ToQString(name)), 4000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::ExtractSelectedPartModelBoundaries()
{
    try {
        const std::string name = ToName(partModelPanel_->SelectedModelName());
        if (name.empty()) {
            throw std::invalid_argument("境界を抽出する部材近似モデルを一覧で選択してください。");
        }
        Project candidate = project_;
        const std::vector<std::string> created = candidate.ExtractPartModelBoundaries(name);
        if (created.empty()) {
            throw std::invalid_argument("抽出できる境界がありません（部材が1つだけです）。");
        }

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(
            QStringLiteral("境界 %1 本を独立したワイヤとして抽出しました（セット 抽出:%2）")
                .arg(created.size())
                .arg(ToQString(name)),
            5000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::ShowSelectedPartPatterns()
{
    try {
        const std::string name = ToName(partModelPanel_->SelectedModelName());
        if (name.empty()) {
            throw std::invalid_argument("型紙を表示する部材近似モデルを一覧で選択してください。");
        }
        const auto model = std::find_if(
            project_.PartModels().begin(), project_.PartModels().end(),
            [&name](const kachakacha::model::NamedPartModel& candidate) {
                return candidate.name == name;
            });
        if (model == project_.PartModels().end()) {
            throw std::invalid_argument("部材近似モデルが見つかりません: " + name);
        }

        std::vector<kachakacha::io::PartPatternResult> results;
        QStringList captions;
        const std::vector<int> selectedParts = partModelPanel_->SelectedPartNumbers();
        if (selectedParts.size() >= 2) {
            // 選択した隣接部材を1枚に結合した型紙。
            results.push_back(
                kachakacha::io::BuildPartPatternWithPreview(project_, *model, selectedParts));
            captions.push_back(
                QStringLiteral("部材%1-%2 を1枚に結合（境界は折り線）")
                    .arg(selectedParts.front())
                    .arg(selectedParts.back()));
        } else {
            results = kachakacha::io::BuildAllPartPatternsWithPreview(project_, *model);
            for (const auto& part : model->result.parts) {
                captions.push_back(
                    QStringLiteral("幅 %1 mm ・ 偏差 %2 mm")
                        .arg(part.widthMillimeters, 0, 'f', 1)
                        .arg(part.estimatedDeviationMillimeters, 0, 'f', 3));
            }
        }

        auto* dialog = new PartPatternViewDialog(
            QStringLiteral("型紙ビュー - %1").arg(ToQString(name)),
            std::move(results),
            std::move(captions),
            this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 6000);
    }
}

void MainWindow::SetApproximationSetsVisible(bool visible)
{
    try {
        Project candidate = project_;
        bool changed = false;
        for (const auto& set : candidate.ObjectSets()) {
            if (set.name.rfind("近似:", 0) == 0) {
                candidate.SetObjectSetState(
                    set.name,
                    visible ? ObjectSetState::Visible : ObjectSetState::Hidden);
                changed = true;
            }
        }
        if (!changed) {
            statusBar()->showMessage(
                QStringLiteral("部材近似モデルがまだありません"), 3000);
            return;
        }
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(
            visible ? QStringLiteral("重ね表示: 部材境界を表示しました")
                    : QStringLiteral("完成品のみ: 部材境界を隠しました"),
            3000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::ChangeSelectedSetState(int state)
{
    try {
        const std::string name = ToName(partModelPanel_->SelectedSetName());
        if (name.empty()) {
            throw std::invalid_argument("状態を変えるセットを一覧で選択してください。");
        }
        Project candidate = project_;
        candidate.SetObjectSetState(name, static_cast<ObjectSetState>(state));

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::CreatePlateFromSelectedPart()
{
    try {
        const std::string name = ToName(partModelPanel_->SelectedModelName());
        if (name.empty()) {
            throw std::invalid_argument("部材近似モデルを一覧で選択してください。");
        }
        const auto model = std::find_if(
            project_.PartModels().begin(), project_.PartModels().end(),
            [&name](const kachakacha::model::NamedPartModel& candidate) {
                return candidate.name == name;
            });
        if (model == project_.PartModels().end()) {
            throw std::invalid_argument("部材近似モデルが見つかりません: " + name);
        }
        std::vector<int> numbers = partModelPanel_->SelectedPartNumbers();
        if (numbers.empty()) {
            throw std::invalid_argument(
                "板材にする部材を一覧で選択してください（複数選択可）。");
        }
        const bool fromSurface = !model->sourceSurfaceName.empty();
        auto sourcePlate = project_.Plates().end();
        if (!fromSurface) {
            sourcePlate = std::find_if(
                project_.Plates().begin(), project_.Plates().end(),
                [&model](const kachakacha::model::NamedPlate& candidate) {
                    return candidate.name == model->sourcePlateName;
                });
            if (sourcePlate == project_.Plates().end()) {
                throw std::invalid_argument("元の板材が見つかりません。");
            }
        }

        // #18: 出力の種類と追従/固定。
        const bool outputPlate = partModelPanel_->PartOutputPlate();
        const bool outputSurface = partModelPanel_->PartOutputSurface();
        const bool outputWires = partModelPanel_->PartOutputWires();
        const bool follows = partModelPanel_->PartPlateFollows();
        if (!outputPlate && !outputSurface && !outputWires) {
            throw std::invalid_argument("出力（板材・面・縁ワイヤ）を1つ以上チェックしてください。");
        }

        Project candidate = project_;
        std::vector<std::string> created;
        int createdSurfaces = 0;
        int createdWires = 0;
        const auto freeWireName = [&candidate](const std::string& base) {
            std::string name = base;
            for (int suffix = 2; ; ++suffix) {
                const bool taken = std::any_of(
                    candidate.Wires().begin(), candidate.Wires().end(),
                    [&](const kachakacha::model::NamedWire& wire) {
                        return wire.name == name;
                    });
                if (!taken) {
                    return name;
                }
                name = base + std::to_string(suffix);
            }
        };
        for (const int number : numbers) {
            if (number < 1 || number > static_cast<int>(model->partSurfaceNames.size())) {
                continue;
            }
            const std::string surfaceName = model->partSurfaceNames[number - 1];

            // 固定(独立)出力・面/ワイヤ出力用: 部材のレール2本を独立ワイヤへ複製。
            std::string bottomCopyName;
            std::string topCopyName;
            std::string independentSurfaceName;
            const bool needsCopies = outputWires || outputSurface || (outputPlate && !follows);
            if (needsCopies) {
                const auto railWire = [&](const std::string& railName)
                    -> const kachakacha::model::NamedWire* {
                    for (const auto& wire : candidate.Wires()) {
                        if (wire.name == railName) {
                            return &wire;
                        }
                    }
                    return nullptr;
                };
                const auto* bottom = railWire(model->boundaryWireNames[number - 1]);
                const auto* top = railWire(model->boundaryWireNames[number]);
                if (bottom == nullptr || top == nullptr) {
                    continue;
                }
                bottomCopyName = freeWireName(surfaceName + "_縁1");
                const kachakacha::model::Wire bottomGeometry = bottom->wire;
                const kachakacha::model::Wire topGeometry = top->wire;
                candidate.AddWire(bottomCopyName, bottomGeometry);
                topCopyName = freeWireName(surfaceName + "_縁2");
                candidate.AddWire(topCopyName, topGeometry);
                createdWires += 2;
                if (outputSurface || (outputPlate && !follows)) {
                    independentSurfaceName = surfaceName + "_独立面";
                    for (int suffix = 2;
                         candidate.FindSurface(independentSurfaceName).has_value(); ++suffix) {
                        independentSurfaceName
                            = surfaceName + "_独立面" + std::to_string(suffix);
                    }
                    candidate.AddRuledSurface(
                        independentSurfaceName, bottomCopyName, topCopyName);
                    ++createdSurfaces;
                }
                if (!outputWires) {
                    candidate.SetWireVisible(bottomCopyName, false);
                    candidate.SetWireVisible(topCopyName, false);
                }
            }

            if (!outputPlate) {
                continue;
            }
            // 板材: 追従=派生の部材面から / 固定=独立コピーの面から。
            const std::string plateSourceName = follows ? surfaceName : independentSurfaceName;
            std::string plateName;
            for (int suffix = 0;; ++suffix) {
                plateName = surfaceName + "板"
                    + (suffix == 0 ? std::string() : std::to_string(suffix + 1));
                if (!candidate.FindPlate(plateName).has_value()) {
                    break;
                }
            }
            if (fromSurface) {
                candidate.AddPlate(
                    plateName,
                    plateSourceName,
                    partModelPanel_->FoldThicknessMillimeters(),
                    kachakacha::model::PlateThicknessDirection::Centered,
                    "未指定");
            } else {
                candidate.AddPlate(
                    plateName,
                    plateSourceName,
                    sourcePlate->plate.Thickness(),
                    sourcePlate->plate.Direction(),
                    sourcePlate->material);
            }
            // この部材に収まる開口(窓・ライト等)の派生輪郭を、そのまま穴として付ける。
            const std::string openingPrefix
                = model->name + "_部材" + std::to_string(number) + "_穴";
            for (const std::string& openingName : model->openingWireNames) {
                if (openingName.rfind(openingPrefix, 0) != 0) {
                    continue;
                }
                try {
                    candidate.AddPlateOpening(plateName, openingName);
                } catch (const std::exception&) {
                    // 開いた輪郭など穴にできない場合は輪郭表示のみとする。
                }
            }
            created.push_back(plateName);
        }
        if (created.empty() && createdSurfaces == 0 && createdWires == 0) {
            throw std::invalid_argument("出力できる部材がありません。");
        }

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        QStringList parts;
        if (!created.empty()) {
            parts << QStringLiteral("板材%1枚(%2)")
                .arg(created.size())
                .arg(follows ? QStringLiteral("近似に追従") : QStringLiteral("固定"));
        }
        if (createdSurfaces > 0) {
            parts << QStringLiteral("独立面%1枚").arg(createdSurfaces);
        }
        if (createdWires > 0 && outputWires) {
            parts << QStringLiteral("縁ワイヤ%1本").arg(createdWires);
        }
        statusBar()->showMessage(
            QStringLiteral("部材から %1 を作成しました")
                .arg(parts.join(QStringLiteral("、"))),
            5000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::PickPartBoundariesFromSelectedWires()
{
    try {
        // 近似元(板材 or 面)の上で、選択ワイヤーが分割軸方向のどの位置にあるかを測り、
        // 手動境界パラメータ(0..1)として設定する(仕様: 境界は自動+手動、段階B)。
        const std::string sourceObjectName = ToName(partModelPanel_->SelectedSourceName());
        const bool fromSurface = partModelPanel_->SelectedSourceIsSurface();
        if (sourceObjectName.empty()) {
            throw std::invalid_argument("先に近似元の板材または面を選んでください。");
        }
        std::vector<const kachakacha::model::NamedWire*> selectedWires;
        for (const CadSelection& selection : viewport_->Selections()) {
            if (selection.kind == CadSelectionKind::Wire && selection.index >= 0
                && selection.index < static_cast<int>(project_.Wires().size())) {
                selectedWires.push_back(&project_.Wires()[selection.index]);
            }
        }
        if (selectedWires.empty()) {
            throw std::invalid_argument(
                "境界にするワイヤーを3D画面で選んでから押してください。");
        }
        // 近似元のサンプラを用意する。
        const kachakacha::model::Surface* sourceSurface = nullptr;
        const kachakacha::model::Plate* sourcePlate = nullptr;
        if (fromSurface) {
            for (const auto& surface : project_.Surfaces()) {
                if (surface.name == sourceObjectName) {
                    sourceSurface = &surface.surface;
                    break;
                }
            }
        } else {
            for (const auto& plate : project_.Plates()) {
                if (plate.name == sourceObjectName) {
                    sourcePlate = &plate.plate;
                    break;
                }
            }
        }
        if (sourceSurface == nullptr && sourcePlate == nullptr) {
            throw std::invalid_argument("近似元が見つかりません: " + sourceObjectName);
        }
        const kachakacha::model::PartSource source = sourceSurface != nullptr
            ? kachakacha::model::PartSource(*sourceSurface)
            : kachakacha::model::PartSource(*sourcePlate);
        const bool splitAlongV = partModelPanel_->CurrentOptions().splitAxis
            == kachakacha::model::PartSplitAxis::V;

        // 近似元を格子サンプリングしておき、各ワイヤー点の最近傍から
        // 分割軸パラメータを求める(平均)。
        constexpr int kAxisSamples = 96;
        constexpr int kCrossSamples = 17;
        std::vector<std::vector<kachakacha::geometry::Vector3>> grid(kAxisSamples + 1);
        for (int axisIndex = 0; axisIndex <= kAxisSamples; ++axisIndex) {
            grid[axisIndex].reserve(kCrossSamples);
            const double t = static_cast<double>(axisIndex) / kAxisSamples;
            for (int crossIndex = 0; crossIndex < kCrossSamples; ++crossIndex) {
                const double sParameter
                    = static_cast<double>(crossIndex) / (kCrossSamples - 1);
                const double u = splitAlongV ? sParameter : t;
                const double v = splitAlongV ? t : sParameter;
                grid[axisIndex].push_back(source.Evaluate(u, v));
            }
        }
        std::vector<double> parameters;
        double worstDistance = 0.0;
        for (const auto* wire : selectedWires) {
            double parameterSum = 0.0;
            int sampleCount = 0;
            double wireWorst = 0.0;
            constexpr int kWireSamples = 9;
            for (int sample = 0; sample <= kWireSamples; ++sample) {
                const kachakacha::geometry::Vector3 point
                    = wire->wire.Evaluate(static_cast<double>(sample) / kWireSamples);
                double best = std::numeric_limits<double>::max();
                int bestAxis = 0;
                for (int axisIndex = 0; axisIndex <= kAxisSamples; ++axisIndex) {
                    for (int crossIndex = 0; crossIndex < kCrossSamples; ++crossIndex) {
                        const double distance
                            = (grid[axisIndex][crossIndex] - point).Length();
                        if (distance < best) {
                            best = distance;
                            bestAxis = axisIndex;
                        }
                    }
                }
                parameterSum += static_cast<double>(bestAxis) / kAxisSamples;
                wireWorst = std::max(wireWorst, best);
                ++sampleCount;
            }
            worstDistance = std::max(worstDistance, wireWorst);
            const double parameter = parameterSum / sampleCount;
            if (parameter > 1.0e-3 && parameter < 1.0 - 1.0e-3) {
                parameters.push_back(parameter);
            }
        }
        std::sort(parameters.begin(), parameters.end());
        parameters.erase(std::unique(parameters.begin(), parameters.end(),
            [](double a, double b) { return std::abs(a - b) < 2.0e-3; }),
            parameters.end());
        if (parameters.empty()) {
            throw std::invalid_argument(
                "選択ワイヤーから境界位置を求められませんでした(端すぎるか、近似元から離れています)。");
        }
        partModelPanel_->SetManualBoundaryParameters(parameters);
        statusBar()->showMessage(
            QStringLiteral("境界を%1本分設定しました（近似元からの最大距離 %2 mm）。「部材近似モデルを作成」で反映されます")
                .arg(parameters.size())
                .arg(worstDistance, 0, 'f', 2),
            6000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

// ---- 曲げ確認と出力 -------------------------------------------------------

void MainWindow::SetSelectedPartModelRailFold(int railIndex, double value)
{
    try {
        const std::string name = ToName(partModelPanel_->SelectedModelName());
        const NamedPartModel* model = name.empty()
            ? nullptr
            : FindPartModel(project_, name);
        if (model == nullptr || model->result.parts.size() < 2) {
            return;
        }
        std::vector<double> progress = model->railFoldProgress;
        if (progress.empty()) {
            progress.assign(model->result.parts.size() - 1, 1.0);
        }
        if (railIndex < 0 || railIndex >= static_cast<int>(progress.size())) {
            return;
        }
        if (std::abs(progress[railIndex] - value) <= 1.0e-12) {
            return;
        }
        progress[railIndex] = std::clamp(value, -4.0, 4.0);
        Project candidate = project_;
        candidate.SetPartModelRailFoldProgress(name, std::move(progress));
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        UpdatePartFoldPreview();
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::UpdatePartFoldPreview()
{
    if (viewport_ == nullptr || partModelPanel_ == nullptr) {
        return;
    }
    try {
        const std::string name = ToName(partModelPanel_->SelectedModelName());
        const NamedPartModel* model = name.empty()
            ? nullptr
            : FindPartModel(project_, name);
        if (model == nullptr) {
            partModelPanel_->SetFoldLines(QString(), {}, {});
        }
        if (!partModelPanel_->FoldPreviewEnabled() || model == nullptr) {
            viewport_->SetPartFoldPreview({}, {});
            return;
        }
        const kachakacha::model::Surface* sourceSurface = nullptr;
        const kachakacha::model::Plate* sourcePlate = nullptr;
        if (!model->sourceSurfaceName.empty()) {
            for (const auto& surface : project_.Surfaces()) {
                if (surface.name == model->sourceSurfaceName) {
                    sourceSurface = &surface.surface;
                    break;
                }
            }
        } else {
            for (const auto& plate : project_.Plates()) {
                if (plate.name == model->sourcePlateName) {
                    sourcePlate = &plate.plate;
                    break;
                }
            }
        }
        if (sourceSurface == nullptr && sourcePlate == nullptr) {
            viewport_->SetPartFoldPreview({}, {});
            return;
        }
        std::vector<double> parameters;
        parameters.push_back(0.0);
        for (std::size_t index = 1; index < model->result.parts.size(); ++index) {
            parameters.push_back(model->result.parts[index].minimumParameter);
        }
        parameters.push_back(1.0);
        const kachakacha::model::PartSource source = sourceSurface != nullptr
            ? kachakacha::model::PartSource(*sourceSurface)
            : kachakacha::model::PartSource(*sourcePlate);
        const auto mesh = kachakacha::model::DevelopPartMesh(
            source, model->options.splitAxis, parameters, 64);
        // 可動折り線(合意10): 折り線ごとの角度⇄%行を更新し、その折り状態を目標に
        // 全体スライダーで「型紙⇄折り状態」を補間する。
        const std::vector<double> creaseAngles
            = kachakacha::model::MeasureCreaseAngles(mesh);
        std::vector<double> angleDegrees(creaseAngles.size(), 0.0);
        for (std::size_t index = 0; index < creaseAngles.size(); ++index) {
            angleDegrees[index] = creaseAngles[index] * 180.0 / 3.14159265358979323846;
        }
        partModelPanel_->SetFoldLines(
            ToQString(model->name), angleDegrees, model->railFoldProgress);
        // 曲げ確認(オーナー指示の見え方): 0%=各部材の展開形(型紙と同じ)を
        // 外向きへ離した位置に並べ、100%で「折り線ごとの角度どおりの剛体折り状態
        // (近似モデルの位置)」へ収束する。スライダーは組立アニメーション専用。
        std::vector<double> individual(creaseAngles.size(), 1.0);
        for (std::size_t index = 0;
             index < individual.size() && index < model->railFoldProgress.size(); ++index) {
            individual[index] = model->railFoldProgress[index];
        }
        // 展開を離す距離: モデルの大きさから決める。
        kachakacha::geometry::Vector3 low = mesh.world.front().front();
        kachakacha::geometry::Vector3 high = low;
        for (const auto& rowPoints : mesh.world) {
            for (const auto& point : rowPoints) {
                low = {std::min(low.x, point.x), std::min(low.y, point.y),
                    std::min(low.z, point.z)};
                high = {std::max(high.x, point.x), std::max(high.y, point.y),
                    std::max(high.z, point.z)};
            }
        }
        const double liftDistance = std::max(25.0, (high - low).Length() * 0.35);
        auto bandRails = kachakacha::model::BuildBandFoldAnimationRails(
            mesh, individual, partModelPanel_->FoldProgress(), liftDistance);
        // 選んでいる部材だけを表示する(複数選択なら複数、未選択なら全部)。
        std::vector<int> visibleBands;
        for (const int number : partModelPanel_->SelectedPartNumbers()) {
            visibleBands.push_back(number - 1);
        }
        viewport_->SetPartFoldPreview(
            std::move(bandRails), mesh.creaseDirections, std::move(visibleBands), true);
    } catch (const std::exception& error) {
        viewport_->SetPartFoldPreview({}, {});
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

namespace {

//! 曲げ状態の名前プレフィックス(プロジェクト内で未使用のもの)を作る。
[[nodiscard]] std::string MakeFoldStatePrefix(
    const Project& project, const std::string& modelName, double progress)
{
    const int percent = static_cast<int>(progress * 100.0 + 0.5);
    const std::string base = modelName + "_曲げ" + std::to_string(percent);
    for (int suffix = 0;; ++suffix) {
        const std::string candidate
            = suffix == 0 ? base : base + "_" + std::to_string(suffix + 1);
        const std::string probe = candidate + "_レール1";
        const bool taken = std::any_of(
            project.Wires().begin(), project.Wires().end(),
            [&probe](const kachakacha::model::NamedWire& wire) {
                return wire.name == probe;
            });
        if (!taken) {
            return candidate;
        }
    }
}

} // namespace

void MainWindow::RealizePartFoldState()
{
    try {
        const std::string name = ToName(partModelPanel_->SelectedModelName());
        if (name.empty()) {
            throw std::invalid_argument("部材近似モデルを一覧で選択してください。");
        }
        const NamedPartModel* model = FindPartModel(project_, name);
        if (model == nullptr) {
            throw std::invalid_argument("部材近似モデルが見つかりません: " + name);
        }
        kachakacha::io::PartFoldStateOptions options;
        // スライダーは組立アニメーション。板材化・出力は 0%=型紙の平面配置、
        // それ以外=折り線ごとの角度どおりの折り状態(帯剛体)を使う。
        options.progress = partModelPanel_->FoldProgress() <= 1.0e-9 ? 0.0 : 1.0;
        options.partNumbers = partModelPanel_->SelectedPartNumbers();
        options.surfaceThicknessMillimeters = partModelPanel_->FoldThicknessMillimeters();
        const std::string prefix
            = MakeFoldStatePrefix(project_, model->name, options.progress);

        Project candidate = project_;
        const auto result = kachakacha::io::AddPartFoldStateModel(
            candidate, candidate, *model, options, prefix);
        // 実体化した曲げ状態は「近似:<モデル名>」配下の子グループへまとめる
        // (docs/surface-unfolding-spec.md 合意11)。
        try {
            using kachakacha::model::ProjectObjectKind;
            candidate.CreateObjectSet(prefix);
            candidate.SetObjectSetParent(prefix, "近似:" + model->name);
            for (const std::string& wireName : result.railWireNames) {
                candidate.AssignObjectToSet(ProjectObjectKind::Wire, wireName, prefix);
            }
            for (const std::string& surfaceName : result.surfaceNames) {
                candidate.AssignObjectToSet(ProjectObjectKind::Surface, surfaceName, prefix);
            }
            for (const std::string& plateName : result.plateNames) {
                candidate.AssignObjectToSet(ProjectObjectKind::Plate, plateName, prefix);
            }
            for (const std::string& wireName : result.openingWireNames) {
                candidate.AssignObjectToSet(ProjectObjectKind::Wire, wireName, prefix);
            }
            for (const std::string& wireName : result.outlineWireNames) {
                candidate.AssignObjectToSet(ProjectObjectKind::Wire, wireName, prefix);
            }
        } catch (const std::exception&) {
            // グループ化に失敗しても実体化自体は成立させる。
        }
        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(
            QStringLiteral("曲げ状態 %1 を板材化しました（板 %2 枚、穴 %3 件%4）")
                .arg(ToQString(prefix))
                .arg(result.plateNames.size())
                .arg(result.openingWireNames.size())
                .arg(result.outlineWireNames.empty()
                        ? QString()
                        : QStringLiteral("、境界またぎで輪郭のみ %1 件")
                              .arg(result.outlineWireNames.size())),
            6000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::ExportPartFoldMesh(bool step)
{
    try {
        const std::string name = ToName(partModelPanel_->SelectedModelName());
        if (name.empty()) {
            throw std::invalid_argument("部材近似モデルを一覧で選択してください。");
        }
        const NamedPartModel* model = FindPartModel(project_, name);
        if (model == nullptr) {
            throw std::invalid_argument("部材近似モデルが見つかりません: " + name);
        }
        kachakacha::io::PartFoldStateOptions options;
        // スライダーは組立アニメーション。板材化・出力は 0%=型紙の平面配置、
        // それ以外=折り線ごとの角度どおりの折り状態(帯剛体)を使う。
        options.progress = partModelPanel_->FoldProgress() <= 1.0e-9 ? 0.0 : 1.0;
        options.partNumbers = partModelPanel_->SelectedPartNumbers();
        options.surfaceThicknessMillimeters = partModelPanel_->FoldThicknessMillimeters();

        Project exportProject;
        const auto result = kachakacha::io::AddPartFoldStateModel(
            exportProject, project_, *model, options, "出力");

        const int percent = static_cast<int>(options.progress * 100.0 + 0.5);
        const QString extension = step ? QStringLiteral(".step") : QStringLiteral(".stl");
        const QString filter = step
            ? QStringLiteral("STEP CAD形状 (*.step *.stp)")
            : QStringLiteral("STL 3Dプリント形状 (*.stl)");
        QString suggestedDirectory;
        if (!currentPath_.isEmpty()) {
            suggestedDirectory = QFileInfo(currentPath_).absolutePath() + QLatin1Char('/');
        }
        QString path = QFileDialog::getSaveFileName(
            this,
            step ? QStringLiteral("曲げ状態をSTEP保存") : QStringLiteral("曲げ状態をSTL保存"),
            suggestedDirectory
                + QStringLiteral("%1_曲げ%2%3").arg(ToQString(model->name)).arg(percent).arg(extension),
            filter);
        if (path.isEmpty()) {
            return;
        }
        if (!path.endsWith(extension, Qt::CaseInsensitive)
            && !(step && path.endsWith(QStringLiteral(".stp"), Qt::CaseInsensitive))) {
            path += extension;
        }
        kachakacha::occt::ModelShapeSelection selection;
        selection.plateNames = result.plateNames;
        const std::filesystem::path nativePath(path.toStdWString());
        if (step) {
            kachakacha::occt::WriteModelStep(nativePath, exportProject, selection);
        } else {
            kachakacha::occt::WriteModelStl(nativePath, exportProject, selection);
        }
        statusBar()->showMessage(
            QStringLiteral("曲げ%1%の形状を保存しました: %2（板 %3 枚、穴 %4 件）")
                .arg(percent)
                .arg(path)
                .arg(result.plateNames.size())
                .arg(result.openingWireNames.size()),
            6000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}

void MainWindow::ExportPartFoldKcd()
{
    try {
        const std::string name = ToName(partModelPanel_->SelectedModelName());
        if (name.empty()) {
            throw std::invalid_argument("部材近似モデルを一覧で選択してください。");
        }
        const NamedPartModel* model = FindPartModel(project_, name);
        if (model == nullptr) {
            throw std::invalid_argument("部材近似モデルが見つかりません: " + name);
        }
        kachakacha::io::PartFoldStateOptions options;
        // スライダーは組立アニメーション。板材化・出力は 0%=型紙の平面配置、
        // それ以外=折り線ごとの角度どおりの折り状態(帯剛体)を使う。
        options.progress = partModelPanel_->FoldProgress() <= 1.0e-9 ? 0.0 : 1.0;
        options.partNumbers = partModelPanel_->SelectedPartNumbers();
        options.surfaceThicknessMillimeters = partModelPanel_->FoldThicknessMillimeters();

        Project exportProject;
        const auto result = kachakacha::io::AddPartFoldStateModel(
            exportProject, project_, *model, options, ToName(ToQString(model->name)));

        const int percent = static_cast<int>(options.progress * 100.0 + 0.5);
        QString suggestedDirectory;
        if (!currentPath_.isEmpty()) {
            suggestedDirectory = QFileInfo(currentPath_).absolutePath() + QLatin1Char('/');
        }
        QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("曲げ状態を別のプロジェクトへ保存"),
            suggestedDirectory
                + QStringLiteral("%1_曲げ%2.kcd").arg(ToQString(model->name)).arg(percent),
            QStringLiteral("kachakachaCAD (*.kcd)"));
        if (path.isEmpty()) {
            return;
        }
        if (!path.endsWith(QStringLiteral(".kcd"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".kcd");
        }
        const std::filesystem::path nativePath(path.toStdWString());
        std::ofstream output(nativePath, std::ios::binary);
        if (!output) {
            throw std::runtime_error("出力ファイルを開けませんでした。");
        }
        kachakacha::io::WriteProjectScript(output, exportProject);
        statusBar()->showMessage(
            QStringLiteral("曲げ%1%の状態を書き出しました: %2（板 %3 枚）")
                .arg(percent)
                .arg(path)
                .arg(result.plateNames.size()),
            6000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
    }
}
