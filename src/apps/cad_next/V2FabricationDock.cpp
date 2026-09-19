#include "V2FabricationDock.h"

#include "kachakacha/app/ApproxInput.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QSlider>
#include <QScrollArea>
#include <QString>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

using kachakacha::v2::app::FabricationChoice;
using kachakacha::v2::app::FabricationMethod;
using kachakacha::v2::app::ParameterId;
using kachakacha::v2::fabrication::FreezeOutput;

namespace {

//! 分割軸の欄の並び: 自動 / U / V。値は 2 / 0 / 1。
constexpr int kSplitAxisValues[3] = {2, 0, 1};

[[nodiscard]] QDoubleSpinBox* MakeCount(QWidget* parent, double minimum, double maximum)
{
    auto* field = new QDoubleSpinBox(parent);
    field->setRange(minimum, maximum);
    field->setDecimals(0);
    field->setSingleStep(1.0);
    return field;
}

[[nodiscard]] QDoubleSpinBox* MakeMm(QWidget* parent, double minimum, double maximum, double step)
{
    auto* field = new QDoubleSpinBox(parent);
    field->setRange(minimum, maximum);
    field->setDecimals(3);
    field->setSingleStep(step);
    field->setSuffix(QStringLiteral(" mm"));
    return field;
}

[[nodiscard]] QPushButton* MakeRun(QWidget* parent, const QString& text, const char* command,
    V2FabricationDock* dock)
{
    auto* button = new QPushButton(text, parent);
    QObject::connect(button, &QPushButton::clicked, dock, [dock, command] { dock->PressRun(command); });
    return button;
}

} // namespace

V2FabricationDock::V2FabricationDock(QWidget* parent)
    : QDockWidget(QStringLiteral("製作"), parent)
{
    setObjectName(QStringLiteral("fabricationDock"));
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    model_ = new QLabel(QStringLiteral("近似モデル: (なし)"), body);
    model_->setWordWrap(true);
    layout->addWidget(model_);

    stages_ = new QTabWidget(body);
    stages_->setObjectName(QStringLiteral("fabricationStages"));
    stages_->setDocumentMode(true);
    layout->addWidget(stages_, 1);

    auto* approximationPage = new QWidget(stages_);
    auto* approximationLayout = new QVBoxLayout(approximationPage);
    approximationLayout->setContentsMargins(4, 8, 4, 4);
    approximationLayout->setSpacing(4);
    // 最上段に「対象と候補」。道具を押した直後から、3D で押した対象と
    // 3通りの作り方の比べ(部材数・最大のずれ)がここに出る(引継ぎ 2026-09-17 の 3)。
    approximationLayout->addWidget(BuildApproxInput(approximationPage));
    approximationLayout->addWidget(BuildOptionsForm(approximationPage));

    auto* buttons = new QWidget(approximationPage);
    auto* buttonLayout = new QVBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(2);
    confirmApprox_ = MakeRun(buttons, QStringLiteral("製作モデルを作る(確定 Enter)"), "fabrication.create", this);
    buttonLayout->addWidget(confirmApprox_);
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("近似プレビューを更新"), "fabrication.preview_update", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("選択境界を開口 / 折り線にする"), "fabrication.assign_role", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("選択した開いた線を切れ目にする"), "fabrication.assign_relief_cut", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("接続する部材の範囲を決める"), "fabrication.set_connection_scope", this));
    approximationLayout->addWidget(buttons);
    approximationLayout->addStretch(1);
    stages_->addTab(approximationPage, QStringLiteral("1 近似モデル"));

    auto* bendPage = new QWidget(stages_);
    auto* bendLayout = new QVBoxLayout(bendPage);
    bendLayout->setContentsMargins(4, 8, 4, 4);
    bendLayout->setSpacing(4);
    bendLayout->addWidget(BuildBendSection(bendPage));
    // 部材の編集は曲げと同じ段に置く(引継ぎ 2026-09-17 の 5)。
    // 分ける・1つにする・切れ目・展開の基準は、曲げながら決めるものだからである。
    bendLayout->addWidget(BuildPartEditSection(bendPage));
    auto* freezeButtons = new QWidget(bendPage);
    auto* freezeLayout = new QVBoxLayout(freezeButtons);
    freezeLayout->setContentsMargins(0, 0, 0, 0);
    freezeLayout->setSpacing(2);
    freezeLayout->addWidget(MakeRun(freezeButtons, QStringLiteral("現在の曲げ状態から形を作る"), "fabrication.freeze_state", this));
    freezeLayout->addWidget(MakeRun(freezeButtons, QStringLiteral("展開図(型紙)を作る"), "fabrication.create_pattern", this));
    bendLayout->addWidget(freezeButtons);
    bendLayout->addStretch(1);
    stages_->addTab(bendPage, QStringLiteral("2 部材の編集・曲げ確認"));

    auto* materialPage = new QWidget(stages_);
    auto* materialLayout = new QVBoxLayout(materialPage);
    materialLayout->setContentsMargins(4, 8, 4, 4);
    materialLayout->addWidget(BuildRangeAndMaterial(materialPage));
    materialLayout->addStretch(1);
    stages_->addTab(materialPage, QStringLiteral("3 材料・範囲"));

    message_ = new QLabel(body);
    message_->setWordWrap(true);
    layout->addWidget(message_);
    layout->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body);
    setWidget(scroll);
    Connect();
    RefreshMethodRows();
}

