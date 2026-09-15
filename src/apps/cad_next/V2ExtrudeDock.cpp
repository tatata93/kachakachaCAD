#include "V2ExtrudeDock.h"

#include "kachakacha/app/ExtrudeOptions.h"
#include "kachakacha/app/ExtrudePlan.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QObject>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <iterator>
#include <string>
#include <utility>

namespace {

using kachakacha::v2::modeling::ExtrudeBooleanMode;

//! 「詳細で決めた向き」が並ぶ場所。選ばれている間だけ生える3つ目。
constexpr int kAdvancedDirectionIndex = 2;

//! 「詳細で決めた範囲」が並ぶ場所。選ばれている間だけ生える3つ目。
constexpr int kAdvancedExtentIndex = 2;

//! 操作の欄に並べる順。立体を選んでいるときだけ出す。
constexpr ExtrudeBooleanMode kBooleans[] = {
    ExtrudeBooleanMode::AddToPart,
    ExtrudeBooleanMode::SubtractFromPart,
    ExtrudeBooleanMode::NewPart,
};

} // namespace

//! 見出しと「1. 入力」(UI の正本の header-card と section 1)。
//!
//! 組み立てを分けてあるのは、1関数100行の門のためである。
//! 切る場所は「入力の欄」と「結果の欄」の境目にした。
void V2ExtrudeDock::BuildHeaderAndInputRows(QVBoxLayout* layout)
{
    // 見出しと、いまの様子(UI の正本の header-card)。
    state_ = new QLabel(body_);
    state_->setWordWrap(true);
    layout->addWidget(state_);
    auto* note = new QLabel(QStringLiteral(
        "選んだものを読み取りました。違うときは各欄の「選び直す」を押してください。"),
        body_);
    note->setWordWrap(true);
    layout->addWidget(note);

    // 1. 入力。**対象と輪郭を別の欄にする**(UI の正本「1. 入力」)。
    // 1本の文字列にしていたので、どちらを選び直すのか読めなかった。
    layout->addWidget(new QLabel(QStringLiteral("1. 入力"), body_));
    auto* targetRow = new QHBoxLayout();
    targetRow->addWidget(new QLabel(QStringLiteral("対象"), body_));
    targetValue_ = new QLabel(body_);
    targetValue_->setWordWrap(true);
    targetRow->addWidget(targetValue_, 1);
    reselectTarget_ = new QPushButton(QStringLiteral("選び直す"), body_);
    targetRow->addWidget(reselectTarget_);
    layout->addLayout(targetRow);

    auto* profileRow = new QHBoxLayout();
    profileValue_ = new QLabel(body_);
    profileValue_->setWordWrap(true);
    profileLabel_ = new QLabel(QStringLiteral("輪郭"), body_);
    profileRow->addWidget(profileLabel_);
    profileRow->addWidget(profileValue_, 1);
    reselectProfile_ = new QPushButton(QStringLiteral("選び直す"), body_);
    profileRow->addWidget(reselectProfile_);
    layout->addLayout(profileRow);

    // 読み取りの全文は、欄が狭いときのために残す。
    input_ = new QLabel(body_);
    input_->setWordWrap(true);
    input_->setVisible(false);

    layout->addWidget(new QLabel(QStringLiteral("2. 結果"), body_));

}

