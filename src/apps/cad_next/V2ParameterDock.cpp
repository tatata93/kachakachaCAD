#include "V2ParameterDock.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <string>
#include <utility>

namespace {

[[nodiscard]] QString Text(std::string_view text)
{
    return QString::fromUtf8(std::string(text).c_str());
}

} // namespace

V2ParameterDock::V2ParameterDock(QWidget* parent)
    : QDockWidget(QStringLiteral("数"), parent)
{
    setObjectName(QStringLiteral("parameterDock"));
    values_ = kachakacha::v2::app::DefaultParameters();

    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    rows_ = new QTreeWidget(body);
    rows_->setColumnCount(2);
    rows_->setHeaderLabels({QStringLiteral("何を"), QStringLiteral("いくつ(式も書けます)")});
    rows_->setRootIsDecorated(false);
    layout->addWidget(rows_);
    setWidget(body);

    QObject::connect(rows_, &QTreeWidget::itemChanged, this,
        [this](QTreeWidgetItem* item, int column) { OnItemChanged(item, column); });
    Refresh();
}

void V2ParameterDock::SetDiagnosticSink(std::function<void(const QString&)> sink)
{
    sink_ = std::move(sink);
}

void V2ParameterDock::Refresh()
{
    if (rows_ == nullptr) {
        return;
    }
    // 並べ直す間は、こちらの書き換えを「人が打った」と読まれないようにする。
    rows_->blockSignals(true);
    rows_->clear();
    int index = 0;
    for (const auto& definition : kachakacha::v2::app::ParameterDefinitions()) {
        auto* item = new QTreeWidgetItem(rows_);
        item->setText(0, Text(definition.nameJa));
        item->setText(1, QString::fromStdString(
            kachakacha::v2::app::ParameterTextOf(values_, definition.id)));
        item->setData(0, 32, index);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        ++index;
    }
    rows_->blockSignals(false);
    rows_->resizeColumnToContents(0);
}

void V2ParameterDock::OnItemChanged(QTreeWidgetItem* item, int column)
{
    if (item == nullptr || column != 1) {
        return;
    }
    const int index = item->data(0, 32).toInt();
    const auto& definitions = kachakacha::v2::app::ParameterDefinitions();
    if (index < 0 || index >= static_cast<int>(definitions.size())) {
        return;
    }
    (void)Apply(definitions[static_cast<std::size_t>(index)].id, item->text(1));
}

bool V2ParameterDock::Apply(kachakacha::v2::app::ParameterId id, const QString& text)
{
    const auto next = kachakacha::v2::app::SetParameter(values_, id,
        text.toStdString());
    if (!next.HasValue()) {
        if (sink_) {
            for (const auto& diagnostic : next.Diagnostics()) {
                sink_(QString::fromStdString(diagnostic.code) + QStringLiteral(" ")
                    + QString::fromStdString(diagnostic.summaryJa) + QStringLiteral(" ")
                    + QString::fromStdString(diagnostic.detailsJa));
            }
        }
        // 断ったら前の値へ戻す。消すと、何だったか思い出せなくなる。
        Refresh();
        return false;
    }
    values_ = next.Value();
    Refresh();
    return true;
}

int V2ParameterDock::RowCount() const
{
    return rows_ == nullptr ? 0 : rows_->topLevelItemCount();
}

QString V2ParameterDock::RowName(int row) const
{
    if (rows_ == nullptr || row < 0 || row >= rows_->topLevelItemCount()) {
        return QString();
    }
    return rows_->topLevelItem(row)->text(0);
}

QString V2ParameterDock::RowText(int row) const
{
    if (rows_ == nullptr || row < 0 || row >= rows_->topLevelItemCount()) {
        return QString();
    }
    return rows_->topLevelItem(row)->text(1);
}
