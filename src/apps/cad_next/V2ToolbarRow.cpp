#include "V2ToolbarRow.h"

#include <QEvent>
#include <QAction>
#include <QHBoxLayout>
#include <QLayout>
#include <QMenu>
#include <QResizeEvent>
#include <QSize>
#include <QSizePolicy>
#include <QString>
#include <QToolButton>
#include <QWidget>

#include <algorithm>

V2ToolbarRow::V2ToolbarRow(const QString& overflowLabel, QWidget* parent)
    : QWidget(parent), row_(new QHBoxLayout(this)), more_(new QToolButton(this))
{
    row_->setContentsMargins(4, 2, 4, 2);
    row_->setSpacing(3);
    row_->setSizeConstraint(QLayout::SetNoConstraint);
    row_->addWidget(more_);
    row_->addStretch();
    more_->setText(overflowLabel);
    more_->setObjectName(QStringLiteral("toolbarOverflowButton"));
    more_->setAccessibleName(overflowLabel);
    more_->setToolTip(overflowLabel);
    more_->setMinimumHeight(28);
    more_->setFocusPolicy(Qt::StrongFocus);
    more_->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(more_);
    menu->setObjectName(QStringLiteral("toolbarOverflowMenu"));
    menu->setStyleSheet(QStringLiteral("QMenu { menu-scrollable: 1; }"));
    more_->setMenu(menu);
    connect(menu, &QMenu::aboutToShow, this, [this] { PopulateMenu(); });
    more_->hide();
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void V2ToolbarRow::Add(QWidget* widget, QAction* action)
{
    row_->insertWidget(static_cast<int>(entries_.size()), widget);
    entries_.push_back({widget, action});
}

void V2ToolbarRow::Clear()
{
    more_->menu()->close();
    more_->menu()->clear();
    for (const auto& entry : entries_) {
        row_->removeWidget(entry.widget);
        entry.widget->hide();
        entry.widget->deleteLater();
    }
    entries_.clear();
    shown_ = 0;
}

QSize V2ToolbarRow::minimumSizeHint() const
{
    const auto margins = row_->contentsMargins();
    return more_->sizeHint() + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

QSize V2ToolbarRow::sizeHint() const
{
    return row_->sizeHint().expandedTo(minimumSizeHint());
}

int V2ToolbarRow::WidthOf(const QWidget& widget)
{
    return std::max(widget.sizeHint().width(), widget.minimumWidth());
}

void V2ToolbarRow::Relayout()
{
    if (entries_.empty()) {
        more_->hide();
        return;
    }
    const auto margins = row_->contentsMargins();
    const int gap = row_->spacing();
    const int available = width() - margins.left() - margins.right() + gap;
    int total = 0;
    for (const auto& entry : entries_)
        total += WidthOf(*entry.widget) + gap;

    std::size_t count = entries_.size();
    if (total > available) {
        int used = more_->sizeHint().width() + gap;
        count = 0;
        while (count < entries_.size()) {
            used += WidthOf(*entries_[count].widget) + gap;
            if (used > available) break;
            ++count;
        }
    }
    const bool overflow = count < entries_.size();
    if (count != shown_ || overflow == more_->isHidden()) more_->menu()->close();
    shown_ = count;
    bool moveFocus = false;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        auto* widget = entries_[i].widget;
        if (i >= count && !widget->isHidden()) {
            auto* focus = focusWidget();
            moveFocus |= focus && (focus == widget || widget->isAncestorOf(focus));
        }
        const bool visible = i < count;
        if (widget->isHidden() == visible) widget->setVisible(visible);
    }
    if (more_->isHidden() == overflow) more_->setVisible(overflow);
    row_->activate();
    if (moveFocus && overflow) more_->setFocus(Qt::OtherFocusReason);
}

void V2ToolbarRow::PopulateMenu()
{
    auto* menu = more_->menu();
    menu->clear();
    for (std::size_t i = shown_; i < entries_.size(); ++i) menu->addAction(entries_[i].action);
}

bool V2ToolbarRow::event(QEvent* event)
{
    const bool handled = QWidget::event(event);
    if (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show) {
        Relayout();
        updateGeometry();
    }
    return handled;
}

void V2ToolbarRow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    Relayout();
}
