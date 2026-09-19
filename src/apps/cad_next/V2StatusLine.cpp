//! 一番下の状態行と 3D 左上の HUD(正本 3 HTML 2026-09-18、指示書 C-11 / C-12)、
//! 測定の重ね道具(C-16)。文言は core(app/StatusLine)が組む。ここは並べるだけ。
//!
//!   状態行: [モード ｜ 道具] [グループ] [案内 …] ‖ [座標 ｜ Grid ｜ Snap ｜ Enter/Esc]
//!   HUD:    3D の左上に「モード › 道具」と案内。測定を重ねていれば戻り先も。

#include "V2MainWindow.h"

#include "V2OperationPanelHost.h"
#include "V2Viewport.h"

#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/StatusLine.h"
#include "kachakacha/modeling/ToolController.h"

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QObject>
#include <QPoint>
#include <QSizePolicy>
#include <QStatusBar>
#include <QString>
#include <QWidget>

#include <string>
#include <vector>

using kachakacha::v2::app::StatusLineParts;
using kachakacha::v2::modeling::DrawingTool;

namespace {

[[nodiscard]] QString Text(const std::string& value)
{
    return QString::fromUtf8(value.c_str());
}

} // namespace

void V2MainWindow::BuildStatusBar()
{
    toolLabel_ = new QLabel(this);
    groupLabel_ = new QLabel(this);
    statusLabel_ = new QLabel(this);
    statusBar()->addWidget(toolLabel_);
    // 作業中グループは常に見えるところに置く(ui-workflows §1 の上の帯)。
    statusBar()->addWidget(groupLabel_);
    // 案内は長いので、幅が足りないときは案内のほうが縮む(文字が切れる)。右の
    // 座標 ｜ Grid ｜ Snap ｜ キー を押しつぶさない(PC 1.5 倍で「Enter 確定」が切れた)。
    statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(statusLabel_, 1);
    // 右側: 座標 ｜ Grid ｜ Snap ｜ Enter/Esc。いつでも見えている。
    cursorLabel_ = new QLabel(this);
    cursorLabel_->setObjectName(QStringLiteral("statusRight"));
    statusBar()->addPermanentWidget(cursorLabel_);
    // 帯を右クリックしても診断を取れるようにする(DIAGNOSTICS_FEATURE_SPEC)。
    // おかしいと思った瞬間に、献立を辿らずに取れるほうがよい。
    // 辿っているあいだに状態が変わってしまうことがある。
    statusBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(statusBar(), &QWidget::customContextMenuRequested, this,
        [this](const QPoint& at) {
            QMenu menu(this);
            QAction* copy = menu.addAction(QStringLiteral("診断情報をコピー"));
            if (menu.exec(statusBar()->mapToGlobal(at)) == copy) {
                RunCommand("help.copy_diagnostics");
            }
        });
    RefreshStatusLine();
}

//! いまの状態を1つにまとめる。文言はこれから core が組む。
StatusLineParts V2MainWindow::BuildStatusLineParts() const
{
    StatusLineParts parts;
    parts.mode = mode_;
    const DrawingTool tool = session_->CurrentTool();
    parts.toolJa = tool == DrawingTool::Select
        ? RunningOperationNameJa()
        : std::string(kachakacha::v2::modeling::DrawingToolNameJa(tool));
    parts.hintJa = statusLabel_ == nullptr ? std::string() : statusLabel_->text().toStdString();
    if (viewport_ != nullptr) {
        if (const auto at = viewport_->HoverPosition(); at.has_value()) {
            parts.cursorU = viewport_->WorkPlane().CoordinateU(*at);
            parts.cursorV = viewport_->WorkPlane().CoordinateV(*at);
        }
    }
    const auto& grid = session_->Scene().grid;
    parts.gridMm = grid.majorSpacingMm;
    parts.gridShown = grid.visible;
    parts.snapOn = snapEnabled_;
    if (toolBeforeMeasure_.has_value() && tool == DrawingTool::Measure) {
        parts.resumeToolJa =
            std::string(kachakacha::v2::modeling::DrawingToolNameJa(*toolBeforeMeasure_));
    }
    return parts;
}

