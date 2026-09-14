#pragma once

//! 押し出しの棚(オーナー指示 2026-09-14 §7)。
//!
//! これまで押し出しは窓(モーダル)で全部決めてから作っていた。
//! 押す前に何ができるのか見えないので、初めての人には難しい。
//!
//! ここは右に出しっぱなしにする棚である。出すのは3つだけ。
//!   1. いまの入力(CADが何を対象・輪郭として読んだか)
//!   2. いま変えられる主なもの(距離・向き・範囲・操作)
//!   3. 結果と、確定・取消
//!
//! **いまの入力で意味のない欄は出さない。**
//! 立体を選んでいないときに「切削」を出しても、押せば断られるだけである。
//!
//! 細かい設定(ある面まで、2距離、出力の種類)は「詳細」の窓へ回す。
//! 全部を右へ並べると、どれを見ればよいのか分からなくなる。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include "kachakacha/app/ExtrudeOptions.h"
#include "kachakacha/app/ExtrudePlan.h"

#include <QDockWidget>
#include <QString>

#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QWidget;

class V2ExtrudeDock final : public QDockWidget {
public:
    explicit V2ExtrudeDock(QWidget* parent);

    //! CADが選択をどう読んだかを映す。読み方が変わるたびに呼ぶ。
    void ShowPlan(const kachakacha::v2::app::ExtrudePlan& plan, const QString& targetNameJa,
        const QString& profileNamesJa);
    //! 距離を映す(矢印を引いたとき)。欄と矢印は常に同じ値を出す。
    void SetDistanceMm(double value);
    //! 人が距離を打ったのと同じ扱いにする(矢印と下見も追う)。試験から呼ぶ。
    void TypeDistanceMm(double value);
    [[nodiscard]] double DistanceMm() const;
    //! 棚で選んでいる向きの決め方。0 = 面(輪郭)に垂直、1 = 作業平面に垂直。
    [[nodiscard]] kachakacha::v2::modeling::ExtrudeDirectionMode DirectionMode() const;
    //! 向きの欄を選び直す。棚を書き直すときと、試験から使う。
    void ChooseDirection(kachakacha::v2::modeling::ExtrudeDirectionMode mode);
    //! いま欄で選んでいる操作。
    [[nodiscard]] kachakacha::v2::modeling::ExtrudeBooleanMode BooleanMode() const;
    //! 両側へ押すか。
    [[nodiscard]] bool Symmetric() const;
    //! 向きを反転しているか。
    [[nodiscard]] bool Reversed() const;

    //! 距離が打たれたときに呼ぶもの(矢印と下見を合わせる)。
    void SetDistanceHandler(std::function<void(double)> handler);
    //! 向き反転・範囲・操作が変わったときに呼ぶもの(下見を作り直す)。
    void SetOptionHandler(std::function<void()> handler);
    //! 確定・取消・詳細。
    void SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel,
        std::function<void()> details);
    //! 「選び直す」(EX-07)。読み取った対象・輪郭のどちらかを外して選び直す。
    //!
    //! 選択を触れば読み直されるので機能としては足りていたが、
    //! **初めての人には、いま何を外せばよいのかが分からない。**
    //! 読み取った当人(棚)が、外し方まで出す。
    void SetReselectHandlers(std::function<void()> target, std::function<void()> profile);

    //! 試験から見る。
    [[nodiscard]] QString InputTextJa() const;
    [[nodiscard]] bool OperationRowShown() const;
    //! 「選び直す」のボタンが出ているか。試験から見る。
    [[nodiscard]] bool ReselectTargetShown() const;
    [[nodiscard]] bool ReselectProfileShown() const;
    void PressConfirm();
    void PressCancel();
    void PressReverse();
    //! 「操作」の欄を選ぶ。人が選んだのと同じ扱いにする。
    void ChooseBoolean(kachakacha::v2::modeling::ExtrudeBooleanMode mode);
    void PressReselectTarget();
    void PressReselectProfile();

private:
    void ApplyRows();
    //! 入力(加工する立体と輪郭)が同じか。
    [[nodiscard]] static bool SameInputs(const kachakacha::v2::app::ExtrudePlan& left,
        const kachakacha::v2::app::ExtrudePlan& right);
    //! 欄の便りを繋ぐ。組み立てと分けてある(1関数100行の門)。
    void ConnectRows();

    QWidget* body_ = nullptr;
    QLabel* input_ = nullptr;
    QDoubleSpinBox* distance_ = nullptr;
    QComboBox* direction_ = nullptr;
    QPushButton* reverse_ = nullptr;
    QComboBox* extent_ = nullptr;
    QComboBox* boolean_ = nullptr;
    QPushButton* reselectTarget_ = nullptr;
    QPushButton* reselectProfile_ = nullptr;
    QLabel* result_ = nullptr;
    QPushButton* details_ = nullptr;
    QPushButton* confirm_ = nullptr;
    QPushButton* cancel_ = nullptr;
    class QFormLayout* form_ = nullptr;

    kachakacha::v2::app::ExtrudePlan plan_;
    bool reversed_ = false;
    //! 人が「操作」の欄を選んだか。選んだら既定で上書きしない(R1 B4)。
    bool operationChosenByUser_ = false;
    bool loading_ = false;
    std::function<void(double)> distanceHandler_;
    std::function<void()> optionHandler_;
    std::function<void()> confirmHandler_;
    std::function<void()> cancelHandler_;
    std::function<void()> detailsHandler_;
    std::function<void()> reselectTargetHandler_;
    std::function<void()> reselectProfileHandler_;
};
