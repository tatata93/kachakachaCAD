#pragma once

//! 書き出しの棚(ui-workflows §11、WP-11)。
//!
//! 判断は core の ExportPanel にある。ここは
//!   - 対象と形式を並べる
//!   - 押されたら core に伝える
//!   - core が返した理由をそのまま出す
//! の3つだけをする。組合せの表も、押せるかどうかの理由も、ここでは作らない。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include "kachakacha/app/ExportPanel.h"

#include <QDockWidget>
#include <QString>

#include <functional>
#include <vector>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QAction;
class QToolBar;

class V2ExportDock final : public QDockWidget {
public:
    explicit V2ExportDock(QWidget* parent);

    //! いま何がいくつあるか。変わるたびに呼ぶ。並びは作り直す。
    void SetCounts(const kachakacha::v2::app::ExportCounts& counts);
    [[nodiscard]] const kachakacha::v2::app::ExportCounts& Counts() const { return counts_; }
    [[nodiscard]] const kachakacha::v2::app::ExportPanelState& State() const { return state_; }

    //! 知らせの出し先。断られた理由はここへ流す。
    void SetDiagnosticSink(std::function<void(const QString&)> sink);
    //! 中身を作る側。対象と形式ごとに用意する。無ければ出せない。
    void SetContentMaker(std::function<kachakacha::v2::base::Result<std::string>(
        const kachakacha::v2::app::ExportRequest&)> maker);

    //! 選ぶ。断られたら false を返し、状態は変えない。
    bool ChooseTarget(kachakacha::v2::app::ExportTarget target);
    bool ChooseFormat(kachakacha::v2::app::ExportFormat format);
    void ChoosePath(const QString& path);
    void SetOverwrite(bool overwrite);

    //! 並びに出ている行。試験から見る。
    [[nodiscard]] int TargetRowCount() const;
    [[nodiscard]] QString TargetRowText(int row) const;
    [[nodiscard]] bool TargetRowSelectable(int row) const;
    [[nodiscard]] int FormatRowCount() const;
    [[nodiscard]] QString FormatRowText(int row) const;
    [[nodiscard]] bool FormatRowSelectable(int row) const;

    //! いまの選びの一文と、押せないときの理由。
    [[nodiscard]] QString SummaryText() const;
    [[nodiscard]] QString ReasonText() const;
    [[nodiscard]] bool CanRun() const;

    //! 出す。検査に通ったときだけ中身を作らせる。
    bool RunNow();
    //! 直前に出した一文。
    [[nodiscard]] QString LastMessage() const { return lastMessage_; }

private:
    void BuildBody();
    void RefreshTargets();
    void RefreshFormats();
    void RefreshSummary();
    void Refresh();
    void Report(const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics);
    void SetMessage(const QString& text);

    kachakacha::v2::app::ExportCounts counts_;
    kachakacha::v2::app::ExportPanelState state_;
    std::vector<kachakacha::v2::app::ExportTargetRow> targetRows_;
    std::vector<kachakacha::v2::app::ExportFormatRow> formatRows_;
    std::function<void(const QString&)> sink_;
    std::function<kachakacha::v2::base::Result<std::string>(
        const kachakacha::v2::app::ExportRequest&)> maker_;
    QTreeWidget* targetView_ = nullptr;
    QTreeWidget* formatView_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* messageLabel_ = nullptr;
    QAction* overwriteAction_ = nullptr;
    QAction* runAction_ = nullptr;
    QString lastMessage_;
};
