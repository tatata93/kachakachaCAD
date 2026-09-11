#pragma once

//! 型紙の下見(棚卸し A-3)。
//!
//! 型紙は作れるが、画面で確かめる道が無かった。SVG や DXF に出して別の道具で
//! 開くまで、紙に収まっているのかも、部材が何枚あるのかも分からない。
//! **紙とプラ板を無駄にしてから気づく** ことになる。
//!
//! ここは出来た型紙をそのまま描くだけで、何も決めない。
//! 紙の収め方は core(`view/PatternView`)、線の種類は `exporters/PatternExport`。
//!
//! 原寸では出さない。画面に収めて全体を見せる。
//! 原寸で確かめるのは紙に出してからで、そのための 1:1 PDF が別にある。

#include "kachakacha/exporters/PatternExport.h"

#include <QDockWidget>
#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QPushButton;

//! 紙1枚を描くところ。
class V2PatternView final : public QWidget {
public:
    explicit V2PatternView(QWidget* parent);

    void SetPage(const kachakacha::v2::exporters::PatternPage& page);
    void Clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    kachakacha::v2::exporters::PatternPage page_;
    bool hasPage_ = false;
};

class V2PatternDock final : public QDockWidget {
public:
    explicit V2PatternDock(QWidget* parent);

    //! 出す型紙を入れ替える。ページが無ければ「まだありません」と出す。
    void SetPages(std::vector<kachakacha::v2::exporters::PatternPage> pages);

    //! 何ページあるか。試験で見る。
    [[nodiscard]] int PageCount() const { return static_cast<int>(pages_.size()); }
    //! いま何ページ目を見ているか(0始まり)。ページが無ければ -1。
    [[nodiscard]] int CurrentPage() const { return pages_.empty() ? -1 : current_; }
    //! ページを送る(試験と、ボタンの両方から)。範囲の外なら何もしない。
    void ShowPage(int index);
    //! 画面に出ている一文(枚数・紙の大きさ・線の数)。
    [[nodiscard]] QString SummaryText() const;

private:
    void Refresh();

    std::vector<kachakacha::v2::exporters::PatternPage> pages_;
    int current_ = 0;
    V2PatternView* view_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* previous_ = nullptr;
    QPushButton* next_ = nullptr;
};