//! 棚で進める操作(押し出し・面を作る・足す引く・厚み・近似・面取り・配列)の名前。
//! これらは作図の道具(DrawingTool)ではなく「選択」のまま棚で進むので、道具名を
//! 空のままにすると HUD と状態行が「部品 › 選択」になり、厚みの最中と分からない
//! (PC の絵 2026-09-19)。動いていなければ空。
std::string V2MainWindow::RunningOperationNameJa() const
{
    using kachakacha::v2::app::Shelf;
    const bool running = extrudeShelfShown_ || surfaceShelfShown_ || booleanShelfShown_
        || thickenShelfShown_ || approxShelfShown_ || cornerPreviewShown_
        || ShelfShown(Shelf::Array);
    if (!running || operationHost_ == nullptr) {
        return std::string();
    }
    const Shelf current = operationHost_->CurrentShelf();
    if (current == Shelf::None) {
        return std::string();
    }
    return std::string(kachakacha::v2::app::ShelfNameJa(current));
}

//! 状態行の左右と HUD を、いまの状態から書き直す。
void V2MainWindow::RefreshStatusLine()
{
    if (toolLabel_ == nullptr || cursorLabel_ == nullptr) {
        return;
    }
    StatusLineParts parts = BuildStatusLineParts();
    const std::string hint = parts.hintJa;
    parts.hintJa.clear();   // 左の札はモードと道具だけ。案内は真ん中の札が持つ。
    toolLabel_->setText(Text(kachakacha::v2::app::StatusLeftText(parts)));
    cursorLabel_->setText(Text(kachakacha::v2::app::StatusRightText(parts)));
    parts.hintJa = hint;
    if (operationHost_ != nullptr) {
        operationHost_->SetHint(Text(hint));   // 右の欄の見出しの下にも同じ案内
    }
    if (viewport_ != nullptr) {
        std::vector<QString> lines;
        for (const std::string& line : kachakacha::v2::app::HudLines(parts)) {
            lines.push_back(Text(line));
        }
        viewport_->SetHudLines(lines);
    }
}

QString V2MainWindow::StatusRightText() const
{
    return cursorLabel_ == nullptr ? QString() : cursorLabel_->text();
}

QString V2MainWindow::StatusLeftText() const
{
    return toolLabel_ == nullptr ? QString() : toolLabel_->text();
}

//! カーソルが動いた。座標の札だけ書き直す(案内は変えない)。
void V2MainWindow::OnViewportHoverChanged()
{
    if (cursorLabel_ == nullptr) {
        return;
    }
    cursorLabel_->setText(Text(kachakacha::v2::app::StatusRightText(BuildStatusLineParts())));
}

// ---- 測定の重ね道具(C-16) ----

//! 道具を持ち替える前に呼ぶ。測定へ持ち替えるなら、いまの道具を戻り先として覚える。
//! 測定以外へ持ち替えるなら忘れる(人が自分で別の道具を選んだ)。
void V2MainWindow::RememberToolForMeasure(DrawingTool next)
{
    const DrawingTool before = session_->CurrentTool();
    if (next == DrawingTool::Measure) {
        if (before != DrawingTool::Measure) {
            toolBeforeMeasure_ = kachakacha::v2::app::ToolToResumeAfterMeasure(before, next);
        }
        return;
    }
    toolBeforeMeasure_.reset();
}

//! Esc や右クリックで「選択へ戻る」と言われた。測定を重ねていたなら元の道具へ戻す。
//! 戻り先が無ければ、ふだんどおり選択道具へ。
void V2MainWindow::BackToSelectOrResume()
{
    if (session_->CurrentTool() == DrawingTool::Measure && toolBeforeMeasure_.has_value()) {
        const DrawingTool resume = *toolBeforeMeasure_;
        toolBeforeMeasure_.reset();
        SelectTool(resume);
        // 一言は Esc の計画(core の EscapeMessageJa)が出す。ここで重ねて言わない。
        return;
    }
    SelectTool(DrawingTool::Select);
}
