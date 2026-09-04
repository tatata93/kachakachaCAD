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
#include <stdexcept>
#include <string>
#include <vector>

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

        Project candidate = project_;
        if (fromSurface) {
            candidate.AddPartModelFromSurface(name, sourceObjectName, options);
        } else {
            candidate.AddPartModel(name, sourceObjectName, options);
        }

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        const auto& model = project_.PartModels().back();
        statusBar()->showMessage(
            QStringLiteral("部材近似モデル %1 を作成しました（部材 %2、最大偏差 %3 mm）")
                .arg(ToQString(name))
                .arg(model.result.parts.size())
                .arg(model.result.maximumDeviationMillimeters, 0, 'f', 3),
            5000);
    } catch (const std::exception& error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 5000);
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

        Project candidate = project_;
        std::vector<std::string> created;
        for (const int number : numbers) {
            if (number < 1 || number > static_cast<int>(model->partSurfaceNames.size())) {
                continue;
            }
            const std::string surfaceName = model->partSurfaceNames[number - 1];
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
                    surfaceName,
                    partModelPanel_->FoldThicknessMillimeters(),
                    kachakacha::model::PlateThicknessDirection::Centered,
                    "未指定");
            } else {
                candidate.AddPlate(
                    plateName,
                    surfaceName,
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
        if (created.empty()) {
            throw std::invalid_argument("板材にできる部材がありません。");
        }

        RecordUndo();
        project_ = std::move(candidate);
        MarkModified();
        RefreshModelViews(false);
        statusBar()->showMessage(
            QStringLiteral("部材から板材を %1 枚作成しました（%2）")
                .arg(created.size())
                .arg(ToQString(created.front())),
            5000);
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
        const bool customFold = std::any_of(
            model->railFoldProgress.begin(), model->railFoldProgress.end(),
            [](double value) { return std::abs(value - 1.0) > 1.0e-12; });
        auto state = customFold
            ? kachakacha::model::BuildFoldPreviewToState(
                mesh,
                kachakacha::model::BuildPerCreaseFoldState(mesh, model->railFoldProgress),
                partModelPanel_->FoldProgress())
            : kachakacha::model::BuildFoldPreview(
                mesh, partModelPanel_->FoldProgress());
        viewport_->SetPartFoldPreview(std::move(state), mesh.creaseDirections);
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
        options.progress = partModelPanel_->FoldProgress();
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
        options.progress = partModelPanel_->FoldProgress();
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
        options.progress = partModelPanel_->FoldProgress();
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
