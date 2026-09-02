#include "CollapsibleSection.h"

#include <QToolButton>
#include <QVBoxLayout>

CollapsibleSection::CollapsibleSection(
    const QString& title,
    QWidget* content,
    bool expanded,
    QWidget* parent)
    : QWidget(parent)
    , title_(title)
    , content_(content)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    header_ = new QToolButton;
    header_->setText(title);
    header_->setCheckable(true);
    header_->setChecked(expanded);
    header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    header_->setAutoRaise(true);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header_->setStyleSheet(
        "QToolButton {"
        " text-align: left; font-weight: 600; color: #26323a;"
        " background: #e8ebed; border: 1px solid #c7cdd2; border-radius: 3px;"
        " padding: 5px 8px; }"
        "QToolButton:hover { background: #dde3e6; }"
        "QToolButton:checked { background: #e8ebed; }");
    layout->addWidget(header_);

    content_->setVisible(expanded);
    layout->addWidget(content_);

    connect(header_, &QToolButton::toggled, this, [this](bool checked) {
        content_->setVisible(checked);
        header_->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    });
}

void CollapsibleSection::SetExpanded(bool expanded)
{
    header_->setChecked(expanded);
}

bool CollapsibleSection::IsExpanded() const
{
    return header_->isChecked();
}

void CollapsibleSection::ExpandAncestors(QWidget* widget)
{
    for (QWidget* current = widget; current != nullptr; current = current->parentWidget()) {
        // moc を使わないため qobject_cast ではなく RTTI で判定する。
        if (auto* section = dynamic_cast<CollapsibleSection*>(current)) {
            section->SetExpanded(true);
        }
    }
}
