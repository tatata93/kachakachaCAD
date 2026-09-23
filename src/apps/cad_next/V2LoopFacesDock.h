#pragma once

//! 「面にする」の棚(線から面、matrix P-14 相当)。
//!
//!   1. 作り方 … 許容(端)。輪ごとの作り方はここでは選ばず、下の 2. の欄ごとに選ぶ
//!   2. 輪     … 見つかった輪ごとに、作り方・辺の数・下見の様子・作るかどうか
//!   3. ずれ・T 字 … 端が離れている所を寄せる/そのまま、T 字の一文、使わない線
//!   4. 状態   … 内訳(平面・四辺面・境界面の数)
//! 下に キャンセル / 確定。
//!
//! 何を面にするか・作り方・ずれは core(app/LoopFaces)が線のつながりから決める。
//! ここは映して押すだけ。行(輪・ずれ)は毎回作り直す(数が変わるので)。

#include <QDockWidget>
#include <QString>

#include <functional>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

//! 輪の辺 1 つ(連続の欄)。隣が無い辺は出さない。
struct V2LoopEdgeCell {
    int edge = 0;                //!< 辺の番号(0 から。表示は 1 から)
    QString neighborJa;          //!< 「すでにある面 面 3 の縁」「輪 2 と共有」
    int continuityIndex = 0;     //!< 0 = G0, 1 = G1, 2 = G2
    bool allowed = true;         //!< この作り方(四辺面・境界面)で連続を付けられるか。平面は偽
};

//! 「面にする」の棚の 1 行(輪 1 つ)。
struct V2LoopFaceRow {
    int number = 1;                       //!< ① から
    std::vector<QString> methodChoicesJa; //!< 作り方の選択肢(表示名)
    int methodIndex = 0;                  //!< いま選ばれている選択肢
    int edgeCount = 0;                    //!< 辺(側)の数(ロフトなら断面の数)
    int lineCount = 0;                    //!< 線の本数(辺の数と違えば「辺 4(線 5)」と出す)
    QString statusJa;                     //!< 「✓」「最大ずれ 0.02 mm」など
    bool make = true;                     //!< 作るか
    std::vector<V2LoopEdgeCell> edges;    //!< 隣のある辺(連続 G0/G1/G2 を押して回す)
};

//! ずれの 1 行。
struct V2LoopGapRow {
    QString textJa;      //!< 「直線 3 と 円弧 1 の端が 3.082 mm 離れています」
    bool movable = true; //!< [寄せる] を押せるか
    bool leave = false;  //!< 「そのまま」を選んだ(寄せない)
};

struct V2LoopFacesView {
    std::vector<V2LoopFaceRow> faces;
    std::vector<V2LoopGapRow> gaps;
    std::vector<QString> splitsJa;   //!< T 字の一文(1 行ずつ)
    QString unusedJa;                //!< 「使わない線: 直線 6」(空なら行を出さない)
    double joinMm = 0.01;            //!< 許容(端)
    QString summaryJa;               //!< 「平面 2・四辺面 1」
    bool canConfirm = false;
};

class V2LoopFacesDock final : public QDockWidget {
public:
    explicit V2LoopFacesDock(QWidget* parent);

    //! 計画を映す。行は毎回作り直す(輪の数が変わる)。
    void ShowView(const V2LoopFacesView& view);
    //! 直前の操作(UI 設計 2-6): 「直前: 面にする(3 枚)」と [開いて直す]。作ったあとに出す。
    //! 開くと 1 回の取り消しで元に戻し、同じ線・同じ選択で構え直す(値を変えて Enter で作り直す)。
    void ShowRecent(const QString& textJa);
    void HideRecent();
    void SetReopenHandler(std::function<void()> handler);
    [[nodiscard]] bool ClickReopen();
    [[nodiscard]] QString RecentTextJa() const;

