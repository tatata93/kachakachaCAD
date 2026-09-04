#pragma once

#include "kachakacha/model/PartModel.h"
#include "kachakacha/model/Project.h"

#include <QWidget>

#include <array>
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
class QTableWidget;
class QTreeWidget;
class QVBoxLayout;

//! 部材タブ(docs/surface-unfolding-spec.md)。部材近似モデルの作成・一覧・
//! セット状態の操作UI。プロジェクトの変更は行わず、シグナルで依頼するだけ。
//! moc を使わない方針(既存コードに合わせる)のため、通知は std::function で行う。
class PartModelPanel : public QWidget {
public:
    explicit PartModelPanel(QWidget* parent = nullptr);

    void RefreshFromProject(const kachakacha::model::Project& project);
    //! 上部ツールで選んだセクションだけ表示する(0=作成 1=一覧・型紙 2=曲げ 3=セット、負=全部)。
    void SetVisibleSection(int index);
    //! 板材コンボを指定名に合わせる(ビューポートの右クリックメニューから)。
    void SelectPlate(const QString& plateName);

    [[nodiscard]] QString SelectedPlateName() const;
    //! 近似元コンボで選ばれた対象の名前(板材名または面名)。
    [[nodiscard]] QString SelectedSourceName() const;
    //! 近似元コンボで面が選ばれているか。
    [[nodiscard]] bool SelectedSourceIsSurface() const;
    //! 面入力の近似モデルを板材化するときの板厚(mm)。
    [[nodiscard]] double FoldThicknessMillimeters() const;
    [[nodiscard]] kachakacha::model::PartApproximationOptions CurrentOptions() const;
    [[nodiscard]] QString SelectedModelName() const;
    [[nodiscard]] std::vector<int> SelectedPartNumbers() const;
    [[nodiscard]] QString SelectedSetName() const;
    [[nodiscard]] double FoldProgress() const;      //!< 0(平面)〜1(近似完成形)
    [[nodiscard]] bool FoldPreviewEnabled() const;  //!< 3Dビューで曲げ状態を表示するか

    //! 近似ユニット(#15): 取り込んだ対象と役割の表。
    struct UnitMember {
        QString name;
        int kind = 0; //!< 0=ワイヤ 1=面 2=板材
        int role = 0; //!< 0=近似する 1=形状維持(接続) 2=対象外
    };
    void AddUnitMembers(const std::vector<UnitMember>& members);
    void ClearUnitMembers();
    void SetUnitName(const QString& name);
    [[nodiscard]] std::vector<UnitMember> UnitMembers() const { return unitMembers_; }
    [[nodiscard]] QString UnitName() const;
    [[nodiscard]] bool UnitWantsNewKcd() const;
    //! 板材化の出力(#18)。
    [[nodiscard]] bool PartOutputPlate() const;
    [[nodiscard]] bool PartOutputSurface() const;
    [[nodiscard]] bool PartOutputWires() const;
    [[nodiscard]] bool PartPlateFollows() const; //!< true=近似に追従(派生) false=固定(独立)

    //! 可動折り線の行(角度⇄%の相互編集)を選択中モデルに合わせて作り直す・更新する。
    //! fullAngleDegrees は完成形での各折り線の折り角(度)、progress は現在の進行度。
    //! 手動境界欄へパラメータ列を書き込み、自動分割のチェックを外す。
    void SetManualBoundaryParameters(const std::vector<double>& parameters);

    void SetFoldLines(
        const QString& modelName,
        const std::vector<double>& fullAngleDegrees,
        const std::vector<double>& progress);

    std::function<void()> onCreate;
    std::function<void()> onCollectUnitMembers; //!< 3D選択をユニット表へ取り込む(#15)
    std::function<void()> onCreateUnit;         //!< 表の役割どおりにユニットを近似(#15)
    std::function<void()> onPickBoundariesFromWires; //!< 選択ワイヤーから手動境界を求める
    std::function<void()> onRecalculate;
    std::function<void()> onRemove;
    std::function<void()> onExtract;
    std::function<void()> onShowPatterns;
    std::function<void(bool)> onOverlayVisibility;
    std::function<void(int)> onSetStateChange; //!< kachakacha::model::ObjectSetState の値
    std::function<void()> onMakePlate;
    std::function<void()> onFoldStateChanged;  //!< スライダー・チェック・選択の変化
    std::function<void()> onRealizeFoldState;  //!< この曲げ状態を同じプロジェクトへ板材化
    std::function<void(int, double)> onRailFoldEdited; //!< 折り線index(0始まり)と新しい進行度
    std::function<void(bool)> onExportFoldMesh; //!< 曲げ状態を保存(true=STEP, false=STL)
    std::function<void()> onExportFoldKcd;     //!< 曲げ状態を別の.kcdへ保存

private:
    void RefreshUnitTable();

    QLineEdit* unitNameEdit_ = nullptr;
    QTableWidget* unitTable_ = nullptr;
    QComboBox* unitOutputCombo_ = nullptr;
    std::vector<UnitMember> unitMembers_;
    QComboBox* partPlateFollowCombo_ = nullptr;
    QCheckBox* partOutputPlateCheck_ = nullptr;
    QCheckBox* partOutputSurfaceCheck_ = nullptr;
    QCheckBox* partOutputWiresCheck_ = nullptr;
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
    QDoubleSpinBox* foldThicknessSpin_ = nullptr;
    QWidget* foldLinesContainer_ = nullptr;
    QVBoxLayout* foldLinesLayout_ = nullptr;
    std::vector<QDoubleSpinBox*> foldAngleSpins_;
    std::vector<QDoubleSpinBox*> foldPercentSpins_;
    std::vector<double> foldFullAngleDegrees_;
    QString foldLinesModelName_;
    bool syncingFoldRows_ = false;
    QSlider* foldSlider_ = nullptr;
    QLabel* foldLabel_ = nullptr;
    std::array<QWidget*, 4> sections_{};
    QWidget* bottomSpacer_ = nullptr; //!< 1区画表示時に項目を上詰めするための余白
};
