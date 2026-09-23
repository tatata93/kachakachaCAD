//! 「面にする」の棚。見出しは V2LoopFacesDock.h。

#include "V2LoopFacesDock.h"
#include "V2PanelFrame.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace {

//! ①〜⑳、それより先は "(21)" のように書く。
[[nodiscard]] QString CircledNumber(int number)
{
    static const QString kCircled = QStringLiteral("①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮⑯⑰⑱⑲⑳");
    if (number >= 1 && number <= kCircled.size()) {
        return kCircled.mid(number - 1, 1);
    }
    return QStringLiteral("(%1)").arg(number);
}

[[nodiscard]] bool ClickIfUsable(QAbstractButton* button)
{
    if (button == nullptr || !button->isVisible() || !button->isEnabled()) {
        return false;
    }
    button->click();
    return true;
}

[[nodiscard]] bool TypeIfUsable(QDoubleSpinBox* box, double value)
{
    if (box == nullptr || !box->isVisible() || !box->isEnabled()) {
        return false;
    }
    box->setValue(value);
    return true;
}

[[nodiscard]] bool ChooseIfUsable(QComboBox* box, int index)
{
    if (box == nullptr || !box->isVisible() || !box->isEnabled() || index < 0 || index >= box->count()) {
        return false;
    }
    box->setCurrentIndex(index);
    return true;
}

} // namespace

V2LoopFacesDock::V2LoopFacesDock(QWidget* parent)
    : QDockWidget(QStringLiteral("面にする"), parent)
{
    setObjectName(QStringLiteral("loopFacesDock"));
    auto* body = new QWidget(this);
    setWidget(body);
    auto* outer = new QVBoxLayout(body);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(4);

    // 直前の操作(作ったあとに出す)。
    recentRow_ = new QWidget(body);
    auto* recentLine = new QHBoxLayout(recentRow_);
    recentLine->setContentsMargins(0, 0, 0, 0);
    recent_ = new QLabel(recentRow_);
    recent_->setWordWrap(true);
    recentLine->addWidget(recent_, 1);
    reopen_ = new QPushButton(QStringLiteral("開いて直す"), recentRow_);
    reopen_->setToolTip(QStringLiteral("1 回の取り消しで元に戻し、同じ線で構え直します。値を変えて Enter で作り直せます"));
    QObject::connect(reopen_, &QPushButton::clicked, this, [this] {
        if (reopenHandler_) {
            reopenHandler_();
        }
    });
    recentLine->addWidget(reopen_);
    recentRow_->hide();
    outer->addWidget(recentRow_);

    planBody_ = new QWidget(body);
    outer->addWidget(planBody_);
    auto* layout = new QVBoxLayout(planBody_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("1. 作り方")));
    auto* toleranceRow = new QHBoxLayout();
    toleranceRow->addWidget(new QLabel(QStringLiteral("許容(端)"), body));
    tolerance_ = new QDoubleSpinBox(body);
    tolerance_->setObjectName(QStringLiteral("loopFacesTolerance"));
    tolerance_->setRange(0.001, 1.0);
    tolerance_->setDecimals(3);
    tolerance_->setSuffix(QStringLiteral(" mm"));
    toleranceRow->addWidget(tolerance_, 1);
    layout->addLayout(toleranceRow);
    QObject::connect(tolerance_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!loading_ && toleranceHandler_) {
            toleranceHandler_(value);
        }
    });

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("2. 輪")));
    auto* facesContainer = new QWidget(body);
    facesLayout_ = new QVBoxLayout(facesContainer);
    facesLayout_->setContentsMargins(0, 0, 0, 0);
    facesLayout_->setSpacing(2);
    layout->addWidget(facesContainer);

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("3. ずれ・T 字")));
    auto* gapsContainer = new QWidget(body);
    gapsLayout_ = new QVBoxLayout(gapsContainer);
    gapsLayout_->setContentsMargins(0, 0, 0, 0);
    gapsLayout_->setSpacing(2);
    layout->addWidget(gapsContainer);

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("4. 状態")));
    summary_ = new QLabel(body);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    layout->addStretch(1);
    layout->addLayout(MakeCancelConfirmRow(body, &cancel_, &confirm_));
    QObject::connect(cancel_, &QPushButton::clicked, this, [this] {
        if (cancelHandler_) {
            cancelHandler_();
        }
    });
    QObject::connect(confirm_, &QPushButton::clicked, this, [this] {
        if (confirmHandler_) {
            confirmHandler_();
        }
    });
}

