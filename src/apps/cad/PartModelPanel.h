#pragma once

#include "kachakacha/model/PartModel.h"
#include "kachakacha/model/Project.h"

#include <QWidget>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSlider;
class QSpinBox;
class QTreeWidget;

//! 部材タブ(docs/surface-unfolding-spec.md)。部材近似モデルの作成・一覧・
//! セット状態の操作UI。プロジェクトの変更は行わず、シグナルで依頼するだけ。
//! moc を使わない方針(既存コードに合わせる)のため、通知は std::function で行う。
class PartModelPanel : public QWidget {
public:
    explicit PartModelPanel(QWidget* parent = nullptr);

    void RefreshFromProject(const kachakacha::model::Project& project);

    [[nodiscard]] QString SelectedPlateName() const;
    [[nodiscard]] kachakacha::model::PartApproximationOptions CurrentOptions() const;
    [[nodiscard]] QString SelectedModelName() const;
    [[nodiscard]] std::vector<int> SelectedPartNumbers() const;
    [[nodiscard]] QString SelectedSetName() const;
    [[nodiscard]] double FoldProgress() const;      //!< 0(平面)〜1(近似完成形)
    [[nodiscard]] bool FoldPreviewEnabled() const;  //!< 3Dビューで曲げ状態を表示するか

    std::function<void()> onCreate;
    std::function<void()> onRecalculate;
    std::function<void()> onRemove;
    std::function<void()> onExtract;
    std::function<void()> onShowPatterns;
    std::function<void(bool)> onOverlayVisibility;
    std::function<void(int)> onSetStateChange; //!< kachakacha::model::ObjectSetState の値
    std::function<void()> onMakePlate;
    std::function<void()> onFoldStateChanged;  //!< スライダー・チェック・選択の変化
    std::function<void()> onRealizeFoldState;  //!< この曲げ状態を同じプロジェクトへ板材化
    std::function<void(bool)> onExportFoldMesh; //!< 曲げ状態を保存(true=STEP, false=STL)
    std::function<void()> onExportFoldKcd;     //!< 曲げ状態を別の.kcdへ保存

private:
    QComboBox* plateCombo_ = nullptr;
    QComboBox* axisCombo_ = nullptr;
    QCheckBox* automaticCheck_ = nullptr;
    QDoubleSpinBox* deviationSpin_ = nullptr;
    QSpinBox* maxCountSpin_ = nullptr;
    QDoubleSpinBox* minWidthSpin_ = nullptr;
    QLineEdit* manualBoundariesEdit_ = nullptr;
    QTreeWidget* modelTree_ = nullptr;
    QListWidget* setList_ = nullptr;
    QCheckBox* foldPreviewCheck_ = nullptr;
    QSlider* foldSlider_ = nullptr;
    QLabel* foldLabel_ = nullptr;
};
