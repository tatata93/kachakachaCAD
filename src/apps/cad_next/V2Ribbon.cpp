#include "V2Ribbon.h"

#include <QAction>
#include <QHBoxLayout>
#include <QObject>
#include <QSizePolicy>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

using kachakacha::v2::app::RibbonCategoriesFor;
using kachakacha::v2::app::RibbonTool;
using kachakacha::v2::app::UiMode;

namespace {

[[nodiscard]] QString Text(std::string_view value)
{
    return QString::fromUtf8(std::string(value).c_str());
}

} // namespace

V2Ribbon::V2Ribbon(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ribbon"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    categoryRow_ = new QWidget(this);
    categoryRow_->setObjectName(QStringLiteral("ribbonCategories"));
    categoryLayout_ = new QHBoxLayout(categoryRow_);
    categoryLayout_->setContentsMargins(4, 2, 4, 0);
    categoryLayout_->setSpacing(0);
    // 右の余白は最初に 1 つだけ置く。並べ直すたびに足すと余白が溜まり、
    // 帯が右へ寄っていった(PC 画面 2026-09-19)。ボタンは余白の前へ差し込む。
    categoryLayout_->addStretch(1);
    layout->addWidget(categoryRow_);
    toolRow_ = new QWidget(this);
    toolRow_->setObjectName(QStringLiteral("ribbonTools"));
    toolLayout_ = new QHBoxLayout(toolRow_);
    toolLayout_->setContentsMargins(6, 3, 6, 3);
    toolLayout_->setSpacing(4);
    toolLayout_->addStretch(1);   // 上と同じ
    layout->addWidget(toolRow_);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void V2Ribbon::SetActionLookup(std::function<QAction*(std::string_view)> lookup)
{
    lookup_ = std::move(lookup);
}

void V2Ribbon::SetVariantHandler(std::function<void(const RibbonTool&)> handler)
{
    variantHandler_ = std::move(handler);
}

void V2Ribbon::SetBlockedHandler(std::function<void(const RibbonTool&)> handler)
{
    blockedHandler_ = std::move(handler);
}

void V2Ribbon::ShowMode(UiMode mode)
{
    remembered_[mode_] = current_;
    mode_ = mode;
    for (QToolButton* button : categoryButtons_) {
        categoryLayout_->removeWidget(button);
        delete button;
    }
    categoryButtons_.clear();
    const auto& categories = RibbonCategoriesFor(mode);
    for (std::size_t index = 0; index < categories.size(); ++index) {
        auto* button = new QToolButton(categoryRow_);
        button->setText(Text(categories[index].labelJa));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setObjectName(QStringLiteral("ribbonCategory"));
        const int at = static_cast<int>(index);
        QObject::connect(button, &QToolButton::clicked, this, [this, at] { ShowCategory(at); });
        categoryLayout_->insertWidget(static_cast<int>(categoryButtons_.size()), button);
        // 親が見えたあとに作った子は、show() を呼ぶまで(イベントループが回るまで)見えない。
        // 試験は押した直後に isVisible を見るので、ここで出す(PC 自己試験 HP-RB 2026-09-19)。
        button->show();
        categoryButtons_.push_back(button);
    }
    const auto seen = remembered_.find(mode);
    current_ = seen == remembered_.end() ? 0 : seen->second;
    if (current_ < 0 || current_ >= static_cast<int>(categories.size())) {
        current_ = 0;
    }
    ShowCategory(current_);
}

void V2Ribbon::ShowCategory(int index)
{
    const auto& categories = RibbonCategoriesFor(mode_);
    if (categories.empty()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(categories.size())) {
        index = 0;
    }
    current_ = index;
    for (std::size_t at = 0; at < categoryButtons_.size(); ++at) {
        categoryButtons_[at]->setChecked(static_cast<int>(at) == index);
    }
    RebuildTools();
}

void V2Ribbon::RebuildTools()
{
    for (ToolEntry& entry : toolButtons_) {
        toolLayout_->removeWidget(entry.button);
        delete entry.button;
        delete entry.ownAction;
    }
    toolButtons_.clear();
    const auto& categories = RibbonCategoriesFor(mode_);
    if (current_ < 0 || current_ >= static_cast<int>(categories.size())) {
        return;
    }
    // 正本の道具を先に、「その他」(正本に無い既存の道具)を後ろに。
    for (const bool extra : {false, true}) {
        for (const RibbonTool& tool : categories[static_cast<std::size_t>(current_)].tools) {
            if (tool.extra != extra) {
                continue;
            }
            auto* button = new QToolButton(toolRow_);
            button->setObjectName(QStringLiteral("ribbonTool"));
            button->setAutoRaise(true);
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            ToolEntry entry;
            entry.button = button;
            entry.tool = tool;
            QAction* action = nullptr;
            const bool plain = !tool.Blocked() && !tool.surfaceMethod.has_value()
                && !tool.measureMode.has_value() && lookup_;
            if (plain) {
                action = lookup_(tool.commandId);
            }
            if (action == nullptr) {
                // 作り方つき、または押せない道具。帯が自分の QAction を持つ。
                action = new QAction(Text(tool.labelJa), this);
                entry.ownAction = action;
                if (tool.Blocked()) {
                    action->setEnabled(false);
                    action->setToolTip(Text(tool.blockedReasonJa));
                    action->setStatusTip(Text(tool.blockedReasonJa));
                } else {
                    action->setCheckable(true);
                    const RibbonTool copy = tool;
                    QObject::connect(action, &QAction::triggered, this, [this, copy] {
                        if (variantHandler_) {
                            variantHandler_(copy);
                        }
                    });
                }
            }
            button->setDefaultAction(action);
            button->setText(Text(tool.labelJa));   // 台帳の名前ではなく正本の言葉を出す
            if (tool.Blocked()) {
                button->setToolTip(Text(tool.blockedReasonJa));
            }
            toolLayout_->insertWidget(static_cast<int>(toolButtons_.size()), button);
            button->show();   // 上と同じ理由
            toolButtons_.push_back(entry);
        }
    }
    SetCurrentSurfaceMethod(currentSurfaceMethod_, currentSurfaceMethod_ >= 0);
    SetCurrentMeasureMode(currentMeasureMode_, currentMeasureMode_ >= 0);
}

void V2Ribbon::RevealCommand(std::string_view commandId)
{
    const auto& categories = RibbonCategoriesFor(mode_);
    const auto holds = [&](std::size_t index) {
        for (const RibbonTool& tool : categories[index].tools) {
            if (tool.commandId == commandId) {
                return true;
            }
        }
        return false;
    };
    // いま見ているカテゴリにその命令があれば、そのまま(もう見えている)。先に並ぶ別のカテゴリへ
    // 飛ぶと、測定の「面積」を押したのに注記の「寸法」の段へ替わっていた(撮影 ui/17、2026-09-22)。
    if (current_ >= 0 && static_cast<std::size_t>(current_) < categories.size()
        && holds(static_cast<std::size_t>(current_))) {
        return;
    }
    for (std::size_t index = 0; index < categories.size(); ++index) {
        if (holds(index)) {
            ShowCategory(static_cast<int>(index));
            return;
        }
    }
}

void V2Ribbon::SetCurrentSurfaceMethod(int method, bool active)
{
    currentSurfaceMethod_ = active ? method : -1;
    for (ToolEntry& entry : toolButtons_) {
        if (entry.ownAction != nullptr && entry.tool.surfaceMethod.has_value()) {
            entry.ownAction->setChecked(active && *entry.tool.surfaceMethod == method);
        }
    }
}

void V2Ribbon::SetCurrentMeasureMode(int mode, bool active)
{
    currentMeasureMode_ = active ? mode : -1;
    for (ToolEntry& entry : toolButtons_) {
        if (entry.ownAction != nullptr && entry.tool.measureMode.has_value()) {
            entry.ownAction->setChecked(active && *entry.tool.measureMode == mode);
        }
    }
}

int V2Ribbon::CategoryCount() const
{
    return static_cast<int>(categoryButtons_.size());
}

QString V2Ribbon::CategoryLabel(int index) const
{
    if (index < 0 || index >= static_cast<int>(categoryButtons_.size())) {
        return QString();
    }
    return categoryButtons_[static_cast<std::size_t>(index)]->text();
}

bool V2Ribbon::ClickCategory(const QString& labelJa)
{
    for (QToolButton* button : categoryButtons_) {
        if (button->text() == labelJa && button->isVisible()) {
            button->click();
            return true;
        }
    }
    return false;
}

QToolButton* V2Ribbon::ToolButton(const QString& labelJa) const
{
    for (const ToolEntry& entry : toolButtons_) {
        if (entry.button->text() == labelJa) {
            return entry.button;
        }
    }
    return nullptr;
}

bool V2Ribbon::ClickTool(const QString& labelJa)
{
    QToolButton* button = ToolButton(labelJa);
    if (button == nullptr || !button->isVisible() || !button->isEnabled()) {
        for (const ToolEntry& entry : toolButtons_) {
            if (entry.button == button && entry.tool.Blocked() && blockedHandler_) {
                blockedHandler_(entry.tool);   // 押せない理由を言う(押したことにはしない)
            }
        }
        return false;
    }
    button->click();
    return true;
}

std::vector<QString> V2Ribbon::ToolLabels() const
{
    std::vector<QString> labels;
    for (const ToolEntry& entry : toolButtons_) {
        labels.push_back(entry.button->text());
    }
    return labels;
}

bool V2Ribbon::ToolEnabled(const QString& labelJa) const
{
    QToolButton* button = ToolButton(labelJa);
    return button != nullptr && button->isVisible() && button->isEnabled();
}

QString V2Ribbon::ToolTip(const QString& labelJa) const
{
    QToolButton* button = ToolButton(labelJa);
    return button == nullptr ? QString() : button->toolTip();
}

bool V2Ribbon::CommandAvailable(std::string_view commandId) const
{
    const auto* tool = kachakacha::v2::app::FindRibbonTool(mode_, commandId);
    return tool != nullptr && !tool->Blocked();
}

bool V2Ribbon::ToolsFitInWidth() const
{
    for (const ToolEntry& entry : toolButtons_) {
        if (entry.button->geometry().right() > toolRow_->width()) {
            return false;
        }
    }
    return true;
}
