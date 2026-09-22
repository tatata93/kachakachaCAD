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
#include "kachakacha/fabrication/BendRadius.h"
#include "kachakacha/fabrication/FreezeState.h"

#include <QDockWidget>
#include <QString>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;

class V2FabricationDock final : public QDockWidget {
public:
    explicit V2FabricationDock(QWidget* parent);

    // ---- 近似の入力と候補(引継ぎ 2026-09-17 の 3)。「1 近似モデル」の最上段。
    //! 対象の名前、候補の3行、選んでいる候補、確定できるか。
    void ShowApproxInput(const QString& sourcesJa, const std::vector<QString>& candidateLinesJa,
        int selectedCandidate, bool canConfirm);
    //! 候補のボタンを押した(0 = A、1 = B、2 = C)。
    void SetCandidateHandler(std::function<void(int)> handler);
    //! 対象の「解除」を押した。
    void SetClearSourcesHandler(std::function<void()> handler);
    //! 近似の「キャンセル Esc」(共通の枠、C-10)。窓が Esc と同じ道へつなぐ。
    void SetCancelHandler(std::function<void()> handler);
    //! 試験から: 近似の段のキャンセルを押す(見えていて押せるときだけ)。
    [[nodiscard]] bool ClickCancelApprox();
    //! 表示(F-04): 元の面・近似の姿を出すか。見るだけの切り替えで、文書は変えない。
    [[nodiscard]] bool ShowSource() const;
    [[nodiscard]] bool ShowApprox() const;
    void SetShowSource(bool shown);
    void SetShowApprox(bool shown);
    void SetDisplayHandler(std::function<void()> handler);
    //! **見えているボタンを実際に押す。**人の道の試験はこちら。見えていなければ偽。
    [[nodiscard]] bool ClickCandidate(int candidate);
    [[nodiscard]] bool ClickClearSources();
    //! いま押された形で出ている候補。無ければ -1。
    [[nodiscard]] int SelectedCandidateShown() const;
    //! 候補の行に出ている言葉。試験から読む。
    [[nodiscard]] QString CandidateTextJa(int candidate) const;
    [[nodiscard]] QString SourcesTextJa() const;
    //! 作り方(標準 / 少部品優先 / 精度優先 / 手動条件)。押した番号は ApproxPolicySpecs の並び。
    void SetPolicyHandler(std::function<void(int)> handler);
    void ShowPolicy(int policy);
    [[nodiscard]] bool ClickPolicy(int policy);
    [[nodiscard]] int SelectedPolicyShown() const;
    [[nodiscard]] std::vector<QString> PolicyLabels() const;
    //! 曲げ状態の基準値(0/25/50/75/100)。押すと組立率を打って当てるのと同じ道。
    [[nodiscard]] bool ClickBendPreset(int percent);
    [[nodiscard]] std::vector<int> BendPresets() const;

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
    //! 3D で部材を押しても書き込む(F-05/06/07)。
    [[nodiscard]] QString PartNumbersText() const;
    void SetPartNumbersText(const QString& text);
    //! 「対象部材」の下に出す読み取り専用の一言(方式 / 最大誤差)。試験から読む。
    void SetPartInfoText(const QString& text);
    [[nodiscard]] QString PartInfoText() const;
    void SetAssemblyHandler(std::function<void(double percent, const QString& parts)> handler);
    void PressApplyAssembly();
    //! **人が欄に打ったのと同じ道。**組立率を打つと半径が、半径を打つと組立率が言い換わる。
    //! 「当てる」「固定」を押すまで文書は触らない。SetAssemblyPercent は言い換えない(窓が映す道)。
    void TypeAssemblyPercent(double percent);
    void TypeRadiusMm(double radiusMm);

    //! 固定で作るもの。
    [[nodiscard]] kachakacha::v2::fabrication::FreezeOutput FreezeOutputChoice() const;
    //! 半径の欄に出ている値と、自動か固定か。試験と窓から読む。
    [[nodiscard]] double RadiusMm() const;
    //! 半径の欄がいま触れるか。触れないなら、値ではなく理由が出ている。
    [[nodiscard]] bool RadiusUsable() const;
    //! 半径の欄のそばに出ている一文。試験から読む。
    [[nodiscard]] QString RadiusStateTextJa() const;
    [[nodiscard]] bool RadiusLocked() const;
    //! 半径の欄を映す。近似が測り直したときに呼ぶ。
    void ShowRadius(const kachakacha::v2::fabrication::BendRadius& bend, double percent);
    //! どの部材も読めないときに、理由だけを出す。値は出さない。
    //!
    //! 無い番号を書いたまま部材1の値を出すと、いまどの部材を読んでいるのかが
    //! 画面と食い違う(Codex Q1-Q5-R3 の UX 指摘)。
    void ShowRadiusUnavailable(const QString& whyJa);
    //! 「固定/自動」を押したときに呼ぶもの。押した時点の半径と曲げ具合を渡す。
    void SetRadiusHandler(std::function<void(double, bool)> handler);
    void PressLockRadius();
    void SetFreezeOutput(kachakacha::v2::fabrication::FreezeOutput value);
    void SetFreezeOutputHandler(
        std::function<void(kachakacha::v2::fabrication::FreezeOutput)> handler);