V2LoopFacesDock::FaceRowWidgets V2LoopFacesDock::MakeFaceRow(const V2LoopFaceRow& face)
{
    auto* body = widget();
    FaceRowWidgets widgets;
    widgets.row = new QWidget(body);
    widgets.column = new QVBoxLayout(widgets.row);
    widgets.column->setContentsMargins(0, 0, 0, 0);
    widgets.column->setSpacing(1);
    auto* first = new QWidget(widgets.row);
    widgets.column->addWidget(first);
    auto* line = new QHBoxLayout(first);
    line->setContentsMargins(0, 0, 0, 0);
    widgets.number = new QLabel(CircledNumber(face.number), widgets.row);
    line->addWidget(widgets.number);
    widgets.method = new QComboBox(widgets.row);
    for (const QString& choice : face.methodChoicesJa) {
        widgets.method->addItem(choice);
    }
    widgets.method->setCurrentIndex(face.methodIndex);
    line->addWidget(widgets.method, 1);
    widgets.edges = new QLabel(QStringLiteral("辺 %1").arg(face.edgeCount), widgets.row);
    line->addWidget(widgets.edges);
    widgets.status = new QLabel(face.statusJa, widgets.row);
    line->addWidget(widgets.status);
    widgets.make = new QCheckBox(QStringLiteral("作る"), widgets.row);
    widgets.make->setChecked(face.make);
    line->addWidget(widgets.make);
    return widgets;
}

//! 辺の連続の欄(隣のある辺だけ)。「辺 2 G1」を押すと G0 → G1 → G2 → G0 と回る。
//! 平面の輪では押せない(平面に連続は付かない。欄は出して、なぜ押せないかを tip で言う)。
void V2LoopFacesDock::AddEdgeLine(FaceRowWidgets& widgets, int faceIndex, const V2LoopFaceRow& face)
{
    if (face.edges.empty()) {
        return;
    }
    widgets.edgeLine = new QWidget(widgets.row);
    auto* line = new QHBoxLayout(widgets.edgeLine);
    line->setContentsMargins(18, 0, 0, 0);
    line->setSpacing(4);
    auto* caption = new QLabel(QStringLiteral("連続"), widgets.edgeLine);
    line->addWidget(caption);
    static const char* kNames[3] = {"G0", "G1", "G2"};
    for (const V2LoopEdgeCell& cell : face.edges) {
        const int order = std::clamp(cell.continuityIndex, 0, 2);
        auto* button = new QPushButton(
            QStringLiteral("辺 %1 %2").arg(cell.edge + 1).arg(QString::fromUtf8(kNames[order])),
            widgets.edgeLine);
        button->setEnabled(cell.allowed);
        button->setToolTip(cell.allowed
                ? cell.neighborJa + QStringLiteral("。押すと G0 → G1 → G2 と回ります(3D でその辺に置いて Tab でも)")
                : cell.neighborJa + QStringLiteral("。平面の輪には連続を付けられません(境界面に変えると付けられます)"));
        const int edge = cell.edge;
        QObject::connect(button, &QPushButton::clicked, this, [this, faceIndex, edge] {
            if (!loading_ && continuityHandler_) {
                continuityHandler_(faceIndex, edge);
            }
        });
        line->addWidget(button);
        widgets.edgeButtons.emplace_back(edge, button);
    }
    line->addStretch(1);
    widgets.column->addWidget(widgets.edgeLine);
}

V2LoopFacesDock::GapRowWidgets V2LoopFacesDock::MakeGapRow(int gap, const V2LoopGapRow& row)
{
    auto* body = widget();
    GapRowWidgets widgets;
    widgets.row = new QWidget(body);
    auto* line = new QHBoxLayout(widgets.row);
    line->setContentsMargins(0, 0, 0, 0);
    widgets.text = new QLabel(row.textJa, widgets.row);
    widgets.text->setWordWrap(true);
    line->addWidget(widgets.text, 1);
    widgets.close = new QPushButton(QStringLiteral("寄せる"), widgets.row);
    widgets.close->setEnabled(row.movable && !row.leave);
    line->addWidget(widgets.close);
    widgets.leave = new QPushButton(QStringLiteral("そのまま"), widgets.row);
    widgets.leave->setCheckable(true);
    widgets.leave->setChecked(row.leave);
    line->addWidget(widgets.leave);
    QObject::connect(widgets.close, &QPushButton::clicked, this, [this, gap] {
        if (!loading_ && closeGapHandler_) {
            closeGapHandler_(gap);
        }
    });
    QObject::connect(widgets.leave, &QPushButton::clicked, this, [this, gap] {
        if (!loading_ && leaveGapHandler_) {
            leaveGapHandler_(gap);
        }
    });
    return widgets;
}