QWidget* V2FabricationDock::BuildApproxInput(QWidget* body)
{
    auto* box = new QWidget(body);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(QStringLiteral("対象"), box));
    sourcesValue_ = new QLabel(QStringLiteral("(3D で面か立体を押してください)"), box);
    sourcesValue_->setWordWrap(true);
    row->addWidget(sourcesValue_, 1);
    clearSources_ = new QPushButton(QStringLiteral("解除"), box);
    QObject::connect(clearSources_, &QPushButton::clicked, this, [this] {
        if (!loading_ && clearSourcesHandler_) {
            clearSourcesHandler_();
        }
    });
    row->addWidget(clearSources_);
    layout->addLayout(row);
    // 作り方(正本の methods)。候補はいつも全部作り、作り方は既定の候補を決める。
    layout->addWidget(new QLabel(QStringLiteral("作り方"), box));
    auto* policyRow = new QHBoxLayout();
    policyRow->setSpacing(2);
    const auto& specs = kachakacha::v2::app::ApproxPolicySpecs();
    for (std::size_t index = 0; index < specs.size(); ++index) {
        auto* button = new QPushButton(QString::fromUtf8(specs[index].labelJa.c_str()), box);
        button->setCheckable(true);
        button->setToolTip(QString::fromUtf8(specs[index].hintJa.c_str()));
        const int at = static_cast<int>(index);
        QObject::connect(button, &QPushButton::clicked, this, [this, at] {
            if (!loading_ && policyHandler_) {
                policyHandler_(at);
            }
        });
        policyRow->addWidget(button);
        policies_.push_back(button);
    }
    layout->addLayout(policyRow);
    ShowPolicy(0);
    layout->addWidget(new QLabel(QStringLiteral("候補(実際に作って比べます)"), box));
    for (int index = 0; index < 3; ++index) {
        auto* button = new QPushButton(box);
        button->setCheckable(true);
        QObject::connect(button, &QPushButton::clicked, this, [this, index] {
            if (!loading_ && candidateHandler_) {
                candidateHandler_(index);
            }
        });
        candidates_.push_back(button);
        layout->addWidget(button);
    }
    ShowApproxInput(QString(), {}, -1, false);
    return box;
}

void V2FabricationDock::ShowApproxInput(const QString& sourcesJa,
    const std::vector<QString>& candidateLinesJa, int selectedCandidate, bool canConfirm)
{
    loading_ = true;
    sourcesJa_ = sourcesJa;
    if (sourcesValue_ != nullptr) {
        sourcesValue_->setText(sourcesJa.isEmpty()
                ? QStringLiteral("(3D で面か立体を押してください)")
                : sourcesJa);
    }
    if (clearSources_ != nullptr) {
        clearSources_->setEnabled(!sourcesJa.isEmpty());
    }
    for (std::size_t index = 0; index < candidates_.size(); ++index) {
        QPushButton* button = candidates_[index];
        button->setText(index < candidateLinesJa.size() ? candidateLinesJa[index]
                                                        : QStringLiteral("—"));
        button->setChecked(static_cast<int>(index) == selectedCandidate);
        button->setEnabled(index < candidateLinesJa.size());
    }
    if (confirmApprox_ != nullptr) {
        confirmApprox_->setEnabled(canConfirm);
    }
    loading_ = false;
}

void V2FabricationDock::SetCandidateHandler(std::function<void(int)> handler)
{
    candidateHandler_ = std::move(handler);
}

void V2FabricationDock::SetClearSourcesHandler(std::function<void()> handler)
{
    clearSourcesHandler_ = std::move(handler);
}

bool V2FabricationDock::ClickCandidate(int candidate)
{
    if (candidate < 0 || candidate >= static_cast<int>(candidates_.size())) {
        return false;
    }
    QPushButton* button = candidates_[static_cast<std::size_t>(candidate)];
    if (!button->isVisible() || !button->isEnabled()) {
        return false;
    }
    button->click();
    return true;
}

bool V2FabricationDock::ClickClearSources()
{
    if (clearSources_ == nullptr || !clearSources_->isVisible() || !clearSources_->isEnabled()) {
        return false;
    }
    clearSources_->click();
    return true;
}

