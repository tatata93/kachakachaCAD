#include "V2ShellSplitDock.h"
#include "V2PanelFrame.h"

#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <string>
#include <utility>

namespace {

[[nodiscard]] bool ClickIfUsable(QPushButton* button)
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

//! 名前・値・「解除」の 1 行(隠せるように入れ物ごと作る)。
[[nodiscard]] QWidget* MakeClearRow(QWidget* body, const QString& name, QLabel** value,
    QPushButton** clear)
{
    auto* row = new QWidget(body);
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->addWidget(new QLabel(name, row));
    *value = new QLabel(row);
    (*value)->setWordWrap(true);
    line->addWidget(*value, 1);
    *clear = new QPushButton(QStringLiteral("解除"), row);
    line->addWidget(*clear);
    return row;
}

//! 名前・数の欄の 1 行。
[[nodiscard]] QWidget* MakeSpinRow(QWidget* body, const QString& name, const QString& objectName,
    double minimum, QDoubleSpinBox** box)
{
    auto* row = new QWidget(body);
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->addWidget(new QLabel(name, row));
    *box = new QDoubleSpinBox(row);
    (*box)->setObjectName(objectName);
    (*box)->setRange(minimum, 100000.0);
    (*box)->setDecimals(3);
    (*box)->setSuffix(QStringLiteral(" mm"));
    line->addWidget(*box, 1);
    return row;
}

} // namespace

V2ShellSplitDock::V2ShellSplitDock(QWidget* parent)
    : QDockWidget(QStringLiteral("シェル・分割"), parent)
{
    setObjectName(QStringLiteral("shellSplitDock"));
    auto* body = new QWidget(this);
    setWidget(body);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("1. 作り方")));
    auto* methodRow = new QHBoxLayout();
    // 名前は短く(右の棚は狭い画面でも窓を押し広げない幅に収める。PC 2f92305 で「狭い画面でも
    // 部品がはみ出さない」が 1024px → 1122px になった)。説明はツールチップへ。
    const char* labels[2] = {"シェル", "分割"};
    const char* tips[2] = {"面を抜いて、内側へ肉厚だけ残します。", "いまの作業平面で 2 つに分けます。"};
    for (std::size_t index = 0; index < methods_.size(); ++index) {
        auto* button = new QPushButton(QString::fromUtf8(labels[index]), body);
        button->setToolTip(QString::fromUtf8(tips[index]));
        button->setCheckable(true);
        const int method = static_cast<int>(index);
        QObject::connect(button, &QPushButton::clicked, this, [this, method] {
            if (!loading_ && methodHandler_) {
                methodHandler_(method);
            }
        });
        methodRow->addWidget(button);
        methods_[index] = button;
    }
    layout->addLayout(methodRow);

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("2. 入力")));
    layout->addWidget(MakeClearRow(body, QStringLiteral("部品"), &partValue_, &clearPart_));
    facesRow_ = MakeClearRow(body, QStringLiteral("抜く面"), &facesValue_, &clearFaces_);
    layout->addWidget(facesRow_);
    QObject::connect(clearPart_, &QPushButton::clicked, this, [this] {
        if (!loading_ && clearPartHandler_) {
            clearPartHandler_();
        }
    });
    QObject::connect(clearFaces_, &QPushButton::clicked, this, [this] {
        if (!loading_ && clearFacesHandler_) {
            clearFacesHandler_();
        }
    });

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("3. 設定")));
    thicknessRow_ = MakeSpinRow(body, QStringLiteral("肉厚"), QStringLiteral("shellSplitThickness"),
        0.001, &thickness_);
    layout->addWidget(thicknessRow_);
    planeRow_ = new QWidget(body);
    auto* planeLine = new QHBoxLayout(planeRow_);
    planeLine->setContentsMargins(0, 0, 0, 0);
    planeLine->addWidget(new QLabel(QStringLiteral("平面"), planeRow_));
    planeValue_ = new QLabel(planeRow_);
    planeValue_->setWordWrap(true);
    planeLine->addWidget(planeValue_, 1);
    layout->addWidget(planeRow_);
    offsetRow_ = MakeSpinRow(body, QStringLiteral("ずらす"), QStringLiteral("shellSplitOffset"),
        -100000.0, &offset_);
    layout->addWidget(offsetRow_);
    QObject::connect(thickness_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!loading_ && thicknessHandler_) {
            thicknessHandler_(value);
        }
    });
    QObject::connect(offset_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!loading_ && offsetHandler_) {
            offsetHandler_(value);
        }
    });

    layout->addWidget(MakePanelSectionTitle(body, QStringLiteral("4. 状態")));
    status_ = new QLabel(body);
    status_->setWordWrap(true);
    layout->addWidget(status_);
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