void V2LoopFacesDock::RebuildFaceRows(const std::vector<V2LoopFaceRow>& faces)
{
    for (const FaceRowWidgets& widgets : faceRows_) {
        delete widgets.row;
    }
    faceRows_.clear();
    for (std::size_t index = 0; index < faces.size(); ++index) {
        const V2LoopFaceRow& face = faces[index];
        FaceRowWidgets widgets = MakeFaceRow(face);
        const int faceIndex = static_cast<int>(index);
        AddEdgeLine(widgets, faceIndex, face);
        facesLayout_->addWidget(widgets.row);
        QObject::connect(widgets.method, &QComboBox::currentIndexChanged, this,
            [this, faceIndex](int methodIndex) {
                if (!loading_ && methodHandler_) {
                    methodHandler_(faceIndex, methodIndex);
                }
            });
        QObject::connect(widgets.make, &QCheckBox::toggled, this, [this, faceIndex](bool checked) {
            if (!loading_ && makeHandler_) {
                makeHandler_(faceIndex, checked);
            }
        });
        faceRows_.push_back(widgets);
    }
}

void V2LoopFacesDock::RebuildGapSection(const V2LoopFacesView& view)
{
    for (const GapRowWidgets& widgets : gapRows_) {
        delete widgets.row;
    }
    gapRows_.clear();
    for (QLabel* label : splitLabels_) {
        delete label;
    }
    splitLabels_.clear();
    delete unusedLabel_;
    unusedLabel_ = nullptr;

    auto* body = widget();
    for (std::size_t index = 0; index < view.gaps.size(); ++index) {
        GapRowWidgets widgets = MakeGapRow(static_cast<int>(index), view.gaps[index]);
        gapsLayout_->addWidget(widgets.row);
        gapRows_.push_back(widgets);
    }
    for (const QString& line : view.splitsJa) {
        auto* label = new QLabel(line, body);
        label->setWordWrap(true);
        gapsLayout_->addWidget(label);
        splitLabels_.push_back(label);
    }
    if (!view.unusedJa.isEmpty()) {
        unusedLabel_ = new QLabel(view.unusedJa, body);
        unusedLabel_->setWordWrap(true);
        gapsLayout_->addWidget(unusedLabel_);
    }
}

void V2LoopFacesDock::ShowView(const V2LoopFacesView& view)
{
    loading_ = true;
    recentRow_->hide();
    planBody_->show();
    tolerance_->setValue(view.joinMm);
    RebuildFaceRows(view.faces);
    RebuildGapSection(view);
    summary_->setText(view.summaryJa);
    confirm_->setEnabled(view.canConfirm);
    loading_ = false;
}

void V2LoopFacesDock::SetMethodHandler(std::function<void(int, int)> handler)
{
    methodHandler_ = std::move(handler);
}

void V2LoopFacesDock::SetMakeHandler(std::function<void(int, bool)> handler)
{
    makeHandler_ = std::move(handler);
}

void V2LoopFacesDock::SetGapHandlers(std::function<void(int)> close, std::function<void(int)> leave)
{
    closeGapHandler_ = std::move(close);
    leaveGapHandler_ = std::move(leave);
}

void V2LoopFacesDock::SetToleranceHandler(std::function<void(double)> handler)
{
    toleranceHandler_ = std::move(handler);
}

void V2LoopFacesDock::SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel)
{
    confirmHandler_ = std::move(confirm);
    cancelHandler_ = std::move(cancel);
}

bool V2LoopFacesDock::ClickConfirm()
{
    return ClickIfUsable(confirm_);
}

bool V2LoopFacesDock::ClickCancel()
{
    return ClickIfUsable(cancel_);
}