V2ExtrudeDock::V2ExtrudeDock(QWidget* parent)
    : QDockWidget(QStringLiteral("押し出し"), parent)
{
    setObjectName(QStringLiteral("extrudeDock"));
    body_ = new QWidget(this);
    auto* layout = new QVBoxLayout(body_);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    BuildHeaderAndInputRows(layout);

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

    // 出力プリセットと、その中身の4項目(UI の正本「2. 結果」)。
    // **見た目だけの欄ではない。**選んだとおりの物が文書に出来る。
    outputPreset_ = new QComboBox(body_);
    for (const auto preset : kachakacha::v2::app::ExtrudeOutputPresets()) {
        outputPreset_->addItem(QString::fromUtf8(
            std::string(kachakacha::v2::app::ExtrudeOutputPresetNameJa(preset)).c_str()));
    }
    form_->addRow(QStringLiteral("出力プリセット"), outputPreset_);

    outBody_ = new QCheckBox(QStringLiteral("ソリッド / 面"), body_);
    outStartWire_ = new QCheckBox(QStringLiteral("開始側の輪郭ワイヤー"), body_);
    outEndWire_ = new QCheckBox(QStringLiteral("押し出し先の輪郭ワイヤー"), body_);
    outSideWires_ = new QCheckBox(QStringLiteral("側面ワイヤー"), body_);
    outBody_->setChecked(true);
    form_->addRow(QStringLiteral("出力"), outBody_);
    form_->addRow(QString(), outStartWire_);
    form_->addRow(QString(), outEndWire_);
    form_->addRow(QString(), outSideWires_);
    layout->addLayout(form_);

    // 3. 状態と、下の3つのボタン(UI の正本「3. 状態」と actions)。
    layout->addWidget(new QLabel(QStringLiteral("3. 状態"), body_));
    result_ = new QLabel(body_);
    result_->setWordWrap(true);
    layout->addWidget(result_);

    details_ = new QPushButton(QStringLiteral("詳細..."), body_);
    layout->addWidget(details_);
    // 並びは正本のとおり: キャンセル / 再プレビュー / 確定。
    auto* buttons = new QHBoxLayout();
    cancel_ = new QPushButton(QStringLiteral("キャンセル Esc"), body_);
    rePreview_ = new QPushButton(QStringLiteral("再プレビュー"), body_);
    confirm_ = new QPushButton(QStringLiteral("確定 Enter"), body_);
    buttons->addWidget(cancel_);
    buttons->addWidget(rePreview_);
    buttons->addWidget(confirm_);
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
    // 出力プリセット → 4項目。
    QObject::connect(outputPreset_, &QComboBox::currentIndexChanged, this,
        [this, option] {
            if (loading_) {
                return;
            }
            SyncOutputRows(true);
            option();
        });
    // 4項目 → プリセットの名前。どちらを触っても表示が食い違わない。
    for (QCheckBox* box : {outBody_, outStartWire_, outEndWire_, outSideWires_}) {
        QObject::connect(box, &QCheckBox::toggled, this, [this, option] {
            if (loading_) {
                return;
            }
            SyncOutputRows(false);
            option();
        });
    }
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
    QObject::connect(rePreview_, &QPushButton::clicked, this, [this] {
        // 下見を作り直す。いまの欄のとおりに出し直すだけで、文書は変えない。
        if (optionHandler_) {
            optionHandler_();
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
    // 2欄に分けて出す。**どちらを選び直すのかが読める。**
    targetValue_->setText(targetNameJa.isEmpty() ? QStringLiteral("(選んでいません)")
                                                 : targetNameJa);
    profileLabel_->setText(plan.profileIsFace ? QStringLiteral("面")
                                              : QStringLiteral("輪郭"));
    profileValue_->setText(profileNamesJa.isEmpty() ? QStringLiteral("(選んでいません)")
                                                    : profileNamesJa);
    state_->setText(plan.readyToPreview ? QStringLiteral("押し出し — プレビュー可能")
                                        : QStringLiteral("押し出し — 入力が足りません"));
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
    // 面をつまんで押しているときは、向きは押す面が決める。
    // 選べない欄を出すと「選んだのに効かない」ことになるので、そのときは隠す。
    form_->setRowVisible(direction_, ready && !plan_.profileIsFace);
    form_->setRowVisible(reverse_, ready);
    form_->setRowVisible(extent_, ready);
    confirm_->setEnabled(ready);
    details_->setEnabled(ready);
    // 外せる物があるときだけ出す。読み取っていない物の「選び直す」を出すと、
    // 押しても何も起きないボタンになる。
    reselectTarget_->setVisible(!plan_.targetSolid.IsNil());
    reselectProfile_->setVisible(!plan_.profiles.empty());
    // 「状態」の中身は `ShowStatusLines` が持つ(UI の正本「3. 状態」)。
    // ここで書くと、通った道を出したあとに一言で上書きしてしまう。
    if (!ready) {
        result_->setText(QString());
    }
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

//! 棚で選んでいる向きの決め方。
//!
//! ふだん出ているのは「面に垂直」と「作業平面に垂直」の2つ。
//! 前者は選んだ輪郭(または面)の平面の法線、後者はいま作図している面の法線。
//! **ここを読まないと、欄は見た目だけで何も変わらない**(Codex R4 B1)。
//!
//! 詳細の窓で X 方向や自由な向きを決めたときは、3つ目としてその名前が出る。
//! 出さずにいると、棚は「作業平面に垂直」と見せながら別の向きへ押すことになり、
//! そのうえ棚の欄をひとつ触っただけで、決めた向きが黙って捨てられていた
//! (Codex P1-EXTRUDE-R6 B2)。
kachakacha::v2::modeling::ExtrudeDirectionMode V2ExtrudeDock::DirectionMode() const
{
    if (direction_ == nullptr) {
        return kachakacha::v2::modeling::ExtrudeDirectionMode::ProfileNormal;
    }
    const int index = direction_->currentIndex();
    if (index == kAdvancedDirectionIndex && advancedDirection_.has_value()) {
        return *advancedDirection_;
    }
    return index == 1 ? kachakacha::v2::modeling::ExtrudeDirectionMode::WorkPlaneNormal
                      : kachakacha::v2::modeling::ExtrudeDirectionMode::ProfileNormal;
}

void V2ExtrudeDock::ChooseDirection(kachakacha::v2::modeling::ExtrudeDirectionMode mode)
{
    if (direction_ == nullptr) {
        return;
    }
    // 出し入れの途中で「人が選んだ」ことにしない。棚を映すだけである。
    const bool blocked = direction_->blockSignals(true);
    const bool plain
        = mode == kachakacha::v2::modeling::ExtrudeDirectionMode::ProfileNormal
        || mode == kachakacha::v2::modeling::ExtrudeDirectionMode::WorkPlaneNormal;
    if (plain) {
        advancedDirection_.reset();
        if (direction_->count() > kAdvancedDirectionIndex) {
            direction_->removeItem(kAdvancedDirectionIndex);
        }
        direction_->setCurrentIndex(
            mode == kachakacha::v2::modeling::ExtrudeDirectionMode::WorkPlaneNormal ? 1
                                                                                    : 0);
    } else {
        advancedDirection_ = mode;
        const QString label
            = QStringLiteral("詳細で決めた向き(%1)")
                  .arg(QString::fromUtf8(std::string(
                      kachakacha::v2::app::ExtrudeDirectionNameJa(mode)).c_str()));
        if (direction_->count() > kAdvancedDirectionIndex) {
            direction_->setItemText(kAdvancedDirectionIndex, label);
        } else {
            direction_->addItem(label);
        }
        direction_->setCurrentIndex(kAdvancedDirectionIndex);
    }
    direction_->blockSignals(blocked);
}

kachakacha::v2::app::ExtrudeOutputs V2ExtrudeDock::Outputs() const
{
    kachakacha::v2::app::ExtrudeOutputs outputs;
    outputs.body = outBody_ != nullptr && outBody_->isChecked();
    outputs.startWire = outStartWire_ != nullptr && outStartWire_->isChecked();
    outputs.endWire = outEndWire_ != nullptr && outEndWire_->isChecked();
    outputs.sideWires = outSideWires_ != nullptr && outSideWires_->isChecked();
    return outputs;
}

void V2ExtrudeDock::ShowOutputs(const kachakacha::v2::app::ExtrudeOutputs& outputs)
{
    if (outBody_ == nullptr) {
        return;
    }
    const bool was = loading_;
    loading_ = true;
    outBody_->setChecked(outputs.body);
    outStartWire_->setChecked(outputs.startWire);
    outEndWire_->setChecked(outputs.endWire);
    outSideWires_->setChecked(outputs.sideWires);
    SyncOutputRows(false);
    loading_ = was;
}

//! 出力の欄どうしを合わせる。
//!
//! プリセットを選んだら4項目をそのとおりにする。4項目を触ったら、
//! いまの組み合わせに当たるプリセットの名前へ変える(無ければ「カスタム」)。
//! **どちらを触っても、画面の2つの表示が食い違わない。**
void V2ExtrudeDock::SyncOutputRows(bool fromPreset)
{
    using kachakacha::v2::app::ExtrudeOutputPresets;
    if (outputPreset_ == nullptr || outBody_ == nullptr) {
        return;
    }
    const auto& presets = ExtrudeOutputPresets();
    const bool was = loading_;
    loading_ = true;
    if (fromPreset) {
        const int index = outputPreset_->currentIndex();
        if (index >= 0 && index < static_cast<int>(presets.size())
            && presets[static_cast<std::size_t>(index)]
                != kachakacha::v2::app::ExtrudeOutputPreset::Custom) {
            const auto wanted = kachakacha::v2::app::OutputsForPreset(
                presets[static_cast<std::size_t>(index)]);
            outBody_->setChecked(wanted.body);
            outStartWire_->setChecked(wanted.startWire);
            outEndWire_->setChecked(wanted.endWire);
            outSideWires_->setChecked(wanted.sideWires);
        }
    } else {
        const auto preset = kachakacha::v2::app::PresetForOutputs(Outputs());
        for (std::size_t index = 0; index < presets.size(); ++index) {
            if (presets[index] == preset) {
                outputPreset_->setCurrentIndex(static_cast<int>(index));
                break;
            }
        }
    }
    // ソリッドを作らないなら、足す・引くは起きない。触れる欄にしておくと
    // 「選んだのに効かない」ことになる(オーナー指示)。
    if (boolean_ != nullptr) {
        const bool applies = outBody_->isChecked();
        boolean_->setEnabled(applies);
        boolean_->setToolTip(applies
                ? QStringLiteral("足す・引く・新しい部品。加工する立体があるときに効きます。")
                : QStringLiteral("ソリッドを作らないので、足す・引くは起きません。"));
    }
    loading_ = was;
}

void V2ExtrudeDock::ShowStatusLines(const std::vector<QString>& lines, bool canConfirm)
{
    if (result_ != nullptr) {
        QString text;
        for (const QString& line : lines) {
            if (!text.isEmpty()) {
                text += QStringLiteral("\n");
            }
            text += line;
        }
        result_->setText(text);
    }
    if (confirm_ != nullptr) {
        confirm_->setEnabled(canConfirm);
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
    return ExtentMode() == kachakacha::v2::modeling::ExtrudeExtentMode::SymmetricDistance;
}

//! 棚で選んでいる終端。
//!
//! ふだん出ているのは「片側」と「両側」の2つ。詳細の窓で「選んだ面まで」や
//! 「両方向に別々の距離」を決めたときは、3つ目としてその名前が出る。
//! 出さずにいると、棚の欄をひとつ触るだけでその終端が黙って「片側」へ
//! 戻っていた。向きで起きていたのと同じことである。
kachakacha::v2::modeling::ExtrudeExtentMode V2ExtrudeDock::ExtentMode() const
{
    if (extent_ == nullptr) {
        return kachakacha::v2::modeling::ExtrudeExtentMode::Distance;
    }
    const int index = extent_->currentIndex();
    if (index == kAdvancedExtentIndex && advancedExtent_.has_value()) {
        return *advancedExtent_;
    }
    return index == 1 ? kachakacha::v2::modeling::ExtrudeExtentMode::SymmetricDistance
                      : kachakacha::v2::modeling::ExtrudeExtentMode::Distance;
}

void V2ExtrudeDock::ChooseExtent(kachakacha::v2::modeling::ExtrudeExtentMode mode)
{
    if (extent_ == nullptr) {
        return;
    }
    const bool blocked = extent_->blockSignals(true);
    const bool plain = mode == kachakacha::v2::modeling::ExtrudeExtentMode::Distance
        || mode == kachakacha::v2::modeling::ExtrudeExtentMode::SymmetricDistance;
    if (plain) {
        advancedExtent_.reset();
        if (extent_->count() > kAdvancedExtentIndex) {
            extent_->removeItem(kAdvancedExtentIndex);
        }
        extent_->setCurrentIndex(
            mode == kachakacha::v2::modeling::ExtrudeExtentMode::SymmetricDistance ? 1 : 0);
    } else {
        advancedExtent_ = mode;
        const QString label
            = QStringLiteral("詳細で決めた範囲(%1)")
                  .arg(QString::fromUtf8(std::string(
                      kachakacha::v2::app::ExtrudeExtentNameJa(mode)).c_str()));
        if (extent_->count() > kAdvancedExtentIndex) {
            extent_->setItemText(kAdvancedExtentIndex, label);
        } else {
            extent_->addItem(label);
        }
        extent_->setCurrentIndex(kAdvancedExtentIndex);
    }
    extent_->blockSignals(blocked);
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
