#include "V2ExtrudeDock.h"

#include "kachakacha/app/ExtrudePlan.h"

#include <QComboBox>
#include <QDockWidget>
#include <QObject>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <iterator>
#include <string>
#include <utility>

namespace {

using kachakacha::v2::modeling::ExtrudeBooleanMode;

//! 操作の欄に並べる順。立体を選んでいるときだけ出す。
constexpr ExtrudeBooleanMode kBooleans[] = {
    ExtrudeBooleanMode::AddToPart,
    ExtrudeBooleanMode::SubtractFromPart,
    ExtrudeBooleanMode::NewPart,
};

} // namespace

V2ExtrudeDock::V2ExtrudeDock(QWidget* parent)
    : QDockWidget(QStringLiteral("押し出し"), parent)
{
    setObjectName(QStringLiteral("extrudeDock"));
    body_ = new QWidget(this);
    auto* layout = new QVBoxLayout(body_);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // 1. いまの入力。CADが何をどう読んだかを、まずここで見せる。
    layout->addWidget(new QLabel(QStringLiteral("入力"), body_));
    input_ = new QLabel(body_);
    input_->setWordWrap(true);
    layout->addWidget(input_);

    // 読み取りを外して選び直す道(EX-07)。読み取った当人が外し方まで出す。
    auto* reselect = new QHBoxLayout();
    reselectTarget_ = new QPushButton(QStringLiteral("対象を選び直す"), body_);
    reselectProfile_ = new QPushButton(QStringLiteral("輪郭を選び直す"), body_);
    reselect->addWidget(reselectTarget_);
    reselect->addWidget(reselectProfile_);
    layout->addLayout(reselect);

    // 2. いま変えられる主なもの。
    form_ = new QFormLayout();
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setSpacing(3);
    distance_ = new QDoubleSpinBox(body_);
    distance_->setRange(-100000.0, 100000.0);
    distance_->setDecimals(2);
    distance_->setSingleStep(1.0);
    distance_->setSuffix(QStringLiteral(" mm"));
    distance_->setValue(10.0);
    form_->addRow(QStringLiteral("距離"), distance_);

    direction_ = new QComboBox(body_);
    direction_->addItem(QStringLiteral("面に垂直"));
    direction_->addItem(QStringLiteral("作業平面に垂直"));
    form_->addRow(QStringLiteral("方向"), direction_);

    reverse_ = new QPushButton(QStringLiteral("方向を反転"), body_);
    form_->addRow(QString(), reverse_);

    extent_ = new QComboBox(body_);
    extent_->addItem(QStringLiteral("片側"));
    extent_->addItem(QStringLiteral("両側"));
    form_->addRow(QStringLiteral("範囲"), extent_);

    boolean_ = new QComboBox(body_);
    for (const ExtrudeBooleanMode mode : kBooleans) {
        boolean_->addItem(
            QString::fromUtf8(std::string(kachakacha::v2::app::ExtrudeBooleanNameJa(mode))
                    .c_str()));
    }
    form_->addRow(QStringLiteral("操作"), boolean_);
    layout->addLayout(form_);

    // 3. 結果と、確定・取消。
    result_ = new QLabel(body_);
    result_->setWordWrap(true);
    layout->addWidget(result_);

    details_ = new QPushButton(QStringLiteral("詳細..."), body_);
    layout->addWidget(details_);
    auto* buttons = new QHBoxLayout();
    confirm_ = new QPushButton(QStringLiteral("確定"), body_);
    cancel_ = new QPushButton(QStringLiteral("キャンセル"), body_);
    buttons->addWidget(confirm_);
    buttons->addWidget(cancel_);
    layout->addLayout(buttons);
    layout->addStretch(1);
    setWidget(body_);

    ConnectRows();
    ApplyRows();
}

