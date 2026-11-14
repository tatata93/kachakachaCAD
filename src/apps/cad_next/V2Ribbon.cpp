#include "V2Ribbon.h"
#include "V2ToolbarRow.h"
#include "V2ToolIcons.h"
#include "kachakacha/app/CommandCatalog.h"
#include <QSize>

#include <QAction>
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

QAction* ToolProxy(QAction* source, const QString& label, QWidget* parent, std::function<void()> run)
{
    auto* action = new QAction(label, parent);
    const auto sync = [source, action] {
        action->setCheckable(source->isCheckable());
        action->setChecked(source->isChecked());
        action->setEnabled(source->property("toolAvailable").isValid()?source->property("toolAvailable").toBool():source->isEnabled());
        action->setToolTip(source->toolTip());
    };
    sync();
    QObject::connect(source, &QAction::changed, action, sync);
    QObject::connect(action, &QAction::triggered, action, [run] { run(); });
    return action;
}

} // namespace

V2Ribbon::V2Ribbon(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ribbon"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    categoryRow_ = new V2ToolbarRow(QStringLiteral("カテゴリ"), this);
    categoryRow_->setObjectName(QStringLiteral("ribbonCategories"));
    layout->addWidget(categoryRow_);
    toolRow_ = new V2ToolbarRow(QStringLiteral("その他"), this);
    toolRow_->setObjectName(QStringLiteral("ribbonTools"));
    layout->addWidget(toolRow_);
    categoryRow_->setStyleSheet(QStringLiteral(
        "QToolButton#ribbonCategory { border: 1px solid palette(mid); border-bottom: 3px solid palette(mid);"
        " border-top-left-radius: 4px; border-top-right-radius: 4px; padding: 3px 8px; background: palette(window); }"
        "QToolButton#ribbonCategory:checked { border-bottom: 3px solid palette(highlight); font-weight: bold;"
        " background: palette(base); color: palette(text); }"
        "QToolButton#ribbonCategory:hover { background: palette(light); }"));
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
    if(mode==mode_&&!categoryButtons_.empty())return;
    remembered_[mode_] = current_;
    mode_ = mode;
    categoryRow_->Clear();
    categoryButtons_.clear();
    const auto& categories = RibbonCategoriesFor(mode);
    for (std::size_t index = 0; index < categories.size(); ++index) {
        auto* button = new QToolButton(categoryRow_);
        auto* action = new QAction(Text(categories[index].labelJa), button);
        action->setCheckable(true);
        button->setDefaultAction(action);
        button->setAutoRaise(false);
        button->setMinimumHeight(32);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setObjectName(QStringLiteral("ribbonCategory"));
        const int at = static_cast<int>(index);
        QObject::connect(action, &QAction::triggered, this, [this, at] { ShowCategory(at); });
        categoryRow_->Add(button, action);
        // 親が見えたあとに作った子は、show() を呼ぶまで(イベントループが回るまで)見えない。
        // 試験は押した直後に isVisible を見るので、ここで出す(PC 自己試験 HP-RB 2026-09-19)。
        button->show();
        categoryButtons_.push_back(button);
    }
    const auto seen = remembered_.find(mode);
    int chosen = seen == remembered_.end() ? 0 : seen->second;
    if (chosen < 0 || chosen >= static_cast<int>(categories.size())) {
        chosen = 0;
    }
    current_=-1; // A new mode must rebuild even when its category number matches.
    ShowCategory(chosen);
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
    const bool reuse=index==current_&&!toolButtons_.empty();
    current_ = index;
    for (std::size_t at = 0; at < categoryButtons_.size(); ++at) {
        categoryButtons_[at]->defaultAction()->setChecked(static_cast<int>(at) == index);
    }
    if(!reuse)RebuildTools();
}

void V2Ribbon::RebuildTools()
{
    toolRow_->Clear();
    for (ToolEntry& entry : toolButtons_) {
        if (entry.ownAction != nullptr) entry.ownAction->deleteLater();
    }
    toolButtons_.clear();
    const auto& categories = RibbonCategoriesFor(mode_);
    if (current_ < 0 || current_ >= static_cast<int>(categories.size())) {
        return;
    }
    // 正本の道具を先に、「その他」(正本に無い既存の道具)を後ろに。
    for (const bool extra : {false, true}) {
        for (const RibbonTool& tool : categories[static_cast<std::size_t>(current_)].tools) {
            if(tool.Blocked())continue;
            if (tool.extra != extra) {
                continue;
            }
            auto* button = new QToolButton(toolRow_);
            button->setObjectName(QStringLiteral("ribbonTool"));
            button->setAutoRaise(false);
            button->setFocusPolicy(Qt::StrongFocus);
            ToolEntry entry;
            entry.button = button;
            entry.tool = tool;
            QAction* action = nullptr;
            const bool plain = !tool.Blocked() && !tool.surfaceMethod.has_value()
                && !tool.measureMode.has_value() && lookup_;
            if (plain) {
                if (auto* source = lookup_(tool.commandId)) {
                    action = ToolProxy(source, Text(tool.labelJa), this, [this, tool] {
                        if (variantHandler_) variantHandler_(tool);
                    });
                    entry.ownAction = action;
                }
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
                    if(const auto* command=kachakacha::v2::app::FindCommand(tool.commandId))
                        action->setToolTip(Text(tool.labelJa)+QStringLiteral(" — ")+Text(command->operationGuideJa));
                    const RibbonTool copy = tool;
                    QObject::connect(action, &QAction::triggered, this, [this, copy] {
                        if (variantHandler_) {
                            variantHandler_(copy);
                        }
                    });
                }
            }
            action->setIcon(V2ToolIcon(tool.commandId,tool.labelJa,tool.surfaceMethod.value_or(tool.measureMode.value_or(-1))));
            button->setDefaultAction(action);
            button->setAccessibleName(Text(tool.labelJa));
            button->setText(Text(tool.labelJa));   // 台帳の名前ではなく正本の言葉を出す
            if (tool.Blocked()) {
                button->setToolTip(Text(tool.blockedReasonJa));
            }
            toolRow_->Add(button, action);
            button->show();   // 上と同じ理由
            toolButtons_.push_back(entry);
        }
    }
    UpdateToolPresentation();
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
        if (entry.button->isVisible()
            && !toolRow_->rect().contains(entry.button->geometry())) {
            return false;
        }
    }
    return true;
}

void V2Ribbon::SetCompact(bool enabled)
{
    if (compact_ == enabled) return;
    compact_ = enabled;
    UpdateToolPresentation();
}

void V2Ribbon::UpdateToolPresentation()
{
    const auto style = compact_ ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonTextUnderIcon;
    const auto iconSize = compact_ ? QSize(20, 20) : QSize(32, 32);
    for (const auto& entry : toolButtons_) {
        entry.button->setToolButtonStyle(style);
        entry.button->setIconSize(iconSize);
        entry.button->setMinimumHeight(compact_ ? 32 : 54);
    }
    categoryRow_->Relayout();
    toolRow_->Relayout();
    layout()->activate();
    const int height = layout()->sizeHint().height();
    if (minimumHeight() != height) setFixedHeight(height);
    updateGeometry();
}
