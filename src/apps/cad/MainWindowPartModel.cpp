// 部材タブ(部材近似モデル)の配線。UI部品は PartModelPanel、型紙表示は
// PartPatternViewDialog に分離し、ここではプロジェクト操作だけを行う
// (ADR 0018: MainWindow.cpp を太らせない)。

#include "MainWindow.h"
#include "PartModelPanel.h"
#include "PartPatternViewDialog.h"

#include "kachakacha/io/PartPatterns.h"

#include <QStatusBar>
#include <QStringList>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

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
    return partModelPanel_;
}

void MainWindow::CreatePartModelFromPanel()
{
    try {
        const std::string plateName = ToName(partModelPanel_->SelectedPlateName());
        if (plateName.empty()) {
            throw std::invalid_argument("近似する板材を選択してください。");
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
        candidate.AddPartModel(name, plateName, options);

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
        const auto sourcePlate = std::find_if(
            project_.Plates().begin(), project_.Plates().end(),
            [&model](const kachakacha::model::NamedPlate& candidate) {
                return candidate.name == model->sourcePlateName;
            });
        if (sourcePlate == project_.Plates().end()) {
            throw std::invalid_argument("元の板材が見つかりません。");
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
            candidate.AddPlate(
                plateName,
                surfaceName,
                sourcePlate->plate.Thickness(),
                sourcePlate->plate.Direction(),
                sourcePlate->material);
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