//! 欄の便りを繋ぐ。組み立てと分けてあるのは、1関数100行の門のためである。
//! 切る場所は「並べる」と「繋ぐ」の境目にした。
void V2ExtrudeDock::ConnectRows()
{
    QObject::connect(distance_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!loading_ && distanceHandler_) {
            distanceHandler_(value);
        }
    });
    const auto option = [this] {
        if (!loading_ && optionHandler_) {
            optionHandler_();
        }
    };
    QObject::connect(direction_, &QComboBox::currentIndexChanged, this, [option] { option(); });
    QObject::connect(extent_, &QComboBox::currentIndexChanged, this, [option] { option(); });
    QObject::connect(boolean_, &QComboBox::currentIndexChanged, this, [this, option] {
        if (!loading_) {
            // 人が選んだ。以後、棚を出し直しても既定へ戻さない。
            operationChosenByUser_ = true;
        }
        option();
    });
    QObject::connect(reverse_, &QPushButton::clicked, this, [this, option] {
        reversed_ = !reversed_;
        option();
    });
    QObject::connect(confirm_, &QPushButton::clicked, this, [this] {
        if (confirmHandler_) {
            confirmHandler_();
        }
    });
    QObject::connect(cancel_, &QPushButton::clicked, this, [this] {
        if (cancelHandler_) {
            cancelHandler_();
        }
    });
    QObject::connect(details_, &QPushButton::clicked, this, [this] {
        if (detailsHandler_) {
            detailsHandler_();
        }
    });
    QObject::connect(reselectTarget_, &QPushButton::clicked, this, [this] {
        if (reselectTargetHandler_) {
            reselectTargetHandler_();
        }
    });
    QObject::connect(reselectProfile_, &QPushButton::clicked, this, [this] {
        if (reselectProfileHandler_) {
            reselectProfileHandler_();
        }
    });
}

//! 入力(加工する立体と輪郭)が同じか。演算の既定を当て直すかの判断に使う。
bool V2ExtrudeDock::SameInputs(const kachakacha::v2::app::ExtrudePlan& left,
    const kachakacha::v2::app::ExtrudePlan& right)
{
    return left.targetSolid == right.targetSolid && left.profiles == right.profiles;
}

void V2ExtrudeDock::ShowPlan(const kachakacha::v2::app::ExtrudePlan& plan,
    const QString& targetNameJa, const QString& profileNamesJa)
{
    const kachakacha::v2::app::ExtrudePlan previous = plan_;
    plan_ = plan;
    QString text;
    if (!targetNameJa.isEmpty()) {
        text += QStringLiteral("対象立体：%1\n").arg(targetNameJa);
    }
    if (!profileNamesJa.isEmpty()) {
        text += QStringLiteral("%1：%2\n")
                    .arg(plan.profileIsFace ? QStringLiteral("面") : QStringLiteral("輪郭"))
                    .arg(profileNamesJa);
    }
    if (!plan.needsJa.empty()) {
        // 足りないときは、何を選べばよいかをここに出す。
        text += QString::fromStdString(plan.needsJa);
    }
    input_->setText(text.trimmed());
    // 既定の操作を当てるのは **入力が変わったときだけ** である(R1 B4)。
    // 棚を出し直すたびに当て直すと、人が選んだ「足す/引く/新しい部品」が
    // 黙って戻る。入力が同じなら、選んだままにしておく。
    const bool inputChanged = plan.kind != previous.kind || !SameInputs(plan, previous);
    if (inputChanged || !operationChosenByUser_) {
        loading_ = true;
        for (int index = 0; index < static_cast<int>(std::size(kBooleans)); ++index) {
            if (kBooleans[index] == plan.defaultOperation) {
                boolean_->setCurrentIndex(index);
                break;
            }
        }
        loading_ = false;
        if (inputChanged) {
            operationChosenByUser_ = false;
        }
    }
    ApplyRows();
}

//! いまの入力で意味のない欄は出さない。
void V2ExtrudeDock::ApplyRows()
{
    using kachakacha::v2::app::ExtrudeInputKind;
    const bool hasTarget = plan_.kind == ExtrudeInputKind::SolidAndProfile
        || plan_.kind == ExtrudeInputKind::SolidAndFace;
    // 操作(追加・切削・新規)は、加工する立体があるときだけ意味がある。
    form_->setRowVisible(boolean_, hasTarget);
    const bool ready = plan_.readyToPreview;
    form_->setRowVisible(distance_, ready);
    form_->setRowVisible(direction_, ready);
    form_->setRowVisible(reverse_, ready);
    form_->setRowVisible(extent_, ready);
    confirm_->setEnabled(ready);
    details_->setEnabled(ready);
    // 外せる物があるときだけ出す。読み取っていない物の「選び直す」を出すと、
    // 押しても何も起きないボタンになる。
    reselectTarget_->setVisible(!plan_.targetSolid.IsNil());
    reselectProfile_->setVisible(!plan_.profiles.empty());
    result_->setText(ready
            ? QStringLiteral("矢印を引くか、距離を打ってください。Enter で確定します。")
            : QString());
}