    void SetMethodHandler(std::function<void(int face, int methodIndex)> handler);
    void SetMakeHandler(std::function<void(int face, bool make)> handler);
    //! [寄せる] / [そのまま](そのままはトグル: 押すと leave が反転する)。
    void SetGapHandlers(std::function<void(int gap)> close, std::function<void(int gap)> leave);
    void SetToleranceHandler(std::function<void(double joinMm)> handler);
    //! 辺の連続の欄を押した(G0 → G1 → G2 → G0 と回す)。
    void SetContinuityHandler(std::function<void(int face, int edge)> handler);
    void SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel);

    //! 見えていて押せるものを実際に押す(自己試験用)。
    [[nodiscard]] bool ClickConfirm();
    [[nodiscard]] bool ClickCancel();
    [[nodiscard]] bool ClickCloseGap(int gap);
    [[nodiscard]] bool ClickLeaveGap(int gap);
    [[nodiscard]] bool ChooseMethod(int face, int methodIndex);
    [[nodiscard]] bool ToggleMake(int face);
    [[nodiscard]] bool TypeTolerance(double joinMm);
    //! 辺の連続の欄を押す(自己試験用)。欄が無い・押せないなら偽。
    [[nodiscard]] bool CycleContinuity(int face, int edge);
    //! 辺の連続の欄の文(「辺 2 G1」)。無ければ空。
    [[nodiscard]] QString EdgeCellTextJa(int face, int edge) const;

    [[nodiscard]] int FaceRowCount() const;
    [[nodiscard]] int GapRowCount() const;
    //! 「① 平面  辺 3  ✓」のような 1 行の文字。
    [[nodiscard]] QString FaceRowTextJa(int face) const;
    [[nodiscard]] QString GapRowTextJa(int gap) const;
    [[nodiscard]] QString SummaryTextJa() const;
    //! T 字の一文(複数なら改行でつなぐ)。無ければ空。
    [[nodiscard]] QString SplitTextJa() const;
    [[nodiscard]] bool ConfirmEnabled() const;

private:
    //! 輪 1 行分の持ち物(2. 節)。
    struct FaceRowWidgets {
        QWidget* row = nullptr;
        QLabel* number = nullptr;
        QComboBox* method = nullptr;
        QLabel* edges = nullptr;
        QLabel* status = nullptr;
        QCheckBox* make = nullptr;
        QVBoxLayout* column = nullptr;
        QWidget* edgeLine = nullptr;
        std::vector<std::pair<int, QPushButton*>> edgeButtons;   //!< 辺の番号と欄
    };
    //! ずれ 1 行分の持ち物(3. 節)。
    struct GapRowWidgets {
        QWidget* row = nullptr;
        QLabel* text = nullptr;
        QPushButton* close = nullptr;
        QPushButton* leave = nullptr;
    };

    void RebuildFaceRows(const std::vector<V2LoopFaceRow>& faces);
    void RebuildGapSection(const V2LoopFacesView& view);
    [[nodiscard]] FaceRowWidgets MakeFaceRow(const V2LoopFaceRow& face);
    void AddEdgeLine(FaceRowWidgets& widgets, int faceIndex, const V2LoopFaceRow& face);
    [[nodiscard]] GapRowWidgets MakeGapRow(int gap, const V2LoopGapRow& row);

    QWidget* recentRow_ = nullptr;
    QLabel* recent_ = nullptr;
    QPushButton* reopen_ = nullptr;
    QWidget* planBody_ = nullptr;   //!< 計画の段(1.〜4. と確定)。直前の操作だけのときは隠す
    QVBoxLayout* facesLayout_ = nullptr;
    QVBoxLayout* gapsLayout_ = nullptr;
    QDoubleSpinBox* tolerance_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* confirm_ = nullptr;
    bool loading_ = false;

    std::vector<FaceRowWidgets> faceRows_;
    std::vector<GapRowWidgets> gapRows_;
    std::vector<QLabel*> splitLabels_;
    QLabel* unusedLabel_ = nullptr;

    std::function<void(int, int)> methodHandler_;
    std::function<void(int, bool)> makeHandler_;
    std::function<void(int, int)> continuityHandler_;
    std::function<void()> reopenHandler_;
    std::function<void(int)> closeGapHandler_;
    std::function<void(int)> leaveGapHandler_;
    std::function<void(double)> toleranceHandler_;
    std::function<void()> confirmHandler_;
    std::function<void()> cancelHandler_;
};
