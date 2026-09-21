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
#include <QStringList>
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
using kachakacha::v2::modeling::FourEdgeStyle;
using kachakacha::v2::modeling::GuideSurfaceMethod;

[[nodiscard]] QString Text(std::string_view value)
{
    return QString::fromUtf8(std::string(value).c_str());
}

//! 見えていて押せるボタンだけを押す。不可視の widget を叩いて通したことにしない。
[[nodiscard]] bool ClickVisible(QPushButton* button)
{
    if (button == nullptr || !button->isVisible() || !button->isEnabled()) {
        return false;
    }
    button->click();
    return true;
}

//! その方式の一言。正本のカードの下段に当たる。個数は個数の約束(SurfaceCardinality)と同じ。
[[nodiscard]] QString MethodHintJa(GuideSurfaceMethod method)
{
    switch (method) {
    case GuideSurfaceMethod::PlanarBoundary: return QStringLiteral("閉じた輪郭（複数線可）");
    case GuideSurfaceMethod::RuledSections:  return QStringLiteral("断面2本〜");
    case GuideSurfaceMethod::LoftSections:   return QStringLiteral("断面2本〜 + ガイド・中心線(任意)");
    case GuideSurfaceMethod::GuidedLoft:     return QStringLiteral("断面 + ガイド1本〜");
    case GuideSurfaceMethod::BoundaryFill:   return QStringLiteral("外周の輪 + 通る線(任意)");
    case GuideSurfaceMethod::GordonNetwork:  return QStringLiteral("U 2本〜 × V 2本〜");
    case GuideSurfaceMethod::OffsetGuide:    return QStringLiteral("面を離す(何枚でも)");
    case GuideSurfaceMethod::Revolve:        return QStringLiteral("断面を回す(何本でも)");
    case GuideSurfaceMethod::FourEdgePatch:  return QStringLiteral("4辺 + 通る線(任意)");
    case GuideSurfaceMethod::CurveNetworkExact: return QStringLiteral("U 2本〜 × V 2本〜(全部通す)");
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

void V2SurfaceDock::BuildSlotRow(QVBoxLayout* layout, int index)
{
    SlotRow& row = slots_[static_cast<std::size_t>(index)];
    const ChainRole role = kachakacha::v2::app::SurfaceSlotKey(index);
    row.key = role;
    auto* head = new QHBoxLayout();
    row.title = new QLabel(Text(kachakacha::v2::app::SurfaceSlotNameJa(role)), widget());
    head->addWidget(row.title);
    row.value = new QLabel(widget());
    row.value->setWordWrap(true);
    head->addWidget(row.value, 1);
    row.arm = new QPushButton(QStringLiteral("ここへ選ぶ"), widget());
    row.arm->setCheckable(true);
    row.arm->setToolTip(QStringLiteral("以後の 3D のクリックがこの欄へ入ります(もう一度押すと外れます)"));
    QObject::connect(row.arm, &QPushButton::clicked, this, [this, role] {
        if (!loading_ && activateHandler_) {
            activateHandler_(role);
        }
    });
    head->addWidget(row.arm);
    row.clear = new QPushButton(QStringLiteral("解除"), widget());
    row.clear->setToolTip(QStringLiteral("この欄を空にします"));
    QObject::connect(row.clear, &QPushButton::clicked, this, [this, role] {
        if (!loading_ && clearHandler_) {
            clearHandler_(role);
        }
    });
    head->addWidget(row.clear);
    layout->addLayout(head);
    // 可変長の一覧(「ガイド1」「ガイド2」のような固定欄にしない)。
    row.list = new QTreeWidget(widget());
    row.list->setColumnCount(2);
    row.list->setHeaderHidden(true);
    row.list->setRootIsDecorated(false);
    row.list->setMaximumHeight(110);
    layout->addWidget(row.list);
    auto* actions = new QHBoxLayout();
    const auto current = [this, index] {
        const QTreeWidget* list = slots_[static_cast<std::size_t>(index)].list;
        return list == nullptr || list->currentItem() == nullptr
            ? -1 : list->indexOfTopLevelItem(list->currentItem());
    };
    row.remove = new QPushButton(QStringLiteral("× 外す"), widget());
    QObject::connect(row.remove, &QPushButton::clicked, this, [this, role, current] {
        if (!loading_ && removeEntryHandler_ && current() >= 0) {
            removeEntryHandler_(role, current());
        }
    });
    actions->addWidget(row.remove);
    row.flip = new QPushButton(QStringLiteral("向き反転"), widget());
    QObject::connect(row.flip, &QPushButton::clicked, this, [this, role, current] {
        if (!loading_ && flipEntryHandler_ && current() >= 0) {
            flipEntryHandler_(role, current());
        }
    });
    actions->addWidget(row.flip);
    actions->addStretch(1);
    layout->addLayout(actions);
}

void V2SurfaceDock::BuildSlotRows(QVBoxLayout* layout)
{
    // 2. 入力。断面 / ガイド / 中心線 / 境界。使わない役割は「この方式では不要」と出す。
    //
    // 欄ごとに「ここへ選ぶ」と「解除」。**3D の次のクリックがどの欄へ入るかは、
    // 押された形の「ここへ選ぶ」でいつも見えている**(引継ぎ 2026-09-17 の 1)。
    // 1本ずつ外すのは 3D でもう一度押すか、一覧の行を選んで「× 外す」。
    layout->addWidget(new QLabel(QStringLiteral("2. 入力"), widget()));
    for (int index = 0; index < kachakacha::v2::app::kSurfaceSlotCount; ++index) {
        BuildSlotRow(layout, index);
    }
    // 境界の辺ごとの連続条件(境界面・四辺面)。境界の一覧で選んだ行に効く。
    auto* edge = new QHBoxLayout();
    continuityTitle_ = new QLabel(QStringLiteral("連続"), widget());
    edge->addWidget(continuityTitle_);
    continuity_ = new QComboBox(widget());
    continuity_->addItem(QStringLiteral("G0 位置"));
    continuity_->addItem(QStringLiteral("G1 接線"));
    continuity_->addItem(QStringLiteral("G2 曲率"));
    continuity_->setToolTip(QStringLiteral("隣の面とのつながり方。G1/G2 は隣の面(支持面)が要ります"));
    edge->addWidget(continuity_);
    pickSupport_ = new QPushButton(QStringLiteral("支持面を選ぶ"), widget());
    pickSupport_->setToolTip(QStringLiteral("押してから、3D で隣の面を押します"));
    edge->addWidget(pickSupport_);
    layout->addLayout(edge);
    supportName_ = new QLabel(widget());
    supportName_->setWordWrap(true);
    layout->addWidget(supportName_);
    const auto boundaryRow = [this]() {
        const QTreeWidget* list = slots_[3].list;
        return list == nullptr || list->currentItem() == nullptr
            ? -1 : list->indexOfTopLevelItem(list->currentItem());
    };
    QObject::connect(continuity_, &QComboBox::currentIndexChanged, this,
        [this, boundaryRow](int index) {
            if (!loading_ && continuityHandler_ && boundaryRow() >= 0 && index >= 0) {
                continuityHandler_(boundaryRow(),
                    static_cast<kachakacha::v2::modeling::SurfaceContinuity>(index));
            }
        });
    QObject::connect(pickSupport_, &QPushButton::clicked, this, [this, boundaryRow] {
        if (!loading_ && pickSupportHandler_ && boundaryRow() >= 0) {
            pickSupportHandler_(boundaryRow());
        }
    });
    QObject::connect(slots_[3].list, &QTreeWidget::itemSelectionChanged, this,
        [this] { RefreshContinuityRow(); });
    // 四辺面の張り方。四辺面のときだけ出す。
    auto* style = new QHBoxLayout();
    fourEdgeStyleTitle_ = new QLabel(QStringLiteral("張り方"), widget());
    style->addWidget(fourEdgeStyleTitle_);
    fourEdgeStyle_ = new QComboBox(widget());
    for (const FourEdgeStyle item : {FourEdgeStyle::Coons, FourEdgeStyle::Stretch,
             FourEdgeStyle::Curved}) {
        fourEdgeStyle_->addItem(Text(kachakacha::v2::modeling::FourEdgeStyleLabelJa(item)));
    }
    QObject::connect(fourEdgeStyle_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!loading_ && fourEdgeStyleHandler_ && index >= 0 && index <= 2) {
            fourEdgeStyleHandler_(static_cast<FourEdgeStyle>(index));
        }
    });
    style->addWidget(fourEdgeStyle_, 1);
    layout->addLayout(style);
    solverNote_ = new QLabel(widget());
    solverNote_->setWordWrap(true);
    layout->addWidget(solverNote_);
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

//! 欄の見出し。作り方で言葉が変わる(回転体の「軸」、境界面の「通る線」、曲線網の U/V)。
void V2SurfaceDock::RefreshSlotTitles(GuideSurfaceMethod method)
{
    for (SlotRow& row : slots_) {
        if (row.title != nullptr) {
            row.title->setText(Text(kachakacha::v2::app::SurfaceSlotNameJa(method, row.key)));
        }
    }
}

void V2SurfaceDock::FillSlotRow(SlotRow& row,
    const kachakacha::v2::app::SurfaceInputState& state, const std::vector<QString>& names,
    bool previewShown)
{
    const auto& entries = kachakacha::v2::app::SurfaceSlotEntries(state, row.key);
    row.list->clear();
    for (std::size_t at = 0; at < names.size(); ++at) {
        auto* item = new QTreeWidgetItem(row.list);
        item->setText(0, QString::number(static_cast<int>(at) + 1));
        const bool flipped = at < entries.size()
            && kachakacha::v2::app::SurfaceEntryReversed(state, entries[at]);
        item->setText(1, names[at] + (flipped ? QStringLiteral("(逆向き)") : QString()));
    }
    for (const auto& view : kachakacha::v2::app::SurfaceSlotsFor(state)) {
        if (view.role != row.key) {
            continue;
        }
        const bool used = view.state != SurfaceSlotState::NotUsedByMethod;
        row.arm->setChecked(state.activeSlot == row.key);
        row.arm->setEnabled(used);
        row.clear->setEnabled(view.count > 0);
        row.list->setVisible(used && view.count > 0);
        row.remove->setVisible(used && view.count > 0);
        row.flip->setVisible(used && view.count > 0
            && kachakacha::v2::app::SurfaceSlotFlippable(state.method, row.key));
        if (!used) {
            // **入っていても捨てない。**そのことも言う。
            row.value->setText(view.count > 0
                    ? QStringLiteral("この方式では不要(%1本は残しています)")
                          .arg(static_cast<int>(view.count))
                    : QStringLiteral("この方式では不要"));
            return;
        }
        if (view.count == 0) {
            row.value->setText(view.state == SurfaceSlotState::Optional
                    ? (row.key == ChainRole::GuideU
                              && state.method == GuideSurfaceMethod::BoundaryFill
                          ? QStringLiteral("(任意) 面が必ず通る線。境界に全部入れても自動で分けます")
                          : QStringLiteral("(任意) 無くても作れます"))
                    : state.activeSlot == row.key ? QStringLiteral("(3D で押してください)")
                                                  : QStringLiteral("(選んでいません)"));
            return;
        }
        if (row.key == ChainRole::BoundarySide
            && (state.method == GuideSurfaceMethod::PlanarBoundary
                || state.method == GuideSurfaceMethod::BoundaryFill)
            && view.count > 1) {
            row.value->setText(previewShown
                    ? QStringLiteral("%1本 → %2").arg(static_cast<int>(view.count)).arg(
                          state.method == GuideSurfaceMethod::BoundaryFill
                              ? QStringLiteral("外周の輪と、面が通る線")
                              : QStringLiteral("1つの閉じた輪郭"))
                    : QStringLiteral("%1本（端点のつながりを確認中）")
                          .arg(static_cast<int>(view.count)));
            return;
        }
        // **名前を並べる。**本数だけでは、どれが入っているのか分からない。
        QStringList parts;
        for (const QString& name : names) {
            parts << name;
        }
        row.value->setText(QStringLiteral("%1本: %2")
                .arg(static_cast<int>(view.count))
                .arg(parts.join(QStringLiteral(", "))));
        return;
    }
}

void V2SurfaceDock::ShowInput(const kachakacha::v2::app::SurfaceInputState& state,
    const SlotNames& names, bool previewShown, const QString& deviationNoteJa,
    const QString& solverNoteJa)
{
    const std::vector<QString>& sectionNamesJa = names.sections;
    loading_ = true;
    shown_ = state;

    // 1. 作り方。いま効いているものが押された形で見える。
    const auto& main = kachakacha::v2::app::MainSurfaceMethods();
    for (std::size_t index = 0; index < methodCards_.size() && index < main.size(); ++index) {
        methodCards_[index]->setChecked(main[index] == state.method);
    }
    state_->setText(QStringLiteral("面を作る — %1")
            .arg(Text(kachakacha::v2::app::GuideSurfaceMethodLabelJa(state.method))));
    RefreshSlotTitles(state.method);

    // 2. 入力。使わない役割は「この方式では不要」と出し、選ぶ先にもさせない。
    const std::vector<QString>* lists[] = {&names.sections, &names.guides, &names.centerlines,
        &names.boundaries};
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        FillSlotRow(slots_[index], state, *lists[index], previewShown);
    }
    shownNames_ = names;
    RefreshContinuityRow();
    const bool fourEdge = state.method == GuideSurfaceMethod::FourEdgePatch;
    fourEdgeStyleTitle_->setVisible(fourEdge);
    fourEdgeStyle_->setVisible(fourEdge);
    fourEdgeStyle_->setCurrentIndex(static_cast<int>(state.fourEdgeStyle));
    solverNote_->setVisible(!solverNoteJa.isEmpty());
    solverNote_->setText(solverNoteJa.isEmpty() ? QString()
                                                : QStringLiteral("作り方の内訳: ") + solverNoteJa);
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

void V2SurfaceDock::SetActivateHandler(std::function<void(ChainRole)> handler)
{
    activateHandler_ = std::move(handler);
}

void V2SurfaceDock::SetClearHandler(std::function<void(ChainRole)> handler)
{
    clearHandler_ = std::move(handler);
}

void V2SurfaceDock::SetOrderingHandler(std::function<void(SurfaceOrdering)> handler)
{
    orderingHandler_ = std::move(handler);
}

void V2SurfaceDock::SetMoveSectionHandler(std::function<void(int, int)> handler)
{
    moveSectionHandler_ = std::move(handler);
}

//! 連続条件の欄を、境界の一覧で選んでいる行に合わせる。
void V2SurfaceDock::RefreshContinuityRow()
{
    if (continuity_ == nullptr || slots_[3].list == nullptr) {
        return;
    }
    const bool takes = kachakacha::v2::app::SurfaceTakesContinuity(shown_.method);
    const QTreeWidget* list = slots_[3].list;
    const int row = list->currentItem() == nullptr ? -1
                                                   : list->indexOfTopLevelItem(list->currentItem());
    const bool usable = takes && row >= 0 && list->isVisible();
    continuityTitle_->setVisible(takes && !shown_.boundaries.empty());
    continuity_->setVisible(takes && !shown_.boundaries.empty());
    pickSupport_->setVisible(takes && !shown_.boundaries.empty());
    continuity_->setEnabled(usable);
    pickSupport_->setEnabled(usable);
    const bool wasLoading = loading_;
    loading_ = true;
    const auto rowIndex = static_cast<std::size_t>(row < 0 ? 0 : row);
    continuity_->setCurrentIndex(usable && rowIndex < shownNames_.boundaryContinuity.size()
            ? shownNames_.boundaryContinuity[rowIndex] : 0);
    loading_ = wasLoading;
    if (!takes) {
        supportName_->setText(shown_.boundaries.empty()
                ? QString()
                : QStringLiteral("この作り方では縁の連続条件を指定できません(境界面・四辺面で指定できます)"));
        return;
    }
    if (!usable) {
        supportName_->setText(shown_.boundaries.empty()
                ? QString()
                : QStringLiteral("境界の一覧で辺を選ぶと、その辺の連続条件(G0/G1/G2)と支持面を決められます"));
        return;
    }
    const QString support = rowIndex < shownNames_.boundarySupports.size()
        ? shownNames_.boundarySupports[rowIndex] : QString();
    supportName_->setText(!shown_.supportPickFor.IsNil()
            ? QStringLiteral("3D で隣の面を押してください")
            : (support.isEmpty() ? QStringLiteral("支持面: なし(G1/G2 には隣の面が要ります)")
                                 : QStringLiteral("支持面: ") + support));
}

bool V2SurfaceDock::ChooseContinuity(int row, kachakacha::v2::modeling::SurfaceContinuity order)
{
    QTreeWidget* list = slots_[3].list;
    if (list == nullptr || continuity_ == nullptr || !list->isVisible() || row < 0
        || row >= list->topLevelItemCount()) {
        return false;
    }
    list->setCurrentItem(list->topLevelItem(row));
    RefreshContinuityRow();
    if (!continuity_->isVisible() || !continuity_->isEnabled()) {
        return false;
    }
    continuity_->setCurrentIndex(static_cast<int>(order));
    return true;
}

bool V2SurfaceDock::ClickPickSupport(int row)
{
    QTreeWidget* list = slots_[3].list;
    if (list == nullptr || !list->isVisible() || row < 0 || row >= list->topLevelItemCount()) {
        return false;
    }
    list->setCurrentItem(list->topLevelItem(row));
    RefreshContinuityRow();
    return ClickVisible(pickSupport_);
}

QString V2SurfaceDock::ContinuityTextJa() const
{
    return supportName_ == nullptr ? QString() : supportName_->text();
}

void V2SurfaceDock::SetContinuityHandler(
    std::function<void(int, kachakacha::v2::modeling::SurfaceContinuity)> handler)
{
    continuityHandler_ = std::move(handler);
}

void V2SurfaceDock::SetPickSupportHandler(std::function<void(int)> handler)
{
    pickSupportHandler_ = std::move(handler);
}

void V2SurfaceDock::SetRemoveEntryHandler(std::function<void(ChainRole, int)> handler)
{
    removeEntryHandler_ = std::move(handler);
}

void V2SurfaceDock::SetFlipEntryHandler(std::function<void(ChainRole, int)> handler)
{
    flipEntryHandler_ = std::move(handler);
}

void V2SurfaceDock::SetFourEdgeStyleHandler(std::function<void(FourEdgeStyle)> handler)
{
    fourEdgeStyleHandler_ = std::move(handler);
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

const V2SurfaceDock::SlotRow* V2SurfaceDock::RowFor(ChainRole slot) const
{
    ChainRole key = slot;
    if (slot == ChainRole::GuideV) {
        key = ChainRole::GuideU;
    } else if (slot == ChainRole::OuterBoundary || slot == ChainRole::HoleBoundary) {
        key = ChainRole::BoundarySide;
    }
    for (const SlotRow& row : slots_) {
        if (row.key == key) {
            return &row;
        }
    }
    return nullptr;
}

bool V2SurfaceDock::ClickActivate(ChainRole slot)
{
    const SlotRow* row = RowFor(slot);
    return row != nullptr && ClickVisible(row->arm);
}

bool V2SurfaceDock::ClickClear(ChainRole slot)
{
    const SlotRow* row = RowFor(slot);
    return row != nullptr && ClickVisible(row->clear);
}

bool V2SurfaceDock::ClickRemoveEntry(ChainRole slot, int row)
{
    const SlotRow* found = RowFor(slot);
    if (found == nullptr || found->list == nullptr || row < 0
        || row >= found->list->topLevelItemCount() || !found->list->isVisible()) {
        return false;
    }
    found->list->setCurrentItem(found->list->topLevelItem(row));
    return ClickVisible(found->remove);
}

bool V2SurfaceDock::ClickFlipEntry(ChainRole slot, int row)
{
    const SlotRow* found = RowFor(slot);
    if (found == nullptr || found->list == nullptr || row < 0
        || row >= found->list->topLevelItemCount() || !found->list->isVisible()) {
        return false;
    }
    found->list->setCurrentItem(found->list->topLevelItem(row));
    return ClickVisible(found->flip);
}

bool V2SurfaceDock::ChooseFourEdgeStyle(FourEdgeStyle style)
{
    if (fourEdgeStyle_ == nullptr || !fourEdgeStyle_->isVisible()) {
        return false;
    }
    fourEdgeStyle_->setCurrentIndex(static_cast<int>(style));
    return true;
}

std::vector<QString> V2SurfaceDock::SlotEntryTexts(ChainRole slot) const
{
    std::vector<QString> texts;
    const SlotRow* row = RowFor(slot);
    if (row == nullptr || row->list == nullptr) {
        return texts;
    }
    for (int index = 0; index < row->list->topLevelItemCount(); ++index) {
        texts.push_back(row->list->topLevelItem(index)->text(0) + QStringLiteral("  ")
            + row->list->topLevelItem(index)->text(1));
    }
    return texts;
}

bool V2SurfaceDock::ClickOrdering(SurfaceOrdering ordering)
{
    QPushButton* button = ordering == SurfaceOrdering::ManualLock ? orderManual_ : orderAuto_;
    if (button == nullptr || !button->isVisible() || !button->isEnabled()) {
        return false;
    }
    button->click();
    return true;
}

bool V2SurfaceDock::ClickMoveRow(int row, bool up)
{
    QPushButton* button = up ? moveUp_ : moveDown_;
    if (orderList_ == nullptr || button == nullptr || !button->isVisible()
        || !button->isEnabled() || row < 0 || row >= orderList_->topLevelItemCount()) {
        return false;
    }
    orderList_->setCurrentItem(orderList_->topLevelItem(row));
    button->click();
    return true;
}

ChainRole V2SurfaceDock::ActiveSlotShown() const
{
    for (const SlotRow& row : slots_) {
        if (row.arm != nullptr && row.arm->isChecked()) {
            return row.key;
        }
    }
    return ChainRole::Section;
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
    const SlotRow* row = RowFor(role);
    return row == nullptr || row->value == nullptr ? QString() : row->value->text();
}

bool V2SurfaceDock::CanConfirm() const
{
    return confirm_ != nullptr && confirm_->isEnabled();
}