void V2ExtrudeDock::SetDistanceMm(double value)
{
    loading_ = true;
    distance_->setValue(value);
    loading_ = false;
}

//! 人が距離を打ったのと同じ扱いにする。試験も本物と同じ道を通す。
//!
//! `SetDistanceMm` は棚を書き直すための道で、知らせを止めてある。
//! 試験がそれを使うと、矢印は前の距離のままになり、
//! 「棚に出ている値」と「実際に作る形」が食い違ったまま試験が通ってしまう。
void V2ExtrudeDock::TypeDistanceMm(double value)
{
    distance_->setValue(value);
    if (distanceHandler_) {
        distanceHandler_(value);
    }
}

double V2ExtrudeDock::DistanceMm() const
{
    return distance_->value();
}

kachakacha::v2::modeling::ExtrudeBooleanMode V2ExtrudeDock::BooleanMode() const
{
    const int index = boolean_->currentIndex();
    if (index < 0 || index >= static_cast<int>(std::size(kBooleans))) {
        return ExtrudeBooleanMode::NewPart;
    }
    return kBooleans[index];
}

bool V2ExtrudeDock::Symmetric() const
{
    return extent_->currentIndex() == 1;
}

bool V2ExtrudeDock::Reversed() const
{
    return reversed_;
}

void V2ExtrudeDock::SetDistanceHandler(std::function<void(double)> handler)
{
    distanceHandler_ = std::move(handler);
}

void V2ExtrudeDock::SetOptionHandler(std::function<void()> handler)
{
    optionHandler_ = std::move(handler);
}

void V2ExtrudeDock::SetActionHandlers(std::function<void()> confirm,
    std::function<void()> cancel, std::function<void()> details)
{
    confirmHandler_ = std::move(confirm);
    cancelHandler_ = std::move(cancel);
    detailsHandler_ = std::move(details);
}

void V2ExtrudeDock::SetReselectHandlers(std::function<void()> target,
    std::function<void()> profile)
{
    reselectTargetHandler_ = std::move(target);
    reselectProfileHandler_ = std::move(profile);
}

QString V2ExtrudeDock::InputTextJa() const
{
    return input_->text();
}

// isVisible() は親(棚・窓)が画面に出ていないと偽になる。
// 画面を出さない自己試験では、出すと決めた欄まで「出ていない」ことになる。
// ここで見たいのは「出すと決めたか」なので isHidden() の裏を返す。
bool V2ExtrudeDock::OperationRowShown() const
{
    return !boolean_->isHidden();
}

bool V2ExtrudeDock::ReselectTargetShown() const
{
    return !reselectTarget_->isHidden();
}

bool V2ExtrudeDock::ReselectProfileShown() const
{
    return !reselectProfile_->isHidden();
}

void V2ExtrudeDock::PressReselectTarget()
{
    if (reselectTargetHandler_) {
        reselectTargetHandler_();
    }
}

void V2ExtrudeDock::PressReselectProfile()
{
    if (reselectProfileHandler_) {
        reselectProfileHandler_();
    }
}

void V2ExtrudeDock::PressConfirm()
{
    if (confirmHandler_) {
        confirmHandler_();
    }
}

void V2ExtrudeDock::PressCancel()
{
    if (cancelHandler_) {
        cancelHandler_();
    }
}

void V2ExtrudeDock::PressReverse()
{
    reversed_ = !reversed_;
    if (optionHandler_) {
        optionHandler_();
    }
}

void V2ExtrudeDock::ChooseBoolean(kachakacha::v2::modeling::ExtrudeBooleanMode mode)
{
    for (int index = 0; index < static_cast<int>(std::size(kBooleans)); ++index) {
        if (kBooleans[index] == mode) {
            boolean_->setCurrentIndex(index);
            // 欄を選んだのと同じ扱いにする。試験も本物と同じ道を通す。
            operationChosenByUser_ = true;
            break;
        }
    }
}
