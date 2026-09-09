#include "V2MeasureDock.h"

#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

namespace {

[[nodiscard]] QString Text(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

} // namespace

V2MeasureDock::V2MeasureDock(QWidget* parent)
    : QDockWidget(QStringLiteral("測る"), parent)
{
    setObjectName(QStringLiteral("measureDock"));
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    summary_ = new QLabel(body);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    rows_ = new QTreeWidget(body);
    rows_->setColumnCount(2);
    rows_->setHeaderLabels({QStringLiteral("何を"), QStringLiteral("いくつ")});
    rows_->setRootIsDecorated(false);
    layout->addWidget(rows_);

    setWidget(body);
    Refresh();
}

void V2MeasureDock::SetRequest(const kachakacha::v2::app::MeasureRequest& request)
{
    request_ = request;
    Refresh();
}

void V2MeasureDock::Refresh()
{
    if (rows_ == nullptr || summary_ == nullptr) {
        return;
    }
    summary_->setText(Text(kachakacha::v2::app::MeasureSummaryJa(request_)));
    rows_->clear();
    for (const auto& row : kachakacha::v2::app::BuildMeasureRows(request_)) {
        auto* item = new QTreeWidgetItem(rows_);
        item->setText(0, Text(row.labelJa));
        item->setText(1, Text(row.valueJa));
    }
    rows_->resizeColumnToContents(0);
}

int V2MeasureDock::RowCount() const
{
    return rows_ == nullptr ? 0 : rows_->topLevelItemCount();
}

QString V2MeasureDock::RowLabel(int row) const
{
    if (rows_ == nullptr || row < 0 || row >= rows_->topLevelItemCount()) {
        return QString();
    }
    return rows_->topLevelItem(row)->text(0);
}

QString V2MeasureDock::RowValue(int row) const
{
    if (rows_ == nullptr || row < 0 || row >= rows_->topLevelItemCount()) {
        return QString();
    }
    return rows_->topLevelItem(row)->text(1);
}

QString V2MeasureDock::SummaryText() const
{
    return summary_ == nullptr ? QString() : summary_->text();
}
