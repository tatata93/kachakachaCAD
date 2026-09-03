#include "PartModelPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kModelNameRole = Qt::UserRole + 1;
constexpr int kPartNumberRole = Qt::UserRole + 2;
constexpr int kSetNameRole = Qt::UserRole + 3;

[[nodiscard]] QString ToQString(const std::string& value)
{
    return QString::fromStdString(value);
}

[[nodiscard]] QString StateText(kachakacha::model::ObjectSetState state)
{
    switch (state) {
    case kachakacha::model::ObjectSetState::ReferenceOnly:
        return QStringLiteral("参照のみ");
    case kachakacha::model::ObjectSetState::Hidden:
        return QStringLiteral("非表示");
    case kachakacha::model::ObjectSetState::Visible:
    default:
        return QStringLiteral("表示");
    }
}

} // namespace

PartModelPanel::PartModelPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // --- 表示モード ---
    auto* modeRow = new QHBoxLayout;
    auto* finishedButton = new QPushButton(QStringLiteral("完成品のみ"));
    finishedButton->setToolTip(QStringLiteral("部材近似(近似:～のセット)を隠します"));
    auto* overlayButton = new QPushButton(QStringLiteral("重ね表示"));
    overlayButton->setToolTip(QStringLiteral("完成品の上に部材境界(赤)を重ねて表示します"));
    connect(finishedButton, &QPushButton::clicked, this, [this] {
        if (onOverlayVisibility) onOverlayVisibility(false);
    });
    connect(overlayButton, &QPushButton::clicked, this, [this] {
        if (onOverlayVisibility) onOverlayVisibility(true);
    });
    modeRow->addWidget(finishedButton, 1);
    modeRow->addWidget(overlayButton, 1);
    layout->addLayout(modeRow);

    // --- 作成 ---
    auto* createGroup = new QGroupBox(QStringLiteral("部材近似モデルを作成"));
    auto* createForm = new QFormLayout(createGroup);
    createForm->setContentsMargins(8, 4, 8, 8);
    plateCombo_ = new QComboBox;
    plateCombo_->setToolTip(QStringLiteral("近似する板材を選びます"));
    createForm->addRow(QStringLiteral("板材"), plateCombo_);
    axisCombo_ = new QComboBox;
    axisCombo_->addItem(QStringLiteral("V方向で区切る（標準）"));
    axisCombo_->addItem(QStringLiteral("U方向で区切る"));
    createForm->addRow(QStringLiteral("分割方向"), axisCombo_);
    automaticCheck_ = new QCheckBox(QStringLiteral("許容偏差から自動で区切る"));
    automaticCheck_->setChecked(true);
    createForm->addRow(automaticCheck_);
    deviationSpin_ = new QDoubleSpinBox;
    deviationSpin_->setRange(0.01, 10.0);
    deviationSpin_->setDecimals(2);
    deviationSpin_->setSingleStep(0.05);
    deviationSpin_->setValue(0.25);
    deviationSpin_->setSuffix(QStringLiteral(" mm"));
    createForm->addRow(QStringLiteral("許容偏差"), deviationSpin_);
    maxCountSpin_ = new QSpinBox;
    maxCountSpin_->setRange(1, 64);
    maxCountSpin_->setValue(12);
    createForm->addRow(QStringLiteral("部材数の上限"), maxCountSpin_);
    minWidthSpin_ = new QDoubleSpinBox;
    minWidthSpin_->setRange(0.0, 500.0);
    minWidthSpin_->setDecimals(1);
    minWidthSpin_->setValue(4.0);
    minWidthSpin_->setSuffix(QStringLiteral(" mm"));
    createForm->addRow(QStringLiteral("最小部材幅"), minWidthSpin_);
    manualBoundariesEdit_ = new QLineEdit;
    manualBoundariesEdit_->setPlaceholderText(QStringLiteral("例: 0.25, 0.5, 0.75"));
    manualBoundariesEdit_->setToolTip(
        QStringLiteral("手動境界の位置(0～1)をカンマ区切りで指定します。自動のチェックを外すと使われます"));
    manualBoundariesEdit_->setEnabled(false);
    createForm->addRow(QStringLiteral("手動境界"), manualBoundariesEdit_);
    connect(automaticCheck_, &QCheckBox::toggled, this, [this](bool automatic) {
        manualBoundariesEdit_->setEnabled(!automatic);
    });
    auto* createButton = new QPushButton(QStringLiteral("部材近似モデルを作成"));
    createButton->setObjectName("primaryButton");
    connect(createButton, &QPushButton::clicked, this, [this] {
        if (onCreate) onCreate();
    });
    createForm->addRow(createButton);
    layout->addWidget(createGroup);

    // --- 一覧 ---
    modelTree_ = new QTreeWidget;
    modelTree_->setHeaderHidden(true);
    modelTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    modelTree_->setToolTip(
        QStringLiteral("部材を複数選ぶと「型紙を表示」で隣接部材を1枚に結合した型紙を出せます"));
    layout->addWidget(modelTree_, 2);

    auto* actionRow = new QHBoxLayout;
    auto* recalcButton = new QPushButton(QStringLiteral("再計算"));
    recalcButton->setToolTip(QStringLiteral("上の設定を選択中のモデルへ適用し直します"));
    auto* removeButton = new QPushButton(QStringLiteral("削除"));
    auto* extractButton = new QPushButton(QStringLiteral("境界を抽出"));
    extractButton->setToolTip(
        QStringLiteral("部材境界を独立した通常ワイヤとして複製します（治具設計の基準線用）"));
    connect(recalcButton, &QPushButton::clicked, this, [this] {
        if (onRecalculate) onRecalculate();
    });
    connect(removeButton, &QPushButton::clicked, this, [this] {
        if (onRemove) onRemove();
    });
    connect(extractButton, &QPushButton::clicked, this, [this] {
        if (onExtract) onExtract();
    });
    actionRow->addWidget(recalcButton, 1);
    actionRow->addWidget(removeButton, 1);
    actionRow->addWidget(extractButton, 1);
    layout->addLayout(actionRow);

    auto* makePlateButton = new QPushButton(QStringLiteral("選択部材を板材化"));
    makePlateButton->setToolTip(QStringLiteral(
        "選択した部材の面から板材を作ります（板厚・材質は元の板材と同じ）。\n"
        "板材にすると開口の追加や1:1出力など既存の板材機能が使えます"));
    connect(makePlateButton, &QPushButton::clicked, this, [this] {
        if (onMakePlate) onMakePlate();
    });
    layout->addWidget(makePlateButton);

    auto* patternButton = new QPushButton(QStringLiteral("型紙を表示"));
    patternButton->setObjectName("primaryButton");
    patternButton->setToolTip(
        QStringLiteral("選択中のモデル（部材を選んでいればその部材だけ）の展開図を表示します"));
    connect(patternButton, &QPushButton::clicked, this, [this] {
        if (onShowPatterns) onShowPatterns();
    });
    layout->addWidget(patternButton);

    // --- 曲げ確認と出力 ---
    auto* foldGroup = new QGroupBox(QStringLiteral("曲げ確認と出力"));
    auto* foldLayout = new QVBoxLayout(foldGroup);
    foldLayout->setContentsMargins(8, 4, 8, 8);
    foldPreviewCheck_ = new QCheckBox(QStringLiteral("3Dビューで曲げ状態を表示"));
    foldPreviewCheck_->setToolTip(QStringLiteral(
        "選択中のモデルを、下のスライダーの曲げ具合でメイン3Dビューへ重ねて表示します"));
    foldLayout->addWidget(foldPreviewCheck_);
    auto* sliderRow = new QHBoxLayout;
    foldSlider_ = new QSlider(Qt::Horizontal);
    foldSlider_->setRange(0, 100);
    foldSlider_->setValue(100);
    foldSlider_->setToolTip(QStringLiteral("0%=平面（型紙の状態） / 100%=折り曲げた近似完成形"));
    foldLabel_ = new QLabel(QStringLiteral("100%（近似完成形）"));
    sliderRow->addWidget(foldSlider_, 1);
    sliderRow->addWidget(foldLabel_);
    foldLayout->addLayout(sliderRow);
    const auto foldChanged = [this] {
        const int value = foldSlider_->value();
        foldLabel_->setText(value == 0
            ? QStringLiteral("0%（平面）")
            : value == 100 ? QStringLiteral("100%（近似完成形）")
                           : QStringLiteral("%1%（曲げ途中）").arg(value));
        if (onFoldStateChanged) onFoldStateChanged();
    };
    connect(foldSlider_, &QSlider::valueChanged, this, [foldChanged](int) { foldChanged(); });
    connect(foldPreviewCheck_, &QCheckBox::toggled, this, [foldChanged](bool) { foldChanged(); });
    connect(modelTree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (onFoldStateChanged) onFoldStateChanged();
    });
    auto* realizeButton = new QPushButton(QStringLiteral("この曲げ状態を板材化"));
    realizeButton->setToolTip(QStringLiteral(
        "スライダーの曲げ具合の形状を、このプロジェクトへ通常の板材として追加します。\n"
        "0%なら平面に置いた展開状態（窓などの開口も実際の穴になります）"));
    connect(realizeButton, &QPushButton::clicked, this, [this] {
        if (onRealizeFoldState) onRealizeFoldState();
    });
    foldLayout->addWidget(realizeButton);
    auto* exportRow = new QHBoxLayout;
    auto* stlButton = new QPushButton(QStringLiteral("STL保存…"));
    stlButton->setToolTip(QStringLiteral("この曲げ状態を3Dプリント用STLへ保存します"));
    auto* stepButton = new QPushButton(QStringLiteral("STEP保存…"));
    stepButton->setToolTip(QStringLiteral("この曲げ状態をCAD交換用STEPへ保存します"));
    auto* kcdButton = new QPushButton(QStringLiteral("別の.kcdへ保存…"));
    kcdButton->setToolTip(QStringLiteral(
        "この曲げ状態だけを含む新しいプロジェクト(.kcd)を書き出します"));
    connect(stlButton, &QPushButton::clicked, this, [this] {
        if (onExportFoldMesh) onExportFoldMesh(false);
    });
    connect(stepButton, &QPushButton::clicked, this, [this] {
        if (onExportFoldMesh) onExportFoldMesh(true);
    });
    connect(kcdButton, &QPushButton::clicked, this, [this] {
        if (onExportFoldKcd) onExportFoldKcd();
    });
    exportRow->addWidget(stlButton, 1);
    exportRow->addWidget(stepButton, 1);
    exportRow->addWidget(kcdButton, 1);
    foldLayout->addLayout(exportRow);
    layout->addWidget(foldGroup);

    // --- セット ---
    auto* setGroup = new QGroupBox(QStringLiteral("セット"));
    auto* setLayout = new QVBoxLayout(setGroup);
    setLayout->setContentsMargins(8, 4, 8, 8);
    setList_ = new QListWidget;
    setList_->setToolTip(QStringLiteral(
        "派生物のまとまり。状態は 表示 / 参照のみ / 非表示。\n参照のみは薄く表示され、選択・編集の対象になりません（v1では表示のみ反映）"));
    setLayout->addWidget(setList_, 1);
    auto* stateRow = new QHBoxLayout;
    auto* visibleButton = new QPushButton(QStringLiteral("表示"));
    auto* referenceButton = new QPushButton(QStringLiteral("参照のみ"));
    auto* hiddenButton = new QPushButton(QStringLiteral("非表示"));
    connect(visibleButton, &QPushButton::clicked, this, [this] {
        if (onSetStateChange) onSetStateChange(static_cast<int>(kachakacha::model::ObjectSetState::Visible));
    });
    connect(referenceButton, &QPushButton::clicked, this, [this] {
        if (onSetStateChange) onSetStateChange(static_cast<int>(kachakacha::model::ObjectSetState::ReferenceOnly));
    });
    connect(hiddenButton, &QPushButton::clicked, this, [this] {
        if (onSetStateChange) onSetStateChange(static_cast<int>(kachakacha::model::ObjectSetState::Hidden));
    });
    stateRow->addWidget(visibleButton, 1);
    stateRow->addWidget(referenceButton, 1);
    stateRow->addWidget(hiddenButton, 1);
    setLayout->addLayout(stateRow);
    layout->addWidget(setGroup, 1);
}

