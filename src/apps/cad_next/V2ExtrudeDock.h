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
//! 正本(part mock 2026-09-18、Inventor 風)に合わせ、範囲(距離/対称/非対称/面まで/貫通)と
//! 方向(7通り)は **全部この棚に** 出す(指示書 P-02 / P-04)。核に無い From とテーパーは
//! 押せない形で理由を出す(P-03 / P-07)。「詳細」の窓は残すが、棚だけで全部決められる。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include "V2ExtrudeTargetChoice.h"

#include "kachakacha/app/ExtrudeInputState.h"
#include "kachakacha/app/ExtrudeOptions.h"
#include "kachakacha/app/ExtrudePlan.h"

#include <QDockWidget>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

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
    //! 棚で選んでいる終端。詳細の窓で決めたものは3つ目に名前で出る。
    [[nodiscard]] kachakacha::v2::modeling::ExtrudeExtentMode ExtentMode() const;
    void ChooseExtent(kachakacha::v2::modeling::ExtrudeExtentMode mode);
    //! 人が選んだのと同じ道(combo の signal を塞がない)。自己試験はこちらを使う。
    [[nodiscard]] bool PickDirection(kachakacha::v2::modeling::ExtrudeDirectionMode mode);
    [[nodiscard]] bool PickExtent(kachakacha::v2::modeling::ExtrudeExtentMode mode);
    //! 向きを反転しているか。
    [[nodiscard]] bool Reversed() const;
    //! 「数値で決める」「選んだ線の向き」のときの向き(x, y, z)。
    [[nodiscard]] kachakacha::v2::geometry::Vector3 CustomDirection() const;
    void SetCustomDirection(const kachakacha::v2::geometry::Vector3& direction);
    //! 「両方向に別々の距離」の逆側。
    [[nodiscard]] double SecondDistanceMm() const;
    void SetSecondDistanceMm(double value);
    //! 「選んだ面まで」の相手に出せる作業平面。棚を出すときに文書から渡す。
    void SetTargets(const std::vector<ExtrudeTargetChoice>& targets);
    //! いま選んでいる相手。無ければ値を持たない。
    [[nodiscard]] std::optional<kachakacha::v2::base::EntityId> TargetEntityId() const;
    void ChooseTarget(const std::optional<kachakacha::v2::base::EntityId>& id);
    //! 範囲の欄に並ぶ言葉(試験から)。
    [[nodiscard]] std::vector<QString> ExtentLabels() const;
    [[nodiscard]] std::vector<QString> DirectionLabels() const;
    //! 逆側の距離 / 相手 の欄が見えているか(試験から)。
    [[nodiscard]] bool SecondDistanceShown() const;
    [[nodiscard]] bool TargetRowShown() const;
    //! 核に無い欄(From / テーパー)は押せない形で、理由がツールチップに出ている。
    [[nodiscard]] QString FromBlockedReasonJa() const;
    [[nodiscard]] QString TaperBlockedReasonJa() const;

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
    //! 「1. 入力」の2欄に、いま出ている言葉。**画面に出ているものそのもの。**
    //! 試験が「選んだものが人に見えているか」を見るために要る(§7)。
    [[nodiscard]] QString TargetTextJa() const;
    [[nodiscard]] QString ProfileTextJa() const;
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
    //! いま選んでいる出力。確定はこの値で作る。
    [[nodiscard]] kachakacha::v2::app::ExtrudeOutputs Outputs() const;
    //! 出力を映す。プリセットの名前も一緒に合わせる。
    void ShowOutputs(const kachakacha::v2::app::ExtrudeOutputs& outputs);
    //! 「状態」に出す行(UI の正本「3. 状態」)。
    void ShowStatusLines(const std::vector<QString>& lines, bool canConfirm);

private:
    //! 出力の欄どうしを合わせる。プリセットを選んだら4項目、
    //! 4項目を触ったらプリセットの名前。
    void SyncOutputRows(bool fromPreset);
    void ApplyRows();
    //! 範囲と方向に応じて、距離 / 逆側の距離 / 相手 / 向きの数 を出し入れする。
    void ApplyExtentRows();
    //! 入力(加工する立体と輪郭)が同じか。
    [[nodiscard]] static bool SameInputs(const kachakacha::v2::app::ExtrudePlan& left,
        const kachakacha::v2::app::ExtrudePlan& right);
    //! 欄の便りを繋ぐ。組み立てと分けてある(1関数100行の門)。
    void ConnectRows();
    //! 見出しと「1. 入力」を組み立てる。
    void BuildHeaderAndInputRows(class QVBoxLayout* layout);
    //! 「2. 結果」の欄を組み立てる。
    void BuildOptionRows(class QVBoxLayout* layout);

    QWidget* body_ = nullptr;
    //! 見出しの下に出す、いまの様子(「プレビュー可能」など)。
    QLabel* state_ = nullptr;
    //! 入力の2欄。**対象と輪郭を別に出す**(UI の正本「1. 入力」)。
    QLabel* targetValue_ = nullptr;
    QLabel* profileLabel_ = nullptr;
    QLabel* profileValue_ = nullptr;
    //! 読み取りの全文。狭いときのために持つが、ふだんは出さない。
    QLabel* input_ = nullptr;
    //! 下見を作り直す。
    QPushButton* rePreview_ = nullptr;
    QDoubleSpinBox* distance_ = nullptr;
    QDoubleSpinBox* secondDistance_ = nullptr;
    QComboBox* target_ = nullptr;
    std::vector<ExtrudeTargetChoice> targets_;
    //! 核に無い From(開始面)とテーパー。押せない形で理由を出す。
    QLabel* fromValue_ = nullptr;
    QDoubleSpinBox* taper_ = nullptr;
    QComboBox* direction_ = nullptr;
    //! 「数値で決める」の向き(x, y, z)。
    QWidget* customRow_ = nullptr;
    QDoubleSpinBox* customX_ = nullptr;
    QDoubleSpinBox* customY_ = nullptr;
    QDoubleSpinBox* customZ_ = nullptr;
    QPushButton* reverse_ = nullptr;
    QComboBox* extent_ = nullptr;
    QComboBox* boolean_ = nullptr;
    //! 出力プリセット(UI の正本「2. 結果」)。
    QComboBox* outputPreset_ = nullptr;
    //! 出力の4項目。プリセットと必ず同じものを指す。
    class QCheckBox* outBody_ = nullptr;
    class QCheckBox* outStartWire_ = nullptr;
    class QCheckBox* outEndWire_ = nullptr;
    class QCheckBox* outSideWires_ = nullptr;
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
