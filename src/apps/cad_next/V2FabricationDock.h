#pragma once

//! 製作の棚(V1 の近似モデル画面 `PartModelPanel` を 1 枚にしたもの、走査 §2-3)。
//!
//! 方式 / 分割軸 / 境界(自動・手動)/ 上限 / 最小幅 / 再現度 / 板厚 / 許すずれ /
//! 組立率 / 固定で作るもの、そして 製作モデルを作る・プレビュー更新・境界の役割・
//! 接続スコープ・現在状態を固定・型紙を作る のボタン。
//!
//! V2 は命令(`fabrication.*`)として全部あったが、方式と固定の種類は押すたびに回る切替で、
//! 上限や最小幅は変えられなかった。ここで欄にする。欄の値の検査と作り方への写しは
//! core(app/FabricationOptions)が持つ。板厚と許すずれは数の棚と同じ値を映す。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/app/FabricationOptions.h"
#include "kachakacha/fabrication/FreezeState.h"

#include <QDockWidget>
#include <QString>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;

class V2FabricationDock final : public QDockWidget {
public:
    explicit V2FabricationDock(QWidget* parent);

    //! いまの欄(手動境界は読めなければ空で返す。読めたかは ManualBoundariesReadable で見る)。
    [[nodiscard]] kachakacha::v2::app::FabricationChoice Choice() const;
    //! 手動境界の欄が読めるか。読めなければ error に理由(UI-F001)を書く。
    [[nodiscard]] bool ManualBoundariesReadable(QString* error) const;
    void SetChoice(const kachakacha::v2::app::FabricationChoice& choice);
    //! 欄が変わったら呼ぶもの(画面が値を持ち直す)。
    void SetChoiceChangedHandler(std::function<void()> handler);

    //! 板厚・許すずれ(数の棚と同じ値)。
    void SetParameterMm(kachakacha::v2::app::ParameterId id, double value);
    void SetParameterHandler(
        std::function<void(kachakacha::v2::app::ParameterId, double)> handler);

    //! 組立率(0 = 平ら、100 = 完成形)。「当てる」で handler(組立率と部材番号の欄)。
    void SetAssemblyPercent(double percent);
    [[nodiscard]] double AssemblyPercent() const;
    //! 部材番号の欄(V1 の「選んだ部材だけが曲がる」)。空なら全体。
    [[nodiscard]] QString PartNumbersText() const;
    void SetPartNumbersText(const QString& text);
    void SetAssemblyHandler(std::function<void(double percent, const QString& parts)> handler);
    void PressApplyAssembly();

    //! 固定で作るもの。
    [[nodiscard]] kachakacha::v2::fabrication::FreezeOutput FreezeOutputChoice() const;
    void SetFreezeOutput(kachakacha::v2::fabrication::FreezeOutput value);
    void SetFreezeOutputHandler(
        std::function<void(kachakacha::v2::fabrication::FreezeOutput)> handler);

    //! ボタン。command は台帳の ID(fabrication.create など)。
    void SetRunHandler(std::function<void(const char* command)> handler);
    void PressRun(const char* command);

    //! 材料と積層(V1 の板材の「材料」「積層」)。「選んだものに当てる」で handler。
    void SetMaterialHandler(std::function<void(const QString& material, int layers)> handler);
    void SetMaterial(const QString& material, int layers);
    [[nodiscard]] QString MaterialName() const;
    [[nodiscard]] int LayerCount() const;
    void PressApplyMaterial();
    //! 範囲の欄(試験用)。
    void SetRange(double uMin, double uMax, double vMin, double vMax);

    void SetMessage(const QString& text);
    [[nodiscard]] QString MessageText() const;
    //! 選んでいる近似モデルの名前など。
    void SetModelText(const QString& text);

    // ---- 試験から ----
    void SetManualBoundariesText(const QString& text);
    void SetAutomaticBoundaries(bool automatic);
    void SetMaximumPartCount(int count);
    void SetSplitAxisIndex(int index);

private:
    QWidget* BuildOptionsForm(QWidget* body);
    QWidget* BuildRangeAndMaterial(QWidget* body);
    QWidget* BuildBendSection(QWidget* body);
    void Connect();
    void Emit();
    void RefreshMethodRows();

    QLabel* model_ = nullptr;
    QComboBox* method_ = nullptr;
    QFormLayout* form_ = nullptr;
    QComboBox* splitAxis_ = nullptr;
    QCheckBox* automatic_ = nullptr;
    QLineEdit* manual_ = nullptr;
    QDoubleSpinBox* maxParts_ = nullptr;
    QDoubleSpinBox* minWidth_ = nullptr;
    QDoubleSpinBox* fidelity_ = nullptr;
    QDoubleSpinBox* thickness_ = nullptr;
    QDoubleSpinBox* deviation_ = nullptr;
    QDoubleSpinBox* assembly_ = nullptr;
    QPushButton* applyAssembly_ = nullptr;
    QLineEdit* parts_ = nullptr;
    QComboBox* freeze_ = nullptr;
    QDoubleSpinBox* rangeUMin_ = nullptr;
    QDoubleSpinBox* rangeUMax_ = nullptr;
    QDoubleSpinBox* rangeVMin_ = nullptr;
    QDoubleSpinBox* rangeVMax_ = nullptr;
    QLineEdit* material_ = nullptr;
    QDoubleSpinBox* layers_ = nullptr;
    QPushButton* applyMaterial_ = nullptr;
    QLabel* message_ = nullptr;
    std::function<void(const QString&, int)> materialHandler_;
    std::function<void()> choiceChanged_;
    std::function<void(kachakacha::v2::app::ParameterId, double)> parameterHandler_;
    std::function<void(double, const QString&)> assemblyHandler_;
    std::function<void(kachakacha::v2::fabrication::FreezeOutput)> freezeHandler_;
    std::function<void(const char*)> runHandler_;
    bool loading_ = false;
};
