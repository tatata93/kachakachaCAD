#include "V2MeasureDock.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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

    // 測り方。V1 と同じ 2点間 / 3点角度 / 要素 に、V2 の「選んだものから」を足す。
    mode_ = new QComboBox(body);
    for (const auto mode : kachakacha::v2::app::MeasureModes()) {
        mode_->addItem(QString::fromUtf8(
            std::string(kachakacha::v2::app::MeasureModeNameJa(mode)).c_str()));
    }
    layout->addWidget(mode_);
    QObject::connect(mode_, &QComboBox::currentIndexChanged, this, [this] {
        if (modeChanged_) {
            modeChanged_();
        }
    });
    summary_ = new QLabel(body);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    rows_ = new QTreeWidget(body);
    rows_->setColumnCount(2);
    rows_->setHeaderLabels({QStringLiteral("何を"), QStringLiteral("いくつ")});
    rows_->setRootIsDecorated(false);
    layout->addWidget(rows_);

    // 寸法を残す(V1 と同じ)。名前を付けて文書へ。
    auto* keepRow = new QWidget(body);
    auto* keepLayout = new QHBoxLayout(keepRow);
    keepLayout->setContentsMargins(0, 0, 0, 0);
    name_ = new QLineEdit(keepRow);
    name_->setPlaceholderText(QStringLiteral("寸法の名前(空なら測り方の名前)"));
    keep_ = new QPushButton(QStringLiteral("寸法を残す"), keepRow);
    clear_ = new QPushButton(QStringLiteral("測定を消去"), keepRow);
    keepLayout->addWidget(name_);
    keepLayout->addWidget(keep_);
    keepLayout->addWidget(clear_);
    layout->addWidget(keepRow);
    kept_ = new QLabel(body);
    layout->addWidget(kept_);
    QObject::connect(keep_, &QPushButton::clicked, this, [this] { PressKeep(); });
    QObject::connect(clear_, &QPushButton::clicked, this, [this] { PressClear(); });

    setWidget(body);
    Refresh();
}

kachakacha::v2::app::MeasureMode V2MeasureDock::Mode() const
{
    const auto& modes = kachakacha::v2::app::MeasureModes();
    const int index = mode_ == nullptr ? 0 : mode_->currentIndex();
    return index >= 0 && index < static_cast<int>(modes.size())
        ? modes[static_cast<std::size_t>(index)]
        : kachakacha::v2::app::MeasureMode::Selection;
}

void V2MeasureDock::SetMode(kachakacha::v2::app::MeasureMode mode)
{
    const auto& modes = kachakacha::v2::app::MeasureModes();
    for (std::size_t index = 0; index < modes.size(); ++index) {
        if (modes[index] == mode) {
            mode_->setCurrentIndex(static_cast<int>(index));
        }
    }
}

void V2MeasureDock::SetModeChangedHandler(std::function<void()> handler)
{
    modeChanged_ = std::move(handler);
}

QString V2MeasureDock::DimensionName() const
{
    return name_ == nullptr ? QString() : name_->text().trimmed();
}

void V2MeasureDock::SetDimensionName(const QString& name)
{
    name_->setText(name);
}

void V2MeasureDock::SetKeepHandler(std::function<void()> handler)
{
    keepHandler_ = std::move(handler);
}

void V2MeasureDock::PressKeep()
{
    if (keepHandler_) {
        keepHandler_();
    }
}

void V2MeasureDock::SetClearHandler(std::function<void()> handler)
{
    clearHandler_ = std::move(handler);
}

void V2MeasureDock::PressClear()
{
    if (clearHandler_) {
        clearHandler_();
    }
}

void V2MeasureDock::SetKeptCount(int count)
{
    kept_->setText(count == 0 ? QString()
                              : QStringLiteral("残した寸法: %1 つ(文書に入っています)").arg(count));
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