void PartModelPanel::RefreshFromProject(const kachakacha::model::Project& project)
{
    const QString previousPlate = plateCombo_->currentText();
    plateCombo_->clear();
    for (const auto& plate : project.Plates()) {
        plateCombo_->addItem(ToQString(plate.name));
    }
    const int plateIndex = plateCombo_->findText(previousPlate);
    if (plateIndex >= 0) {
        plateCombo_->setCurrentIndex(plateIndex);
    }

    const QString previousModel = SelectedModelName();
    modelTree_->blockSignals(true);
    modelTree_->clear();
    for (const auto& model : project.PartModels()) {
        auto* modelItem = new QTreeWidgetItem(modelTree_, {
            QStringLiteral("%1 ← %2  （部材 %3、最大偏差 %4 mm%5）")
                .arg(ToQString(model.name), ToQString(model.sourcePlateName))
                .arg(model.result.parts.size())
                .arg(model.result.maximumDeviationMillimeters, 0, 'f', 3)
                .arg(model.result.reachedRequestedTolerance
                        ? QString()
                        : QStringLiteral("・許容超過")),
        });
        modelItem->setData(0, kModelNameRole, ToQString(model.name));
        for (const auto& part : model.result.parts) {
            auto* partItem = new QTreeWidgetItem(modelItem, {
                QStringLiteral("部材%1  幅 %2 mm  偏差 %3 mm")
                    .arg(part.number)
                    .arg(part.widthMillimeters, 0, 'f', 1)
                    .arg(part.estimatedDeviationMillimeters, 0, 'f', 3),
            });
            partItem->setData(0, kModelNameRole, ToQString(model.name));
            partItem->setData(0, kPartNumberRole, part.number);
        }
        modelItem->setExpanded(true);
        if (ToQString(model.name) == previousModel) {
            modelItem->setSelected(true);
        }
    }
    modelTree_->blockSignals(false);

    setList_->clear();
    for (const auto& set : project.ObjectSets()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  [%2]%3")
                .arg(ToQString(set.name), StateText(set.state),
                    set.automatic ? QStringLiteral("（自動）") : QString()));
        item->setData(kSetNameRole, ToQString(set.name));
        setList_->addItem(item);
    }
}