    //! 生成(正本 fabrication mock)の「作り方」カード: 現在状態 / Flat 0% / Target 100%。
    //! カードに出ている言葉。試験から読む。
    [[nodiscard]] std::vector<QString> GenerateCardLabels() const;
    //! **見えているカードを実際に押す。**見えていなければ偽。
    [[nodiscard]] bool ClickGenerateCard(const QString& labelJa);

    //! 展開(matrix F-11/F-12)の「作り方」カード: 自動展開 / 基準辺指定 / 複数部材配置。
    //! 「複数部材配置」は無効(理由は UnfoldCardTip)。カードは選ぶだけで、
    //! 実際に走らせるのは「展開」ボタン(PressUnfold)である。
    [[nodiscard]] std::vector<QString> UnfoldCardLabels() const;
    //! **見えているカードを実際に押す。**見えていない・無効なら偽。
    [[nodiscard]] bool ClickUnfoldCard(const QString& labelJa);
    [[nodiscard]] bool UnfoldCardEnabled(const QString& labelJa) const;
    //! カードのそばに出す一言(有効なカードは説明、無効なカードは無い理由)。
    [[nodiscard]] QString UnfoldCardTip(const QString& labelJa) const;
    //! 「展開先」が紙(型紙)だけである理由。核に作業面・XY平面への展開が無いので、
    //! 選べないことをボタンの下に常に出す(隠さない)。
    [[nodiscard]] QString UnfoldTargetReasonJa() const;
    //! 「展開」ボタン。いま選んでいるカードの道を、既存の PressRun で走らせる
    //! (基準辺指定は set_unfold_base の後に create_pattern。Preview/型紙棚の挙動は変えない)。
    void PressUnfold();

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
    [[nodiscard]] int StageIndex() const;
    void SetStageIndex(int index);
    //! 組立率の欄へ焦点を移す。メニューの「組立状態」はここへ来る(窓を出さない)。
    void FocusAssemblyField();

    // ---- 試験から ----
    void SetManualBoundariesText(const QString& text);
    void SetAutomaticBoundaries(bool automatic);
    void SetMaximumPartCount(int count);
    void SetSplitAxisIndex(int index);
    //! 「部材を分ける」で 1 枚を何枚に等分するか(2〜8)。人が欄に打つのと同じ道。
    [[nodiscard]] int SplitPieces() const;
    void SetSplitPieces(int pieces);
    //! 2 枚に分けるときの位置(番号の小さい側から、5〜95%)。等分(3 枚以上)では使わない。
    [[nodiscard]] int SplitPercent() const;
    void SetSplitPercent(int percent);

private:
    QWidget* BuildApproxInput(QWidget* body);
    QWidget* BuildDisplayRow(QWidget* page);
    QWidget* BuildOptionsForm(QWidget* body);
    QWidget* BuildRangeAndMaterial(QWidget* body);
    QWidget* BuildBendSection(QWidget* body);
    //! 部材の編集(分ける・1つにする・切れ目・展開の基準)。曲げの段と同じ棚に置く。
    QWidget* BuildPartEditSection(QWidget* body);
    //! 展開(F-11/F-12)の作り方カードと配置(展開先・表裏)。部材の編集のすぐ下に置く。
    QWidget* BuildUnfoldSection(QWidget* body);
    //! 生成(固定で作るもの・作り方カード)。コンストラクタを100行以内に保つために分けた。
    QWidget* BuildFreezeSection(QWidget* body);
    //! 組立率を打ったら半径を、半径を打ったら組立率を言い換える。当てるまで文書は触らない。
    void SyncRadiusFromPercent();
    void SyncPercentFromRadius();
    void Connect();
    void Emit();
    void RefreshMethodRows();

