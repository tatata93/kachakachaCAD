#include "V2SurfaceDock.h"

#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <QComboBox>
#include <QDockWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <string>

namespace {

using kachakacha::v2::app::SurfaceOrdering;
using kachakacha::v2::app::SurfaceSlotState;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideSurfaceMethod;

[[nodiscard]] QString Text(std::string_view value)
{
    return QString::fromUtf8(std::string(value).c_str());
}

//! その方式の一言。正本のカードの下段に当たる。
[[nodiscard]] QString MethodHintJa(GuideSurfaceMethod method)
{
    switch (method) {
    case GuideSurfaceMethod::PlanarBoundary: return QStringLiteral("閉じた輪郭（複数線可）");
    case GuideSurfaceMethod::RuledSections:  return QStringLiteral("断面2本");
    case GuideSurfaceMethod::LoftSections:   return QStringLiteral("断面3本以上");
    case GuideSurfaceMethod::GuidedLoft:     return QStringLiteral("断面 + ガイド");
    case GuideSurfaceMethod::BoundaryFill:   return QStringLiteral("非平面の閉じた輪郭（複数線可）");
    case GuideSurfaceMethod::GordonNetwork:  return QStringLiteral("U/Vネットワーク");
    case GuideSurfaceMethod::OffsetGuide:    return QStringLiteral("面を離す");
    case GuideSurfaceMethod::Revolve:        return QStringLiteral("断面を回す");
    }
    return QString();
}

} // namespace

void V2SurfaceDock::BuildMethodCards(QVBoxLayout* layout)
{
    // 1. 作り方。**常時見せる。**いまどの作り方なのかが、どこにも出ていなかった。
    layout->addWidget(new QLabel(QStringLiteral("1. 作り方"), widget()));
    for (const GuideSurfaceMethod method : kachakacha::v2::app::MainSurfaceMethods()) {
        auto* card = new QPushButton(
            Text(kachakacha::v2::app::GuideSurfaceMethodLabelJa(method))
                + QStringLiteral("  ─  ") + MethodHintJa(method),
            widget());
        card->setCheckable(true);
        QObject::connect(card, &QPushButton::clicked, this, [this, method] {
            if (!loading_ && methodHandler_) {
                methodHandler_(method);
            }
        });
        methodCards_.push_back(card);
        layout->addWidget(card);
    }
    // 主要6方式に入らないものは「その他」へ。**既存機能は消さない。**
    otherMethods_ = new QComboBox(widget());
    otherMethods_->addItem(QStringLiteral("その他の作り方..."));
    for (const GuideSurfaceMethod method : kachakacha::v2::app::OtherSurfaceMethods()) {
        otherMethods_->addItem(Text(kachakacha::v2::app::GuideSurfaceMethodLabelJa(method)));
    }
    QObject::connect(otherMethods_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (loading_ || index <= 0 || !methodHandler_) {
            return;
        }
        const auto& others = kachakacha::v2::app::OtherSurfaceMethods();
        const std::size_t at = static_cast<std::size_t>(index - 1);
        if (at < others.size()) {
            methodHandler_(others[at]);
        }
    });
    layout->addWidget(otherMethods_);
}

void V2SurfaceDock::BuildSlotRows(QVBoxLayout* layout)
{
    // 2. 入力。断面 / ガイド / 境界。使わない役割は「この方式では不要」と出す。
    layout->addWidget(new QLabel(QStringLiteral("2. 入力"), widget()));
    const auto row = [this, layout](const QString& name, QLabel** value,
                         QPushButton** add, ChainRole role) {
        auto* line = new QHBoxLayout();
        line->addWidget(new QLabel(name, widget()));
        *value = new QLabel(widget());
        (*value)->setWordWrap(true);
        line->addWidget(*value, 1);
        *add = new QPushButton(QStringLiteral("追加"), widget());
        QObject::connect(*add, &QPushButton::clicked, this, [this, role] {
            if (!loading_ && addHandler_) {
                addHandler_(role);
            }
        });
        line->addWidget(*add);
        layout->addLayout(line);
    };
    row(QStringLiteral("断面"), &sectionValue_, &addSection_, ChainRole::Section);
    row(QStringLiteral("ガイド"), &guideValue_, &addGuide_, ChainRole::GuideU);
    row(QStringLiteral("境界"), &boundaryValue_, &addBoundary_, ChainRole::BoundarySide);
}