int V2FabricationDock::SelectedCandidateShown() const
{
    for (std::size_t index = 0; index < candidates_.size(); ++index) {
        if (candidates_[index]->isChecked()) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

QString V2FabricationDock::CandidateTextJa(int candidate) const
{
    if (candidate < 0 || candidate >= static_cast<int>(candidates_.size())) {
        return QString();
    }
    return candidates_[static_cast<std::size_t>(candidate)]->text();
}

//! 対象の欄の中身。空のときの「(3D で面か立体を押してください)」は案内であって中身ではない。
QString V2FabricationDock::SourcesTextJa() const
{
    return sourcesJa_;
}

QWidget* V2FabricationDock::BuildOptionsForm(QWidget* body)
{
    auto* formWidget = new QWidget(body);
    form_ = new QFormLayout(formWidget);
    form_->setContentsMargins(0, 0, 0, 0);
    method_ = new QComboBox(formWidget);
    method_->addItem(QStringLiteral("V1方式(帯へ近似し直す)"));
    method_->addItem(QStringLiteral("V2方式(面を分類して展開)"));
    form_->addRow(QStringLiteral("方式"), method_);
    splitAxis_ = new QComboBox(formWidget);
    splitAxis_->addItem(QStringLiteral("自動(曲がっている方向を横切る)"));
    splitAxis_->addItem(QStringLiteral("U 方向で切る"));
    splitAxis_->addItem(QStringLiteral("V 方向で切る"));
    form_->addRow(QStringLiteral("分割軸"), splitAxis_);
    automatic_ = new QCheckBox(QStringLiteral("許すずれから自動で切る"), formWidget);
    automatic_->setChecked(true);
    form_->addRow(QStringLiteral("境界"), automatic_);
    manual_ = new QLineEdit(formWidget);
    manual_->setPlaceholderText(QStringLiteral("0.3, 0.6(分割軸の 0〜1)"));
    form_->addRow(QStringLiteral("手動境界"), manual_);
    // 立体を面ごとに分ける。箱なら6枚の型紙になる。
    // 切らないと、立体からは「平らな1枚」しか型紙にならない。
    splitSolidFaces_ = new QCheckBox(QStringLiteral("立体を面ごとに分ける"), formWidget);
    splitSolidFaces_->setChecked(false);
    form_->addRow(QStringLiteral("立体の扱い"), splitSolidFaces_);
    maxParts_ = MakeCount(formWidget, 1.0, 200.0);
    maxParts_->setValue(12.0);
    form_->addRow(QStringLiteral("部材数の上限"), maxParts_);
    minWidth_ = MakeMm(formWidget, 0.0, 1000.0, 0.5);
    minWidth_->setValue(4.0);
    form_->addRow(QStringLiteral("部材の最小幅"), minWidth_);
    // 切れ目の上限。紙とプラ板と真鍮で、残してよい幅は違う。
    // 隠した既定値にすると、断られた理由が分かっても直せない。
    reliefDepth_ = MakeCount(formWidget, 1.0, 99.0);
    reliefDepth_->setSuffix(QStringLiteral(" %"));
    reliefDepth_->setValue(55.0);
    reliefDepth_->setToolTip(
        QStringLiteral("部材の幅に対する、切れ目の深さの上限。100% は幅いっぱいで、"
                       "切った時点で2枚になります。"));
    form_->addRow(QStringLiteral("切れ目の深さの上限"), reliefDepth_);
    reliefLigament_ = MakeMm(formWidget, 0.01, 100.0, 0.1);
    reliefLigament_->setValue(0.5);
    reliefLigament_->setToolTip(
        QStringLiteral("切れ目の先から向こう側の縁まで、最低これだけ残します。"
                       "既定は 0.3mm 厚のプラ板の値です。"));
    form_->addRow(QStringLiteral("切れ目の先に残す幅"), reliefLigament_);
    fidelity_ = MakeCount(formWidget, 1.0, 20.0);
    fidelity_->setValue(6.0);
    form_->addRow(QStringLiteral("再現度"), fidelity_);
    thickness_ = MakeMm(formWidget, 0.0, 100.0, 0.1);
    thickness_->setToolTip(QStringLiteral("数の棚の「板厚」と同じ値です。"));
    form_->addRow(QStringLiteral("板厚"), thickness_);
    deviation_ = MakeMm(formWidget, 0.0, 100.0, 0.05);
    deviation_->setToolTip(QStringLiteral("数の棚の「展開で許すずれ」と同じ値です。"));
    form_->addRow(QStringLiteral("許すずれ"), deviation_);
    return formWidget;

}

QWidget* V2FabricationDock::BuildRangeAndMaterial(QWidget* body)
{
    // 範囲(V1 の plate_range)と材料・積層(plate の材料、plate_laminate)。
    auto* widget = new QWidget(body);
    auto* form = new QFormLayout(widget);
    form->setContentsMargins(0, 0, 0, 0);
    const auto makeUnit = [widget](double value) {
        auto* field = new QDoubleSpinBox(widget);
        field->setRange(0.0, 1.0);
        field->setDecimals(3);
        field->setSingleStep(0.05);
        field->setValue(value);
        return field;
    };
    auto* uRow = new QWidget(widget);
    auto* uLayout = new QHBoxLayout(uRow);
    uLayout->setContentsMargins(0, 0, 0, 0);
    rangeUMin_ = makeUnit(0.0);
    rangeUMax_ = makeUnit(1.0);
    uLayout->addWidget(rangeUMin_);
    uLayout->addWidget(new QLabel(QStringLiteral("〜"), uRow));
    uLayout->addWidget(rangeUMax_);
    form->addRow(QStringLiteral("範囲 u"), uRow);
    auto* vRow = new QWidget(widget);
    auto* vLayout = new QHBoxLayout(vRow);
    vLayout->setContentsMargins(0, 0, 0, 0);
    rangeVMin_ = makeUnit(0.0);
    rangeVMax_ = makeUnit(1.0);
    vLayout->addWidget(rangeVMin_);
    vLayout->addWidget(new QLabel(QStringLiteral("〜"), vRow));
    vLayout->addWidget(rangeVMax_);
    form->addRow(QStringLiteral("範囲 v"), vRow);

    material_ = new QLineEdit(widget);
    material_->setPlaceholderText(QStringLiteral("プラ板 0.5 など"));
    form->addRow(QStringLiteral("材料"), material_);
    layers_ = MakeCount(widget, 1.0, 20.0);
    layers_->setValue(1.0);
    form->addRow(QStringLiteral("積層の枚数"), layers_);
    applyMaterial_ = new QPushButton(QStringLiteral("材料と積層を選んだものに当てる"), widget);
    form->addRow(applyMaterial_);
    return widget;
}

QWidget* V2FabricationDock::BuildBendSection(QWidget* body)
{
    auto* bendWidget = new QWidget(body);
    auto* bend = new QFormLayout(bendWidget);
    bend->setContentsMargins(0, 0, 0, 0);
    auto* assemblyRow = new QWidget(bendWidget);
    auto* assemblyLayout = new QHBoxLayout(assemblyRow);
    assemblyLayout->setContentsMargins(0, 0, 0, 0);
    assembly_ = new QDoubleSpinBox(assemblyRow);
    assembly_->setRange(0.0, 100.0);
    assembly_->setDecimals(1);
    assembly_->setSingleStep(5.0);
    assembly_->setSuffix(QStringLiteral(" %"));
    assembly_->setValue(100.0);
    applyAssembly_ = new QPushButton(QStringLiteral("当てる"), assemblyRow);
    assemblyLayout->addWidget(assembly_);
    assemblyLayout->addWidget(applyAssembly_);
    bend->addRow(QStringLiteral("組立率"), assemblyRow);
    // 曲げ状態(正本 F-10): スライダ 0〜100 と基準値 0/25/50/75/100。
    // どちらも「組立率を打って当てる」と同じ道を通る。別の道を作らない。
    auto* bendRow = new QWidget(bendWidget);
    auto* bendLayout = new QHBoxLayout(bendRow);
    bendLayout->setContentsMargins(0, 0, 0, 0);
    bendLayout->setSpacing(2);
    bendSlider_ = new QSlider(Qt::Horizontal, bendRow);
    bendSlider_->setRange(0, 100);
    bendSlider_->setValue(100);
    bendSlider_->setToolTip(QStringLiteral("0% = 実際の展開、100% = 目標の形。離すと当てます。"));
    bendLayout->addWidget(bendSlider_, 1);
    for (const int percent : {0, 25, 50, 75, 100}) {
        auto* preset = new QPushButton(QStringLiteral("%1").arg(percent), bendRow);
        preset->setToolTip(QStringLiteral("組立率を %1% にして当てます。").arg(percent));
        QObject::connect(preset, &QPushButton::clicked, this, [this, percent] {
            TypeAssemblyPercent(static_cast<double>(percent));
            PressApplyAssembly();
        });
        bendLayout->addWidget(preset);
        bendPresets_.push_back(preset);
    }
    QObject::connect(bendSlider_, &QSlider::sliderReleased, this, [this] {
        TypeAssemblyPercent(static_cast<double>(bendSlider_->value()));
        PressApplyAssembly();
    });
    bend->addRow(QStringLiteral("曲げ状態"), bendRow);
    // 半径。曲げ具合と同じことの言い換えである。どちらから入れてもよい(§30)。
    auto* radiusRow = new QWidget(bendWidget);
    auto* radiusLayout = new QHBoxLayout(radiusRow);
    radiusLayout->setContentsMargins(0, 0, 0, 0);
    radius_ = new QDoubleSpinBox(radiusRow);
    radius_->setRange(0.0, 100000.0);
    radius_->setDecimals(2);
    radius_->setSingleStep(0.5);
    radius_->setSuffix(QStringLiteral(" mm"));
    radius_->setToolTip(QStringLiteral(
        "いまの組立率での半径です。手元の丸棒や治具の径へ合わせたいときは、"
        "その値を入れて「固定」を押してください。近似をやり直しても戻りません。"));
    lockRadius_ = new QPushButton(QStringLiteral("固定"), radiusRow);
    radiusState_ = new QLabel(QStringLiteral("自動"), radiusRow);
    radiusLayout->addWidget(radius_);
    radiusLayout->addWidget(lockRadius_);
    radiusLayout->addWidget(radiusState_);
    bend->addRow(QStringLiteral("半径"), radiusRow);
    parts_ = new QLineEdit(bendWidget);
    parts_->setPlaceholderText(QStringLiteral("空なら全部。1, 3 のように部材番号"));
    parts_->setToolTip(QStringLiteral(
        "部材番号(1 から)を挙げると、その部材だけが曲がります(V1 と同じ)。"
        "空にして当てると全体が動き、部材ごとの値は捨てます。"));
    bend->addRow(QStringLiteral("曲げる部材"), parts_);
    freeze_ = new QComboBox(bendWidget);
    freeze_->addItem(QStringLiteral("ワイヤーのみ"));
    freeze_->addItem(QStringLiteral("部品のみ"));
    freeze_->addItem(QStringLiteral("両方"));
    bend->addRow(QStringLiteral("固定で作るもの"), freeze_);
    return bendWidget;
}

QWidget* V2FabricationDock::BuildPartEditSection(QWidget* body)
{
    auto* editWidget = new QWidget(body);
    auto* layout = new QVBoxLayout(editWidget);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(new QLabel(QStringLiteral("部材の編集(「曲げる部材」の番号に当てる)"),
        editWidget));
    layout->addWidget(MakeRun(editWidget, QStringLiteral("部材を分ける(1度目は下見)"),
        "fabrication.split_part", this));
    layout->addWidget(MakeRun(editWidget, QStringLiteral("部材を1つにする(1度目は下見)"),
        "fabrication.merge_parts", this));
    layout->addWidget(MakeRun(editWidget, QStringLiteral("選択した開いた線を切れ目にする"),
        "fabrication.assign_relief_cut", this));
    layout->addWidget(MakeRun(editWidget, QStringLiteral("展開の基準にする辺"),
        "fabrication.set_unfold_base", this));
    return editWidget;
}

//! 組立率を打った。同じ部材の半径へ言い換える(R(p) = R100 × 100 / p)。
void V2FabricationDock::SyncRadiusFromPercent()
{
    if (loading_ || !bendShown_ || radius_ == nullptr) {
        return;
    }
    loading_ = true;
    const auto shown =
        kachakacha::v2::fabrication::RadiusAtPercent(shownBend_, assembly_->value());
    radius_->setValue(shown.value_or(0.0));
    radius_->setEnabled(shown.has_value());
    loading_ = false;
}

//! 半径を打った。同じ部材の組立率へ言い換える(p = R100 × 100 / R)。
//! この板では曲げられない半径(100% より小さい)なら、組立率は動かさない。
void V2FabricationDock::SyncPercentFromRadius()
{
    if (loading_ || !bendShown_ || radius_ == nullptr) {
        return;
    }
    const auto percent =
        kachakacha::v2::fabrication::PercentForRadius(shownBend_, radius_->value());
    if (!percent.has_value()) {
        return;
    }
    loading_ = true;
    assembly_->setValue(*percent);
    loading_ = false;
}

void V2FabricationDock::Connect()
{
    QObject::connect(method_, &QComboBox::currentIndexChanged, this, [this] {
        RefreshMethodRows();
        Emit();
    });
    QObject::connect(splitAxis_, &QComboBox::currentIndexChanged, this, [this] { Emit(); });
    QObject::connect(automatic_, &QCheckBox::toggled, this, [this] { Emit(); });
    QObject::connect(splitSolidFaces_, &QCheckBox::toggled, this, [this] { Emit(); });
    QObject::connect(lockRadius_, &QPushButton::clicked, this,
        [this] { PressLockRadius(); });
    QObject::connect(assembly_, &QDoubleSpinBox::valueChanged, this,
        [this] { SyncRadiusFromPercent(); });
    QObject::connect(radius_, &QDoubleSpinBox::valueChanged, this,
        [this] { SyncPercentFromRadius(); });
    QObject::connect(manual_, &QLineEdit::textChanged, this, [this] { Emit(); });
    QObject::connect(maxParts_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(minWidth_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(reliefDepth_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(reliefLigament_, &QDoubleSpinBox::valueChanged, this,
        [this] { Emit(); });
    QObject::connect(fidelity_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(thickness_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!loading_ && parameterHandler_) {
            parameterHandler_(ParameterId::ExtrudeDistance, value);
        }
    });
    QObject::connect(deviation_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!loading_ && parameterHandler_) {
            parameterHandler_(ParameterId::MaxDeviationMm, value);
        }
    });
    QObject::connect(rangeUMin_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(rangeUMax_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(rangeVMin_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(rangeVMax_, &QDoubleSpinBox::valueChanged, this, [this] { Emit(); });
    QObject::connect(applyMaterial_, &QPushButton::clicked, this, [this] { PressApplyMaterial(); });
    QObject::connect(applyAssembly_, &QPushButton::clicked, this, [this] { PressApplyAssembly(); });
    QObject::connect(freeze_, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading_ && freezeHandler_) {
            freezeHandler_(FreezeOutputChoice());
        }
    });
}

void V2FabricationDock::RefreshMethodRows()
{
    // 分割軸・境界・上限・最小幅は V1 方式(帯)だけが使う。V2 方式では隠す。
    const bool band = method_->currentIndex() == 0;
    form_->setRowVisible(splitAxis_, band);
    form_->setRowVisible(automatic_, band);
    form_->setRowVisible(manual_, band);
    form_->setRowVisible(maxParts_, band);
    form_->setRowVisible(minWidth_, band);
    // 面ごとに分けるのは V2 方式(面を分類して展開)の話である。
    // 帯へ近似し直す V1 方式では、そもそも面の分類をしない。
    form_->setRowVisible(splitSolidFaces_, !band);
}

void V2FabricationDock::Emit()
{
    if (!loading_ && choiceChanged_) {
        choiceChanged_();
    }
}

FabricationChoice V2FabricationDock::Choice() const
{
    FabricationChoice choice;
    choice.method = method_->currentIndex() == 0 ? FabricationMethod::BandApproximation
                                                 : FabricationMethod::ClassifyFaces;
    const int axisIndex = splitAxis_->currentIndex();
    choice.splitAxis = axisIndex >= 0 && axisIndex < 3 ? kSplitAxisValues[axisIndex] : 2;
    choice.automaticBoundaries = automatic_->isChecked();
    choice.splitSolidFaces = splitSolidFaces_->isChecked();
    choice.maximumPartCount = static_cast<int>(maxParts_->value());
    choice.minimumPartWidthMm = minWidth_->value();
    // 欄は % で見せて、中では比で持つ。% のまま比較すると桁を間違える。
    choice.maximumReliefDepthRatio = reliefDepth_->value() / 100.0;
    choice.minimumReliefLigamentMm = reliefLigament_->value();
    choice.fidelity = static_cast<int>(fidelity_->value());
    choice.rangeUMin = rangeUMin_->value();
    choice.rangeUMax = rangeUMax_->value();
    choice.rangeVMin = rangeVMin_->value();
    choice.rangeVMax = rangeVMax_->value();
    const auto parsed = kachakacha::v2::app::ParseBoundaryList(manual_->text().toStdString());
    if (parsed.HasValue()) {
        choice.manualBoundaries = parsed.Value();
    }
    return choice;
}

bool V2FabricationDock::ManualBoundariesReadable(QString* error) const
{
    const auto parsed = kachakacha::v2::app::ParseBoundaryList(manual_->text().toStdString());
    if (parsed.HasValue()) {
        return true;
    }
    if (error != nullptr) {
        *error = QString::fromStdString(parsed.FirstCode() + " "
            + parsed.FirstSummaryJa() + " " + parsed.FirstDetailsJa());
    }
    return false;
}

void V2FabricationDock::SetChoice(const FabricationChoice& choice)
{
    loading_ = true;
    method_->setCurrentIndex(choice.method == FabricationMethod::BandApproximation ? 0 : 1);
    for (int index = 0; index < 3; ++index) {
        if (kSplitAxisValues[index] == choice.splitAxis) {
            splitAxis_->setCurrentIndex(index);
        }
    }
    automatic_->setChecked(choice.automaticBoundaries);
    splitSolidFaces_->setChecked(choice.splitSolidFaces);
    manual_->setText(QString::fromStdString(
        kachakacha::v2::app::FormatBoundaryList(choice.manualBoundaries)));
    maxParts_->setValue(static_cast<double>(choice.maximumPartCount));
    minWidth_->setValue(choice.minimumPartWidthMm);
    reliefDepth_->setValue(choice.maximumReliefDepthRatio * 100.0);
    reliefLigament_->setValue(choice.minimumReliefLigamentMm);
    fidelity_->setValue(static_cast<double>(choice.fidelity));
    rangeUMin_->setValue(choice.rangeUMin);
    rangeUMax_->setValue(choice.rangeUMax);
    rangeVMin_->setValue(choice.rangeVMin);
    rangeVMax_->setValue(choice.rangeVMax);
    loading_ = false;
    RefreshMethodRows();
}

void V2FabricationDock::SetMaterialHandler(
    std::function<void(const QString& material, int layers)> handler)
{
    materialHandler_ = std::move(handler);
}

void V2FabricationDock::SetMaterial(const QString& material, int layers)
{
    material_->setText(material);
    layers_->setValue(static_cast<double>(layers));
}

QString V2FabricationDock::MaterialName() const
{
    return material_->text();
}

int V2FabricationDock::LayerCount() const
{
    return static_cast<int>(layers_->value());
}

void V2FabricationDock::PressApplyMaterial()
{
    if (materialHandler_) {
        materialHandler_(material_->text(), LayerCount());
    }
}

void V2FabricationDock::SetRange(double uMin, double uMax, double vMin, double vMax)
{
    rangeUMin_->setValue(uMin);
    rangeUMax_->setValue(uMax);
    rangeVMin_->setValue(vMin);
    rangeVMax_->setValue(vMax);
}

void V2FabricationDock::SetChoiceChangedHandler(std::function<void()> handler)
{
    choiceChanged_ = std::move(handler);
}

void V2FabricationDock::SetParameterMm(ParameterId id, double value)
{
    loading_ = true;
    if (id == ParameterId::ExtrudeDistance) {
        thickness_->setValue(value);
    } else if (id == ParameterId::MaxDeviationMm) {
        deviation_->setValue(value);
    }
    loading_ = false;
}

void V2FabricationDock::SetParameterHandler(std::function<void(ParameterId, double)> handler)
{
    parameterHandler_ = std::move(handler);
}

void V2FabricationDock::SetAssemblyPercent(double percent)
{
    loading_ = true;
    assembly_->setValue(percent);
    if (bendSlider_ != nullptr) {
        bendSlider_->setValue(static_cast<int>(percent + 0.5));
    }
    loading_ = false;
}

double V2FabricationDock::AssemblyPercent() const
{
    return assembly_->value();
}

void V2FabricationDock::TypeAssemblyPercent(double percent)
{
    assembly_->setValue(percent);   // valueChanged → SyncRadiusFromPercent
    if (bendSlider_ != nullptr) {
        bendSlider_->setValue(static_cast<int>(percent + 0.5));   // スライダも同じ値を指す
    }
}

void V2FabricationDock::TypeRadiusMm(double radiusMm)
{
    if (radius_ != nullptr) {
        radius_->setValue(radiusMm);   // valueChanged → SyncPercentFromRadius
    }
}

QString V2FabricationDock::PartNumbersText() const
{
    return parts_->text();
}

void V2FabricationDock::SetPartNumbersText(const QString& text)
{
    parts_->setText(text);
}

void V2FabricationDock::SetAssemblyHandler(
    std::function<void(double percent, const QString& parts)> handler)
{
    assemblyHandler_ = std::move(handler);
}

void V2FabricationDock::PressApplyAssembly()
{
    if (assemblyHandler_) {
        assemblyHandler_(assembly_->value(), parts_->text());
    }
}

FreezeOutput V2FabricationDock::FreezeOutputChoice() const
{
    switch (freeze_->currentIndex()) {
    case 1:  return FreezeOutput::PartsOnly;
    case 2:  return FreezeOutput::Both;
    default: return FreezeOutput::WiresOnly;
    }
}

void V2FabricationDock::SetFreezeOutput(kachakacha::v2::fabrication::FreezeOutput value)
{
    loading_ = true;
    freeze_->setCurrentIndex(value == FreezeOutput::PartsOnly ? 1
            : value == FreezeOutput::Both                     ? 2
                                                              : 0);
    loading_ = false;
}

void V2FabricationDock::SetFreezeOutputHandler(
    std::function<void(kachakacha::v2::fabrication::FreezeOutput)> handler)
{
    freezeHandler_ = std::move(handler);
}

void V2FabricationDock::SetRunHandler(std::function<void(const char*)> handler)
{
    runHandler_ = std::move(handler);
}

void V2FabricationDock::PressRun(const char* command)
{
    if (runHandler_) {
        runHandler_(command);
    }
}

void V2FabricationDock::SetMessage(const QString& text)
{
    message_->setText(text);
}

QString V2FabricationDock::MessageText() const
{
    return message_->text();
}

int V2FabricationDock::StageIndex() const
{
    return stages_ == nullptr ? -1 : stages_->currentIndex();
}

void V2FabricationDock::SetStageIndex(int index)
{
    if (stages_ != nullptr && index >= 0 && index < stages_->count()) {
        stages_->setCurrentIndex(index);
    }
}

void V2FabricationDock::SetModelText(const QString& text)
{
    model_->setText(text);
}

void V2FabricationDock::SetManualBoundariesText(const QString& text)
{
    manual_->setText(text);
}

void V2FabricationDock::SetAutomaticBoundaries(bool automatic)
{
    automatic_->setChecked(automatic);
}

void V2FabricationDock::SetMaximumPartCount(int count)
{
    maxParts_->setValue(static_cast<double>(count));
}

void V2FabricationDock::SetSplitAxisIndex(int index)
{
    splitAxis_->setCurrentIndex(index);
}

double V2FabricationDock::RadiusMm() const
{
    return radius_ == nullptr ? 0.0 : radius_->value();
}

bool V2FabricationDock::RadiusLocked() const
{
    return radiusLocked_;
}

void V2FabricationDock::ShowRadius(const kachakacha::v2::fabrication::BendRadius& bend,
    double percent)
{
    if (radius_ == nullptr) {
        return;
    }
    loading_ = true;
    shownBend_ = bend;
    bendShown_ = bend.flatLengthMm > 0.0 && bend.radiusMm > 0.0;
    const auto shown = kachakacha::v2::fabrication::RadiusAtPercent(bend, percent);
    radius_->setValue(shown.value_or(0.0));
    radius_->setEnabled(shown.has_value());
    radiusLocked_ = bend.lock == kachakacha::v2::fabrication::ValueLock::Locked;
    if (radiusState_ != nullptr) {
        radiusState_->setText(QString::fromUtf8(
            std::string(kachakacha::v2::fabrication::ValueLockNameJa(bend.lock)).c_str()));
    }
    if (lockRadius_ != nullptr) {
        lockRadius_->setText(radiusLocked_ ? QStringLiteral("固定を外す")
                                           : QStringLiteral("固定"));
    }
    loading_ = false;
}

bool V2FabricationDock::RadiusUsable() const
{
    return radius_ != nullptr && radius_->isEnabled();
}

QString V2FabricationDock::RadiusStateTextJa() const
{
    return radiusState_ == nullptr ? QString() : radiusState_->text();
}

void V2FabricationDock::ShowRadiusUnavailable(const QString& whyJa)
{
    if (radius_ == nullptr) {
        return;
    }
    loading_ = true;
    bendShown_ = false;
    radius_->setValue(0.0);
    radius_->setEnabled(false);
    radiusLocked_ = false;
    if (radiusState_ != nullptr) {
        radiusState_->setText(whyJa);
    }
    if (lockRadius_ != nullptr) {
        lockRadius_->setText(QStringLiteral("固定"));
    }
    loading_ = false;
}

void V2FabricationDock::SetRadiusHandler(std::function<void(double, bool)> handler)
{
    radiusHandler_ = std::move(handler);
}

void V2FabricationDock::PressLockRadius()
{
    if (radiusHandler_) {
        // 押すと自動と固定が入れ替わる。いま欄に出ている半径をそのまま渡す。
        radiusHandler_(RadiusMm(), !radiusLocked_);
    }
}

void V2FabricationDock::SetPolicyHandler(std::function<void(int)> handler)
{
    policyHandler_ = std::move(handler);
}

void V2FabricationDock::ShowPolicy(int policy)
{
    for (std::size_t index = 0; index < policies_.size(); ++index) {
        policies_[index]->setChecked(static_cast<int>(index) == policy);
    }
}

bool V2FabricationDock::ClickPolicy(int policy)
{
    if (policy < 0 || policy >= static_cast<int>(policies_.size())
        || !policies_[static_cast<std::size_t>(policy)]->isVisible()) {
        return false;
    }
    policies_[static_cast<std::size_t>(policy)]->click();
    return true;
}

int V2FabricationDock::SelectedPolicyShown() const
{
    for (std::size_t index = 0; index < policies_.size(); ++index) {
        if (policies_[index]->isChecked()) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

std::vector<QString> V2FabricationDock::PolicyLabels() const
{
    std::vector<QString> labels;
    for (QPushButton* button : policies_) {
        labels.push_back(button->text());
    }
    return labels;
}

bool V2FabricationDock::ClickBendPreset(int percent)
{
    for (QPushButton* button : bendPresets_) {
        if (button->text() == QStringLiteral("%1").arg(percent) && button->isVisible()) {
            button->click();
            return true;
        }
    }
    return false;
}

std::vector<int> V2FabricationDock::BendPresets() const
{
    std::vector<int> values;
    for (QPushButton* button : bendPresets_) {
        values.push_back(button->text().toInt());
    }
    return values;
}