void V2ShellSplitDock::ShowInput(const kachakacha::v2::app::ShellSplitInputState& state,
    const QString& partNameJa, const QString& planeTextJa,
    const std::vector<QString>& statusLinesJa, bool canConfirm)
{
    loading_ = true;
    const bool split = state.method == 1;
    for (std::size_t index = 0; index < methods_.size(); ++index) {
        methods_[index]->setChecked(static_cast<int>(index) == state.method);
    }
    partValue_->setText(state.part.IsNil() ? QStringLiteral("(選んでいません)") : partNameJa);
    clearPart_->setEnabled(!state.part.IsNil());
    facesRow_->setVisible(!split);
    thicknessRow_->setVisible(!split);
    planeRow_->setVisible(split);
    offsetRow_->setVisible(split);
    facesValue_->setText(state.faces.empty() ? QStringLiteral("(選んでいません)")
                                             : QStringLiteral("%1 枚").arg(static_cast<int>(state.faces.size())));
    clearFaces_->setEnabled(!state.faces.empty());
    thickness_->setValue(state.thicknessMm);
    planeValue_->setText(planeTextJa);
    offset_->setValue(state.splitOffsetMm);
    QString status;
    for (const QString& line : statusLinesJa) {
        status += (status.isEmpty() ? QString() : QStringLiteral("\n")) + line;
    }
    status_->setText(status);
    confirm_->setEnabled(canConfirm);
    loading_ = false;
}

void V2ShellSplitDock::SetMethodHandler(std::function<void(int)> handler)
{
    methodHandler_ = std::move(handler);
}

void V2ShellSplitDock::SetValueHandlers(std::function<void(double)> thickness,
    std::function<void(double)> offset)
{
    thicknessHandler_ = std::move(thickness);
    offsetHandler_ = std::move(offset);
}

void V2ShellSplitDock::SetClearHandlers(std::function<void()> part, std::function<void()> faces)
{
    clearPartHandler_ = std::move(part);
    clearFacesHandler_ = std::move(faces);
}

void V2ShellSplitDock::SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel)
{
    confirmHandler_ = std::move(confirm);
    cancelHandler_ = std::move(cancel);
}

bool V2ShellSplitDock::ClickMethod(int method)
{
    return method >= 0 && method < static_cast<int>(methods_.size())
        && ClickIfUsable(methods_[static_cast<std::size_t>(method)]);
}

bool V2ShellSplitDock::ClickClearFaces()
{
    return ClickIfUsable(clearFaces_);
}

bool V2ShellSplitDock::ClickConfirm()
{
    return ClickIfUsable(confirm_);
}

bool V2ShellSplitDock::TypeThickness(double thicknessMm)
{
    return TypeIfUsable(thickness_, thicknessMm);
}

bool V2ShellSplitDock::TypeOffset(double offsetMm)
{
    return TypeIfUsable(offset_, offsetMm);
}

QString V2ShellSplitDock::PartTextJa() const
{
    return partValue_->text();
}

QString V2ShellSplitDock::FacesTextJa() const
{
    return facesValue_->text();
}

QString V2ShellSplitDock::PlaneTextJa() const
{
    return planeValue_->text();
}

QString V2ShellSplitDock::StatusTextJa() const
{
    return status_->text();
}

bool V2ShellSplitDock::ConfirmEnabled() const
{
    return confirm_ != nullptr && confirm_->isEnabled();
}

bool V2ShellSplitDock::FacesRowShown() const
{
    return facesRow_ != nullptr && !facesRow_->isHidden();
}

bool V2ShellSplitDock::PlaneRowShown() const
{
    return planeRow_ != nullptr && !planeRow_->isHidden();
}

int V2ShellSplitDock::MethodShown() const
{
    return methods_[1]->isChecked() ? 1 : 0;
}