void V2SurfaceDock::BuildOrderRows(QVBoxLayout* layout)
{
    // 3. 断面順。**いまの生成順を番号つきで出す。**
    layout->addWidget(new QLabel(QStringLiteral("3. 断面順"), widget()));
    auto* modes = new QHBoxLayout();
    orderAuto_ = new QPushButton(QStringLiteral("自動"), widget());
    orderManual_ = new QPushButton(QStringLiteral("手動固定"), widget());
    orderAuto_->setCheckable(true);
    orderManual_->setCheckable(true);
    QObject::connect(orderAuto_, &QPushButton::clicked, this, [this] {
        if (!loading_ && orderingHandler_) {
            orderingHandler_(SurfaceOrdering::Auto);
        }
    });
    QObject::connect(orderManual_, &QPushButton::clicked, this, [this] {
        if (!loading_ && orderingHandler_) {
            orderingHandler_(SurfaceOrdering::ManualLock);
        }
    });
    modes->addWidget(orderAuto_);
    modes->addWidget(orderManual_);
    layout->addLayout(modes);

    orderList_ = new QTreeWidget(widget());
    orderList_->setColumnCount(2);
    orderList_->setHeaderLabels({QStringLiteral("番号"), QStringLiteral("断面")});
    orderList_->setRootIsDecorated(false);
    layout->addWidget(orderList_);

    auto* moves = new QHBoxLayout();
    moveUp_ = new QPushButton(QStringLiteral("↑"), widget());
    moveDown_ = new QPushButton(QStringLiteral("↓"), widget());
    const auto move = [this](int delta) {
        if (loading_ || moveSectionHandler_ == nullptr || orderList_ == nullptr) {
            return;
        }
        QTreeWidgetItem* item = orderList_->currentItem();
        if (item == nullptr) {
            return;
        }
        const int from = orderList_->indexOfTopLevelItem(item);
        const int to = from + delta;
        if (from < 0 || to < 0 || to >= orderList_->topLevelItemCount()) {
            return;
        }
        moveSectionHandler_(from, to);
    };
    QObject::connect(moveUp_, &QPushButton::clicked, this, [move] { move(-1); });
    QObject::connect(moveDown_, &QPushButton::clicked, this, [move] { move(+1); });
    moves->addWidget(moveUp_);
    moves->addWidget(moveDown_);
    layout->addLayout(moves);
}