QString PartModelPanel::SelectedPlateName() const
{
    return plateCombo_->currentText();
}

void PartModelPanel::SelectPlate(const QString& plateName)
{
    const int index = plateCombo_->findText(plateName);
    if (index >= 0) {
        plateCombo_->setCurrentIndex(index);
    }
}

kachakacha::model::PartApproximationOptions PartModelPanel::CurrentOptions() const
{
    kachakacha::model::PartApproximationOptions options;
    options.splitAxis = axisCombo_->currentIndex() == 1
        ? kachakacha::model::PartSplitAxis::U
        : kachakacha::model::PartSplitAxis::V;
    options.automaticBoundaries = automaticCheck_->isChecked();
    options.maximumDeviationMillimeters = deviationSpin_->value();
    options.maximumPartCount = maxCountSpin_->value();
    options.minimumPartWidthMillimeters = minWidthSpin_->value();
    if (!options.automaticBoundaries) {
        const QStringList tokens = manualBoundariesEdit_->text().split(
            QRegularExpression(QStringLiteral("[,、\\s]+")), Qt::SkipEmptyParts);
        for (const QString& token : tokens) {
            bool ok = false;
            const double value = token.toDouble(&ok);
            if (ok) {
                options.manualBoundaryParameters.push_back(value);
            }
        }
    }
    return options;
}