bool V2LoopFacesDock::ClickCloseGap(int gap)
{
    if (gap < 0 || gap >= static_cast<int>(gapRows_.size())) {
        return false;
    }
    return ClickIfUsable(gapRows_[static_cast<std::size_t>(gap)].close);
}

bool V2LoopFacesDock::ClickLeaveGap(int gap)
{
    if (gap < 0 || gap >= static_cast<int>(gapRows_.size())) {
        return false;
    }
    return ClickIfUsable(gapRows_[static_cast<std::size_t>(gap)].leave);
}

bool V2LoopFacesDock::ChooseMethod(int face, int methodIndex)
{
    if (face < 0 || face >= static_cast<int>(faceRows_.size())) {
        return false;
    }
    return ChooseIfUsable(faceRows_[static_cast<std::size_t>(face)].method, methodIndex);
}

bool V2LoopFacesDock::ToggleMake(int face)
{
    if (face < 0 || face >= static_cast<int>(faceRows_.size())) {
        return false;
    }
    return ClickIfUsable(faceRows_[static_cast<std::size_t>(face)].make);
}

bool V2LoopFacesDock::TypeTolerance(double joinMm)
{
    return TypeIfUsable(tolerance_, joinMm);
}

void V2LoopFacesDock::ShowRecent(const QString& textJa)
{
    recent_->setText(textJa);
    recentRow_->show();
    planBody_->hide();
}

void V2LoopFacesDock::HideRecent()
{
    recent_->setText(QString());
    recentRow_->hide();
    planBody_->show();
}

void V2LoopFacesDock::SetReopenHandler(std::function<void()> handler)
{
    reopenHandler_ = std::move(handler);
}

bool V2LoopFacesDock::ClickReopen()
{
    if (recentRow_->isHidden() || !reopenHandler_) {
        return false;
    }
    reopenHandler_();
    return true;
}

QString V2LoopFacesDock::RecentTextJa() const
{
    return recent_->text();
}

void V2LoopFacesDock::SetContinuityHandler(std::function<void(int, int)> handler)
{
    continuityHandler_ = std::move(handler);
}

bool V2LoopFacesDock::CycleContinuity(int face, int edge)
{
    if (face < 0 || face >= static_cast<int>(faceRows_.size())) {
        return false;
    }
    for (const auto& [index, button] : faceRows_[static_cast<std::size_t>(face)].edgeButtons) {
        if (index == edge) {
            return ClickIfUsable(button);
        }
    }
    return false;
}

QString V2LoopFacesDock::EdgeCellTextJa(int face, int edge) const
{
    if (face < 0 || face >= static_cast<int>(faceRows_.size())) {
        return QString();
    }
    for (const auto& [index, button] : faceRows_[static_cast<std::size_t>(face)].edgeButtons) {
        if (index == edge) {
            return button->text();
        }
    }
    return QString();
}

int V2LoopFacesDock::FaceRowCount() const
{
    return static_cast<int>(faceRows_.size());
}

int V2LoopFacesDock::GapRowCount() const
{
    return static_cast<int>(gapRows_.size());
}

QString V2LoopFacesDock::FaceRowTextJa(int face) const
{
    if (face < 0 || face >= static_cast<int>(faceRows_.size())) {
        return QString();
    }
    const FaceRowWidgets& widgets = faceRows_[static_cast<std::size_t>(face)];
    QString text = widgets.number->text() + QStringLiteral(" ") + widgets.method->currentText()
        + QStringLiteral("  ") + widgets.edges->text();
    if (!widgets.status->text().isEmpty()) {
        text += QStringLiteral("  ") + widgets.status->text();
    }
    return text;
}

QString V2LoopFacesDock::GapRowTextJa(int gap) const
{
    if (gap < 0 || gap >= static_cast<int>(gapRows_.size())) {
        return QString();
    }
    return gapRows_[static_cast<std::size_t>(gap)].text->text();
}

QString V2LoopFacesDock::SummaryTextJa() const
{
    return summary_->text();
}

QString V2LoopFacesDock::SplitTextJa() const
{
    QString text;
    for (const QLabel* label : splitLabels_) {
        text += (text.isEmpty() ? QString() : QStringLiteral("\n")) + label->text();
    }
    return text;
}

bool V2LoopFacesDock::ConfirmEnabled() const
{
    return confirm_ != nullptr && confirm_->isEnabled();
}
