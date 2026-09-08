#include "V2ExportDock.h"

#include <QAction>
#include <QColor>
#include <QLabel>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <string>
#include <string_view>
#include <utility>

namespace {

using kachakacha::v2::app::ExportFormat;
using kachakacha::v2::app::ExportTarget;

//! 行に載せた選択肢を取り出すための場所。
constexpr int kValueRole = 32; // Qt::UserRole

[[nodiscard]] QString Text(std::string_view text)
{
    return QString::fromUtf8(std::string(text).c_str());
}

} // namespace

V2ExportDock::V2ExportDock(QWidget* parent)
    : QDockWidget(QStringLiteral("書き出し"), parent)
{
    setObjectName(QStringLiteral("exportDock"));
    BuildBody();
    Refresh();
}

void V2ExportDock::BuildBody()
{
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    targetView_ = new QTreeWidget(body);
    targetView_->setColumnCount(1);
    targetView_->setHeaderLabels({QStringLiteral("何を出すか")});
    targetView_->setRootIsDecorated(false);
    layout->addWidget(targetView_);

    formatView_ = new QTreeWidget(body);
    formatView_->setColumnCount(1);
    formatView_->setHeaderLabels({QStringLiteral("どの形式で")});
    formatView_->setRootIsDecorated(false);
    layout->addWidget(formatView_);

    pathLabel_ = new QLabel(body);
    pathLabel_->setWordWrap(true);
    layout->addWidget(pathLabel_);

    summaryLabel_ = new QLabel(body);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    messageLabel_ = new QLabel(body);
    messageLabel_->setWordWrap(true);
    layout->addWidget(messageLabel_);

    auto* bar = new QToolBar(body);
    overwriteAction_ = new QAction(QStringLiteral("上書きしてよい"), bar);
    overwriteAction_->setCheckable(true);
    bar->addAction(overwriteAction_);
    runAction_ = new QAction(QStringLiteral("出す"), bar);
    bar->addAction(runAction_);
    layout->addWidget(bar);

    body->setLayout(layout);
    setWidget(body);

    QObject::connect(targetView_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item, int) {
            if (item == nullptr) {
                return;
            }
            const int index = item->data(0, kValueRole).toInt();
            if (index >= 0 && index < static_cast<int>(targetRows_.size())) {
                ChooseTarget(targetRows_[static_cast<std::size_t>(index)].target);
            }
        });
    QObject::connect(formatView_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item, int) {
            if (item == nullptr) {
                return;
            }
            const int index = item->data(0, kValueRole).toInt();
            if (index >= 0 && index < static_cast<int>(formatRows_.size())) {
                ChooseFormat(formatRows_[static_cast<std::size_t>(index)].format);
            }
        });
    QObject::connect(overwriteAction_, &QAction::triggered, this,
        [this](bool checked) { SetOverwrite(checked); });
    QObject::connect(runAction_, &QAction::triggered, this, [this](bool) { RunNow(); });
}

void V2ExportDock::SetDiagnosticSink(std::function<void(const QString&)> sink)
{
    sink_ = std::move(sink);
}

void V2ExportDock::SetContentMaker(
    std::function<kachakacha::v2::base::Result<std::string>(
        const kachakacha::v2::app::ExportRequest&)> maker)
{
    maker_ = std::move(maker);
}

void V2ExportDock::SetCounts(const kachakacha::v2::app::ExportCounts& counts)
{
    counts_ = counts;
    // 数が変わったら選び直す。自分で選んだ対象が出せる間は動かさない(core が決める)。
    state_ = kachakacha::v2::app::RetargetForCounts(state_, counts_);
    if (overwriteAction_ != nullptr) {
        overwriteAction_->setChecked(state_.overwrite);
    }
    Refresh();
}

bool V2ExportDock::ChooseTarget(kachakacha::v2::app::ExportTarget target)
{
    const auto next = kachakacha::v2::app::SetExportPanelTarget(state_, target, counts_);
    if (!next.HasValue()) {
        Report(next.Diagnostics());
        return false;
    }
    state_ = next.Value();
    Refresh();
    return true;
}

bool V2ExportDock::ChooseFormat(kachakacha::v2::app::ExportFormat format)
{
    const auto next = kachakacha::v2::app::SetExportPanelFormat(state_, format);
    if (!next.HasValue()) {
        Report(next.Diagnostics());
        return false;
    }
    state_ = next.Value();
    Refresh();
    return true;
}

void V2ExportDock::ChoosePath(const QString& path)
{
    state_ = kachakacha::v2::app::SetExportPanelPath(state_, path.toStdString());
    if (overwriteAction_ != nullptr) {
        overwriteAction_->setChecked(false);
    }
    Refresh();
}

void V2ExportDock::SetOverwrite(bool overwrite)
{
    state_ = kachakacha::v2::app::SetExportPanelOverwrite(state_, overwrite);
    if (overwriteAction_ != nullptr) {
        overwriteAction_->setChecked(overwrite);
    }
    Refresh();
}

