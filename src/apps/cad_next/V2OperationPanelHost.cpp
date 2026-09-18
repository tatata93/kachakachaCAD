#include "V2OperationPanelHost.h"

#include <QComboBox>
#include <QFont>
#include <QLabel>
#include <QObject>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {

QString ShelfText(kachakacha::v2::app::Shelf shelf)
{
    return QString::fromUtf8(kachakacha::v2::app::ShelfNameJa(shelf));
}

} // namespace

V2OperationPanelHost::V2OperationPanelHost(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("currentOperationPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    title_ = new QLabel(QStringLiteral("現在の操作"), this);
    title_->setObjectName(QStringLiteral("currentOperationTitle"));
    QFont titleFont = title_->font();
    titleFont.setBold(true);
    title_->setFont(titleFont);
    layout->addWidget(title_);
    // 見出しの直下に案内(次に何をするか)。道具の欄の共通の形(指示書 C-10)。
    hint_ = new QLabel(this);
    hint_->setObjectName(QStringLiteral("currentOperationHint"));
    hint_->setWordWrap(true);
    hint_->hide();
    layout->addWidget(hint_);

    pageChoice_ = new QComboBox(this);
    pageChoice_->setObjectName(QStringLiteral("relatedOperationChoice"));
    pageChoice_->setToolTip(QStringLiteral("現在の操作に関係する設定を切り替えます。"));
    layout->addWidget(pageChoice_);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("operationPages"));
    layout->addWidget(pages_, 1);

    QObject::connect(pageChoice_, &QComboBox::currentIndexChanged, this,
        [this](int index) { ActivateIndex(index); });
}

void V2OperationPanelHost::AddPage(kachakacha::v2::app::Shelf shelf, QWidget* page)
{
    if (shelf == kachakacha::v2::app::Shelf::None || page == nullptr
        || pageByShelf_.contains(shelf)) {
        return;
    }
    page->setParent(pages_);
    pages_->addWidget(page);
    pageByShelf_.emplace(shelf, page);
}

void V2OperationPanelHost::SetShelves(
    const std::vector<kachakacha::v2::app::Shelf>& shelves)
{
    shownShelves_.clear();
    for (const auto shelf : shelves) {
        if (pageByShelf_.contains(shelf)) {
            shownShelves_.push_back(shelf);
        }
    }

    const QSignalBlocker blocker(pageChoice_);
    pageChoice_->clear();
    for (const auto shelf : shownShelves_) {
        pageChoice_->addItem(ShelfText(shelf));
    }
    pageChoice_->setVisible(shownShelves_.size() > 1);
    ActivateIndex(shownShelves_.empty() ? -1 : 0);
}

bool V2OperationPanelHost::Shows(kachakacha::v2::app::Shelf shelf) const
{
    return std::find(shownShelves_.begin(), shownShelves_.end(), shelf)
        != shownShelves_.end();
}

kachakacha::v2::app::Shelf V2OperationPanelHost::CurrentShelf() const noexcept
{
    return current_;
}

void V2OperationPanelHost::ActivateIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(shownShelves_.size())) {
        current_ = kachakacha::v2::app::Shelf::None;
        title_->setText(QStringLiteral("現在の操作"));
        pages_->setCurrentIndex(-1);
        return;
    }

    current_ = shownShelves_[static_cast<std::size_t>(index)];
    title_->setText(ShelfText(current_));
    pages_->setCurrentWidget(pageByShelf_.at(current_));
    if (pageChoice_->currentIndex() != index) {
        const QSignalBlocker blocker(pageChoice_);
        pageChoice_->setCurrentIndex(index);
    }
}

void V2OperationPanelHost::SetHint(const QString& hintJa)
{
    hint_->setText(hintJa);
    hint_->setVisible(!hintJa.isEmpty());
}

QString V2OperationPanelHost::HintText() const
{
    return hint_->text();
}