    QLabel* model_ = nullptr;
    QTabWidget* stages_ = nullptr;
    QComboBox* method_ = nullptr;
    QFormLayout* form_ = nullptr;
    QComboBox* splitAxis_ = nullptr;
    QCheckBox* automatic_ = nullptr;
    //! 立体を面ごとに分けるか。V2 方式(面を分類して展開)だけが使う。
    QCheckBox* splitSolidFaces_ = nullptr;
    QLineEdit* manual_ = nullptr;
    QDoubleSpinBox* maxParts_ = nullptr;
    QDoubleSpinBox* minWidth_ = nullptr;
    QDoubleSpinBox* fidelity_ = nullptr;
    //! 切れ目の上限。材料で変わるので、隠した既定値にしない。
    QDoubleSpinBox* reliefDepth_ = nullptr;
    QDoubleSpinBox* reliefLigament_ = nullptr;
    //! 「部材を分ける」の枚数(等分)。
    QSpinBox* splitPieces_ = nullptr;
    //! 「部材を分ける」の位置(%)。2 枚のときだけ効く(F-06 位置指定)。
    QSpinBox* splitPercent_ = nullptr;
    QDoubleSpinBox* thickness_ = nullptr;
    QDoubleSpinBox* deviation_ = nullptr;
    QDoubleSpinBox* assembly_ = nullptr;
    //! 曲げた先の半径(§30・§31)。曲げ具合と同じことの言い換えである。
    //! 「自動」は近似が測った値、「固定」は人が入れた値。作り直しても戻さない。
    QDoubleSpinBox* radius_ = nullptr;
    QPushButton* lockRadius_ = nullptr;
    QLabel* radiusState_ = nullptr;
    //! いま欄に映している部材の曲げ。組立率と半径を、どちらから打っても
    //! もう片方へ言い換えるために持つ(引継ぎ 2026-09-17 の 5)。
    kachakacha::v2::fabrication::BendRadius shownBend_;
    bool bendShown_ = false;
    QPushButton* applyAssembly_ = nullptr;
    QLineEdit* parts_ = nullptr;
    //! 「方式 / 最大誤差」の読み取り専用の一言。3D で押した部材が分かったときに映す。
    QLabel* partInfo_ = nullptr;
    QComboBox* freeze_ = nullptr;
    QDoubleSpinBox* rangeUMin_ = nullptr;
    QDoubleSpinBox* rangeUMax_ = nullptr;
    QDoubleSpinBox* rangeVMin_ = nullptr;
    QDoubleSpinBox* rangeVMax_ = nullptr;
    QLineEdit* material_ = nullptr;
    QDoubleSpinBox* layers_ = nullptr;
    QPushButton* applyMaterial_ = nullptr;
    QLabel* message_ = nullptr;
    //! 近似の入力と候補。
    QLabel* sourcesValue_ = nullptr;
    //! 対象の欄の中身(空なら何も入っていない。札の「押してください」は中身ではない)。
    QString sourcesJa_;
    QPushButton* clearSources_ = nullptr;
    std::vector<QPushButton*> candidates_;
    std::vector<QPushButton*> policies_;
    //! 生成の「作り方」カード(現在状態 / Flat 0% / Target 100%)。正本 fabrication mock。
    std::vector<QPushButton*> generateCards_;
    //! 展開の「作り方」カード(自動展開 / 基準辺指定 / 複数部材配置。matrix F-11/F-12)。
    std::vector<QPushButton*> unfoldCards_;
    //! 選んだカードのそばに出す一言(いまは基準辺指定のときだけ中身がある)。
    QLabel* unfoldHint_ = nullptr;
    //! 展開先(紙(A4型紙)だけが実際に選べる。欄ごと disabled にして並べて見せる)。
    QComboBox* unfoldTarget_ = nullptr;
    //! 展開先が紙だけである理由。常に見える(核に作業面への展開が無いため)。
    QLabel* unfoldTargetReason_ = nullptr;
    //! 表裏(核に反転が無いので disabled のまま)。
    QComboBox* unfoldFlip_ = nullptr;
    QPushButton* unfoldRun_ = nullptr;
    std::function<void(int)> policyHandler_;
    std::vector<QPushButton*> bendPresets_;
    class QSlider* bendSlider_ = nullptr;
    QPushButton* confirmApprox_ = nullptr;
    QPushButton* cancelApprox_ = nullptr;
    std::function<void()> cancelHandler_;
    std::function<void(int)> candidateHandler_;
    std::function<void()> clearSourcesHandler_;
    std::function<void()> displayHandler_;
    QCheckBox* showSource_ = nullptr;
    QCheckBox* showApprox_ = nullptr;
    std::function<void(const QString&, int)> materialHandler_;
    std::function<void()> choiceChanged_;
    std::function<void(kachakacha::v2::app::ParameterId, double)> parameterHandler_;
    std::function<void(double, const QString&)> assemblyHandler_;
    std::function<void(double, bool)> radiusHandler_;
    bool radiusLocked_ = false;
    std::function<void(kachakacha::v2::fabrication::FreezeOutput)> freezeHandler_;
    std::function<void(const char*)> runHandler_;
    bool loading_ = false;
};
