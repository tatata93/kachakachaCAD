#include <QListWidgetItem>
#include <QObject>
#include <QSize>
#include <QString>
#include <QWidget>
#include "V2BooleanDock.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <utility>

using kachakacha::v2::app::BooleanKind;

void V2BooleanDock::BuildContactRows(QVBoxLayout* layout)
{
    swap_ = new QPushButton(QStringLiteral("対象と相手を入れ替える"), widget());
    swap_->setObjectName(QStringLiteral("contactSwap"));
    contactWire_ = new QCheckBox(QStringLiteral("境界ワイヤーも作る（独立コピー）"), widget());
    contactWire_->setObjectName(QStringLiteral("contactWireAlso"));
    contactHint_ = new QLabel(widget());
    contactHint_->setWordWrap(true);
    contactRegions_ = new QListWidget(widget());
    contactRegions_->setObjectName(QStringLiteral("contactRegions"));
    contactRegions_->setMaximumHeight(180);
    layout->addWidget(swap_);
    layout->addWidget(contactWire_);
    layout->addWidget(contactHint_);
    layout->addWidget(contactRegions_);
    QObject::connect(swap_, &QPushButton::clicked, this, [this] { if (contactSwap_) contactSwap_(); });
    QObject::connect(contactWire_, &QCheckBox::toggled, this, [this] {
        if (!loading_ && contactUpdate_) contactUpdate_();
    });
    QObject::connect(contactRegions_, &QListWidget::itemChanged, this, [this] {
        if (!loading_ && contactUpdate_) contactUpdate_();
    });
    QObject::connect(contactRegions_, &QListWidget::currentRowChanged, this, [this] {
        if (!loading_ && contactUpdate_) contactUpdate_();
    });
    ShowContactOptions(BooleanKind::Add);
}

void V2BooleanDock::SetContactHandlers(std::function<void()> update, std::function<void()> swap)
{
    contactUpdate_ = std::move(update);
    contactSwap_ = std::move(swap);
}

void V2BooleanDock::ShowContactOptions(BooleanKind kind)
{
    kind_ = kind;
    const bool wire = kind == BooleanKind::ContactWire;
    const bool contact = wire || kind == BooleanKind::TrimOverlap || kind == BooleanKind::SplitOverlap;
    add_->setVisible(!contact);
    cut_->setVisible(!contact);
    intersect_->setVisible(!contact);
    operationTitle_->setVisible(!contact);
    targetLabel_->setText(contact ? (wire ? QStringLiteral("対象") : QStringLiteral("加工する部品")) : QStringLiteral("土台"));
    swap_->setVisible(contact);
    contactWire_->setVisible(contact && !wire);
    contactRegions_->setVisible(contact);
    contactHint_->setVisible(contact);
    contactHint_->setText(wire
        ? QStringLiteral("作る境界にチェック。元の部品は両方残ります。ワイヤーは独立コピーです。")
        : kind == BooleanKind::TrimOverlap
            ? QStringLiteral("重なりごとに削る側を選びます。A=対象、B=相手。塗りは選択領域、線は完成形です。")
            : QStringLiteral("残す領域にチェック。相手の部品は残ります。外すとプレビューから消えます。"));
    setWindowTitle(QString::fromUtf8(kachakacha::v2::app::BooleanOperationLabelJa(kind).data()));
}

bool V2BooleanDock::WantsContactWire() const
{
    return kind_ == BooleanKind::ContactWire || contactWire_->isChecked();
}

void V2BooleanDock::SetContactRegions(const QString& key, const std::vector<QString>& names)
{
    if (key == contactKey_ && contactRegions_->count() == static_cast<int>(names.size())) return;
    contactKey_ = key;
    const bool blocked = contactRegions_->blockSignals(true);
    contactRegions_->clear();
    for (const auto& name : names) {
        auto* item = new QListWidgetItem(name, contactRegions_);
        if (kind_ == BooleanKind::TrimOverlap) {
            auto* choice = new QComboBox(contactRegions_);
            choice->addItems({QStringLiteral("残す"), QStringLiteral("Aから削る"), QStringLiteral("Bから削る"), QStringLiteral("両方から削る")});
            choice->setCurrentIndex(1);
            choice->setToolTip(name);
            item->setText(name + QStringLiteral("    "));
            item->setSizeHint(QSize(240, 54));
            // Put the choice beneath its descriptive label.
            auto* body = new QWidget(contactRegions_);
            auto* row = new QVBoxLayout(body); row->setContentsMargins(2, 0, 2, 0);
            row->addWidget(new QLabel(name, body)); row->addWidget(choice);
            contactRegions_->setItemWidget(item, body);
            QObject::connect(choice, &QComboBox::currentIndexChanged, this, [this] { if (!loading_ && contactUpdate_) contactUpdate_(); });
        } else {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        }
    }
    contactRegions_->blockSignals(blocked);
}

std::vector<bool> V2BooleanDock::ContactRegions() const
{
    std::vector<bool> result;
    for (int i = 0; i < contactRegions_->count(); ++i) result.push_back(contactRegions_->item(i)->checkState() == Qt::Checked);
    return result;
}

std::vector<int> V2BooleanDock::ContactRemovals() const
{
    std::vector<int> result;
    for (int i = 0; i < contactRegions_->count(); ++i) {
        auto* body = contactRegions_->itemWidget(contactRegions_->item(i));
        auto* choice = body == nullptr ? nullptr : body->findChild<QComboBox*>();
        result.push_back(choice == nullptr ? 0 : choice->currentIndex());
    }
    return result;
}
int V2BooleanDock::ContactRegionIndex() const { return contactRegions_->currentRow(); }
void V2BooleanDock::SelectContactRegion(int index) { contactRegions_->setCurrentRow(index); }

void V2BooleanDock::SetContactRemoval(int index, int removal)
{
    if(index<0 || index>=contactRegions_->count())return;
    auto* body=contactRegions_->itemWidget(contactRegions_->item(index));
    auto* combo=body==nullptr ? nullptr : body->findChild<QComboBox*>();
    if(combo==nullptr)return;
    const bool blocked=combo->blockSignals(true);combo->setCurrentIndex(removal);combo->blockSignals(blocked);
}