QString PartModelPanel::SelectedModelName() const
{
    const QList<QTreeWidgetItem*> items = modelTree_->selectedItems();
    for (QTreeWidgetItem* item : items) {
        const QString name = item->data(0, kModelNameRole).toString();
        if (!name.isEmpty()) {
            return name;
        }
    }
    if (modelTree_->topLevelItemCount() == 1) {
        return modelTree_->topLevelItem(0)->data(0, kModelNameRole).toString();
    }
    return {};
}

std::vector<int> PartModelPanel::SelectedPartNumbers() const
{
    std::vector<int> numbers;
    const QString modelName = SelectedModelName();
    for (QTreeWidgetItem* item : modelTree_->selectedItems()) {
        if (item->data(0, kModelNameRole).toString() != modelName) {
            continue;
        }
        const QVariant number = item->data(0, kPartNumberRole);
        if (number.isValid()) {
            numbers.push_back(number.toInt());
        }
    }
    std::sort(numbers.begin(), numbers.end());
    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
    return numbers;
}

QString PartModelPanel::SelectedSetName() const
{
    QListWidgetItem* item = setList_->currentItem();
    if (item == nullptr) {
        return {};
    }
    return item->data(kSetNameRole).toString();
}

double PartModelPanel::FoldProgress() const
{
    return static_cast<double>(foldSlider_->value()) / 100.0;
}

bool PartModelPanel::FoldPreviewEnabled() const
{
    return foldPreviewCheck_->isChecked();
}