void V2ExportDock::RefreshTargets()
{
    targetRows_ = kachakacha::v2::app::BuildExportTargetRows(counts_);
    targetView_->clear();
    for (std::size_t index = 0; index < targetRows_.size(); ++index) {
        const auto& row = targetRows_[index];
        auto* item = new QTreeWidgetItem(targetView_);
        const bool chosen = row.target == state_.target;
        item->setText(0, QStringLiteral("%1%2")
                .arg(chosen ? QStringLiteral("● ") : QStringLiteral("   "),
                    Text(row.labelJa)));
        item->setData(0, kValueRole, QVariant(static_cast<int>(index)));
        if (!row.selectable) {
            // 選べない行は薄くする。消しはしない。
            item->setForeground(0, QColor(128, 128, 128));
        }
    }
}

void V2ExportDock::RefreshFormats()
{
    formatRows_ = kachakacha::v2::app::BuildExportFormatRows(state_.target);
    formatView_->clear();
    for (std::size_t index = 0; index < formatRows_.size(); ++index) {
        const auto& row = formatRows_[index];
        auto* item = new QTreeWidgetItem(formatView_);
        const bool chosen = row.format == state_.format;
        item->setText(0, QStringLiteral("%1%2")
                .arg(chosen ? QStringLiteral("● ") : QStringLiteral("   "),
                    Text(row.labelJa)));
        item->setData(0, kValueRole, QVariant(static_cast<int>(index)));
        if (!row.selectable) {
            item->setForeground(0, QColor(128, 128, 128));
        }
    }
}

void V2ExportDock::RefreshSummary()
{
    pathLabel_->setText(state_.path.empty()
            ? QStringLiteral("出力先: (まだ決めていません)")
            : QStringLiteral("出力先: %1").arg(QString::fromStdString(state_.path)));
    const QString reason = ReasonText();
    summaryLabel_->setText(reason.isEmpty()
            ? QStringLiteral("%1 出せます。").arg(SummaryText())
            : QStringLiteral("%1 出せません。%2").arg(SummaryText(), reason));
    if (runAction_ != nullptr) {
        runAction_->setEnabled(reason.isEmpty());
    }
}

void V2ExportDock::Refresh()
{
    RefreshTargets();
    RefreshFormats();
    RefreshSummary();
}

int V2ExportDock::TargetRowCount() const
{
    return static_cast<int>(targetRows_.size());
}

QString V2ExportDock::TargetRowText(int row) const
{
    if (row < 0 || row >= TargetRowCount()) {
        return {};
    }
    return targetView_->topLevelItem(row)->text(0);
}

bool V2ExportDock::TargetRowSelectable(int row) const
{
    if (row < 0 || row >= TargetRowCount()) {
        return false;
    }
    return targetRows_[static_cast<std::size_t>(row)].selectable;
}

int V2ExportDock::FormatRowCount() const
{
    return static_cast<int>(formatRows_.size());
}

QString V2ExportDock::FormatRowText(int row) const
{
    if (row < 0 || row >= FormatRowCount()) {
        return {};
    }
    return formatView_->topLevelItem(row)->text(0);
}

bool V2ExportDock::FormatRowSelectable(int row) const
{
    if (row < 0 || row >= FormatRowCount()) {
        return false;
    }
    return formatRows_[static_cast<std::size_t>(row)].selectable;
}

QString V2ExportDock::SummaryText() const
{
    return QString::fromStdString(
        kachakacha::v2::app::ExportSelectionTextJa(state_, counts_));
}

QString V2ExportDock::ReasonText() const
{
    const std::string reason = kachakacha::v2::app::ExportBlockReasonJa(state_, counts_);
    if (!reason.empty()) {
        return QString::fromStdString(reason);
    }
    if (!maker_) {
        // 中身を作る手立てが無いのに「出せます」と言わない。
        return QStringLiteral("EXP-013 書き出せませんでした。");
    }
    return {};
}

bool V2ExportDock::CanRun() const
{
    return ReasonText().isEmpty();
}

bool V2ExportDock::RunNow()
{
    const auto request = kachakacha::v2::app::ToExportRequest(state_, counts_);
    if (!request.HasValue()) {
        Report(request.Diagnostics());
        return false;
    }
    if (!maker_) {
        SetMessage(QStringLiteral("EXP-013 書き出せませんでした。"
                                  "この組合せの中身を作る手立てがありません。"));
        return false;
    }
    const auto order = request.Value();
    const auto outcome = kachakacha::v2::app::RunExport(order,
        [this, order]() { return maker_(order); });
    if (!outcome.HasValue()) {
        Report(outcome.Diagnostics());
        return false;
    }
    SetMessage(QString::fromStdString(
        kachakacha::v2::app::ExportOutcomeTextJa(outcome.Value())));
    Refresh();
    return true;
}

void V2ExportDock::Report(const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics)
{
    for (const auto& diagnostic : diagnostics) {
        const QString text = QStringLiteral("%1 %2")
            .arg(QString::fromStdString(diagnostic.code),
                QString::fromStdString(diagnostic.summaryJa));
        SetMessage(text);
        if (sink_) {
            sink_(text);
        }
    }
}

void V2ExportDock::SetMessage(const QString& text)
{
    lastMessage_ = text;
    if (messageLabel_ != nullptr) {
        messageLabel_->setText(text);
    }
}