V2SurfaceDock::V2SurfaceDock(QWidget* parent)
    : QDockWidget(QStringLiteral("面を作る"), parent)
{
    setObjectName(QStringLiteral("surfaceDock"));
    auto* body = new QWidget(this);
    setWidget(body);
    auto* rootLayout = new QVBoxLayout(body);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(6);

    auto* content = new QWidget(body);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    state_ = new QLabel(body);
    state_->setWordWrap(true);
    layout->addWidget(state_);

    BuildMethodCards(layout);
    BuildSlotRows(layout);
    BuildOrderRows(layout);

    // 4. 状態。
    layout->addWidget(new QLabel(QStringLiteral("4. 状態"), body));
    status_ = new QLabel(body);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    layout->addStretch(1);

    auto* scroll = new QScrollArea(body);
    scroll->setObjectName(QStringLiteral("surfaceSettingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    rootLayout->addWidget(scroll, 1);

    // 下のボタン。並びは正本のとおり。
    auto* actions = new QHBoxLayout();
    cancel_ = new QPushButton(QStringLiteral("キャンセル Esc"), body);
    reset_ = new QPushButton(QStringLiteral("入力をやり直す"), body);
    confirm_ = new QPushButton(QStringLiteral("確定 Enter"), body);
    QObject::connect(cancel_, &QPushButton::clicked, this, [this] {
        if (cancelHandler_) {
            cancelHandler_();
        }
    });
    QObject::connect(reset_, &QPushButton::clicked, this, [this] {
        if (resetHandler_) {
            resetHandler_();
        }
    });
    QObject::connect(confirm_, &QPushButton::clicked, this, [this] {
        if (confirmHandler_) {
            confirmHandler_();
        }
    });
    actions->addWidget(cancel_);
    actions->addWidget(reset_);
    actions->addWidget(confirm_);
    rootLayout->addLayout(actions);
}

void V2SurfaceDock::ShowInput(const kachakacha::v2::app::SurfaceInputState& state,
    const std::vector<QString>& sectionNamesJa, bool previewShown,
    const QString& deviationNoteJa)
{
    loading_ = true;
    shown_ = state;

    // 1. 作り方。いま効いているものが押された形で見える。
    const auto& main = kachakacha::v2::app::MainSurfaceMethods();
    for (std::size_t index = 0; index < methodCards_.size() && index < main.size(); ++index) {
        methodCards_[index]->setChecked(main[index] == state.method);
    }
    state_->setText(QStringLiteral("面を作る — %1")
            .arg(Text(kachakacha::v2::app::GuideSurfaceMethodLabelJa(state.method))));

    // 2. 入力。使わない役割は「この方式では不要」と出し、追加も押させない。
    const auto fill = [&](ChainRole role, QLabel* value, QPushButton* add) {
        for (const auto& view : kachakacha::v2::app::SurfaceSlotsFor(state)) {
            if (view.role != role) {
                continue;
            }
            if (view.state == SurfaceSlotState::NotUsedByMethod) {
                // **入っていても捨てない。**そのことも言う。
                value->setText(view.count > 0
                        ? QStringLiteral("この方式では不要(%1本は残しています)")
                              .arg(static_cast<int>(view.count))
                        : QStringLiteral("この方式では不要"));
                add->setEnabled(false);
                return;
            }
            add->setEnabled(true);
            if (view.count == 0) {
                value->setText(QStringLiteral("(選んでいません)"));
                return;
            }
            if (role == ChainRole::BoundarySide
                && (state.method == GuideSurfaceMethod::PlanarBoundary
                    || state.method == GuideSurfaceMethod::BoundaryFill)
                && view.count > 1) {
                value->setText(previewShown
                        ? QStringLiteral("%1本 → 1つの閉じた輪郭")
                              .arg(static_cast<int>(view.count))
                        : QStringLiteral("%1本（端点のつながりを確認中）")
                              .arg(static_cast<int>(view.count)));
            } else {
                value->setText(QStringLiteral("%1本").arg(static_cast<int>(view.count)));
            }
            return;
        }
    };
    fill(ChainRole::Section, sectionValue_, addSection_);
    fill(ChainRole::GuideU, guideValue_, addGuide_);
    fill(ChainRole::BoundarySide, boundaryValue_, addBoundary_);

    // 3. 断面順。**いまの生成順を番号つきで出す。**
    orderAuto_->setChecked(state.ordering == SurfaceOrdering::Auto);
    orderManual_->setChecked(state.ordering == SurfaceOrdering::ManualLock);
    orderList_->clear();
    for (std::size_t index = 0; index < sectionNamesJa.size(); ++index) {
        auto* item = new QTreeWidgetItem(orderList_);
        item->setText(0, QString::number(static_cast<int>(index) + 1));
        item->setText(1, sectionNamesJa[index]);
    }
    const bool manual = state.ordering == SurfaceOrdering::ManualLock;
    moveUp_->setEnabled(manual);
    moveDown_->setEnabled(manual);

    // 4. 状態。
    QString text;
    for (const std::string& line : kachakacha::v2::app::SurfaceStatusLinesJa(state,
             previewShown, deviationNoteJa.toStdString())) {
        if (!text.isEmpty()) {
            text += QStringLiteral("\n");
        }
        text += QString::fromStdString(line);
    }
    status_->setText(text);
    confirm_->setEnabled(kachakacha::v2::app::SurfaceReadyToBuild(state));
    loading_ = false;
}

void V2SurfaceDock::SetMethodHandler(std::function<void(GuideSurfaceMethod)> handler)
{
    methodHandler_ = std::move(handler);
}

void V2SurfaceDock::SetAddHandler(std::function<void(ChainRole)> handler)
{
    addHandler_ = std::move(handler);
}

void V2SurfaceDock::SetOrderingHandler(std::function<void(SurfaceOrdering)> handler)
{
    orderingHandler_ = std::move(handler);
}

void V2SurfaceDock::SetMoveSectionHandler(std::function<void(int, int)> handler)
{
    moveSectionHandler_ = std::move(handler);
}

void V2SurfaceDock::SetActionHandlers(std::function<void()> confirm,
    std::function<void()> cancel, std::function<void()> reset)
{
    confirmHandler_ = std::move(confirm);
    cancelHandler_ = std::move(cancel);
    resetHandler_ = std::move(reset);
}

bool V2SurfaceDock::ClickMethodCard(GuideSurfaceMethod method)
{
    const auto& main = kachakacha::v2::app::MainSurfaceMethods();
    for (std::size_t index = 0; index < methodCards_.size() && index < main.size(); ++index) {
        if (main[index] != method) {
            continue;
        }
        if (!methodCards_[index]->isVisible()) {
            return false;   // 見えていないものは押せない。
        }
        methodCards_[index]->click();
        return true;
    }
    return false;
}

void V2SurfaceDock::PressMethod(GuideSurfaceMethod method)
{
    if (methodHandler_) {
        methodHandler_(method);
    }
}

void V2SurfaceDock::PressAdd(ChainRole role)
{
    if (addHandler_) {
        addHandler_(role);
    }
}

void V2SurfaceDock::PressOrdering(SurfaceOrdering ordering)
{
    if (orderingHandler_) {
        orderingHandler_(ordering);
    }
}

void V2SurfaceDock::PressConfirm()
{
    if (confirmHandler_) {
        confirmHandler_();
    }
}

void V2SurfaceDock::PressCancel()
{
    if (cancelHandler_) {
        cancelHandler_();
    }
}

std::vector<QString> V2SurfaceDock::SectionOrderTexts() const
{
    std::vector<QString> texts;
    if (orderList_ == nullptr) {
        return texts;
    }
    for (int index = 0; index < orderList_->topLevelItemCount(); ++index) {
        texts.push_back(orderList_->topLevelItem(index)->text(1));
    }
    return texts;
}

QString V2SurfaceDock::SlotTextJa(ChainRole role) const
{
    switch (role) {
    case ChainRole::Section:
        return sectionValue_ == nullptr ? QString() : sectionValue_->text();
    case ChainRole::GuideU:
    case ChainRole::GuideV:
        return guideValue_ == nullptr ? QString() : guideValue_->text();
    case ChainRole::BoundarySide:
    case ChainRole::OuterBoundary:
    case ChainRole::HoleBoundary:
        return boundaryValue_ == nullptr ? QString() : boundaryValue_->text();
    case ChainRole::SourceSurface:
        break;
    }
    return QString();
}

bool V2SurfaceDock::CanConfirm() const
{
    return confirm_ != nullptr && confirm_->isEnabled();
}
