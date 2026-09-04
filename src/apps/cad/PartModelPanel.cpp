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
#include <QHeaderView>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kModelNameRole = Qt::UserRole + 1;
constexpr int kPartNumberRole = Qt::UserRole + 2;
constexpr int kSetNameRole = Qt::UserRole + 3;
constexpr int kSourceNameRole = Qt::UserRole + 4;   // 近似元コンボ: 実際の名前
constexpr int kSourceIsSurfaceRole = Qt::UserRole + 5; // 近似元コンボ: 面なら true

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
    layout->setSpacing(5);

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

    // --- 作成(近似ユニット #15 が主、従来の1面近似は下に併設) ---
    auto* createGroup = new QGroupBox(QStringLiteral("部材近似モデルを作成"));
    auto* createOuter = new QVBoxLayout(createGroup);
    createOuter->setContentsMargins(8, 4, 8, 8);
    createOuter->setSpacing(5);

    unitNameEdit_ = new QLineEdit(QStringLiteral("近似ユニット1"));
    unitNameEdit_->setToolTip(QStringLiteral(
        "ユニット名。作られる近似モデルと部材グループの名前に使われます"));
    auto* unitNameRow = new QHBoxLayout;
    unitNameRow->addWidget(new QLabel(QStringLiteral("ユニット名")));
    unitNameRow->addWidget(unitNameEdit_, 1);
    createOuter->addLayout(unitNameRow);

    auto* collectButton = new QPushButton(QStringLiteral("選択をユニットの表へ取り込む"));
    collectButton->setToolTip(QStringLiteral(
        "近似したい部品の周辺の面・板材・ワイヤーを3D画面でまとめて選んで押します。\n"
        "部材グループごとの選択でも構いません。押すたびに表へ追記されます"));
    connect(collectButton, &QPushButton::clicked, this, [this] {
        if (onCollectUnitMembers) onCollectUnitMembers();
    });
    createOuter->addWidget(collectButton);

    unitTable_ = new QTableWidget(0, 3);
    unitTable_->setHorizontalHeaderLabels({
        QStringLiteral("対象"), QStringLiteral("種類"), QStringLiteral("役割")});
    unitTable_->verticalHeader()->setVisible(false);
    unitTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    unitTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    unitTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    unitTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    unitTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    unitTable_->setMinimumHeight(96);
    unitTable_->setMaximumHeight(150);
    unitTable_->setToolTip(QStringLiteral(
        "各行の役割: 近似する=部材近似モデルになる / 形状維持(接続)=形は保ったまま\n"
        "近似の実形状へ接続できるよう「〜_接続」を自動生成 / 対象外=何もしない"));
    createOuter->addWidget(unitTable_);

    auto* unitRowButtons = new QHBoxLayout;
    auto* removeUnitRowButton = new QPushButton(QStringLiteral("選択行を外す"));
    connect(removeUnitRowButton, &QPushButton::clicked, this, [this] {
        const int row = unitTable_->currentRow();
        if (row >= 0 && row < static_cast<int>(unitMembers_.size())) {
            unitMembers_.erase(unitMembers_.begin() + row);
            RefreshUnitTable();
        }
    });
    auto* clearUnitButton = new QPushButton(QStringLiteral("表をクリア"));
    connect(clearUnitButton, &QPushButton::clicked, this, [this] { ClearUnitMembers(); });
    unitRowButtons->addWidget(removeUnitRowButton, 1);
    unitRowButtons->addWidget(clearUnitButton, 1);
    createOuter->addLayout(unitRowButtons);

    unitOutputCombo_ = new QComboBox;
    unitOutputCombo_->addItem(QStringLiteral("このプロジェクト内の新グループへ"));
    unitOutputCombo_->addItem(QStringLiteral("新グループ+新しい.kcdにも保存"));
    auto* unitOutputRow = new QHBoxLayout;
    unitOutputRow->addWidget(new QLabel(QStringLiteral("出力先")));
    unitOutputRow->addWidget(unitOutputCombo_, 1);
    createOuter->addLayout(unitOutputRow);

    auto* createUnitButton = new QPushButton(QStringLiteral("ユニットを近似"));
    createUnitButton->setObjectName("primaryButton");
    createUnitButton->setToolTip(QStringLiteral(
        "「近似する」役割の面・板材ごとに部材近似モデルを作り、\n"
        "「形状維持(接続)」役割は最寄りの近似へ接続する「〜_接続」を自動生成します。\n"
        "近似元の面にある閉じた投影輪郭は開口として自動で写します"));
    connect(createUnitButton, &QPushButton::clicked, this, [this] {
        if (onCreateUnit) onCreateUnit();
    });
    createOuter->addWidget(createUnitButton);

    auto* singleLabel = new QLabel(QStringLiteral("―― 1面だけ手早く近似（従来方式） ――"));
    singleLabel->setStyleSheet("color: #6a7781;");
    singleLabel->setAlignment(Qt::AlignCenter);
    createOuter->addWidget(singleLabel);

    auto* createForm = new QFormLayout;
    createForm->setContentsMargins(0, 0, 0, 0);
    createOuter->addLayout(createForm);
    plateCombo_ = new QComboBox;
    plateCombo_->setToolTip(QStringLiteral(
        "近似する板材または面を選びます。\n"
        "面は厚み未定のまま近似し、板材化の時点で板厚を指定します"));
    createForm->addRow(QStringLiteral("近似元"), plateCombo_);
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
    auto* pickBoundariesButton = new QPushButton(QStringLiteral("選択ワイヤーを境界に使う"));
    pickBoundariesButton->setToolTip(QStringLiteral(
        "3D画面で境界(折り線)にしたいワイヤーを選んでから押します。\n"
        "近似元の上での位置を測って手動境界欄へ入れます(折り線を任意の線で指定できます)"));
    connect(pickBoundariesButton, &QPushButton::clicked, this, [this] {
        if (onPickBoundariesFromWires) onPickBoundariesFromWires();
    });
    createForm->addRow(pickBoundariesButton);
    auto* scopeHint = new QLabel(QStringLiteral(
        "近似元と一緒に周辺のワイヤー・面(部材グループごと選択でも可)を選んでおくと、\n"
        "近似の実形状へ接続できるよう自動変形した「〜_接続」を作り、再計算にも追従します"));
    scopeHint->setWordWrap(true);
    scopeHint->setStyleSheet("color: #5b6a74;");
    createForm->addRow(scopeHint);
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
    sections_[0] = createGroup;

    // --- 一覧 ---
    auto* listSection = new QWidget;
    auto* listSectionLayout = new QVBoxLayout(listSection);
    listSectionLayout->setContentsMargins(0, 0, 0, 0);
    listSectionLayout->setSpacing(layout->spacing());
    modelTree_ = new QTreeWidget;
    modelTree_->setHeaderHidden(true);
    modelTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    modelTree_->setToolTip(
        QStringLiteral("部材を複数選ぶと「型紙を表示」で隣接部材を1枚に結合した型紙を出せます"));
    listSectionLayout->addWidget(modelTree_, 2);

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
    listSectionLayout->addLayout(actionRow);

    // #18: 板材化の追従/固定と出力の種類。
    auto* partOutputRow = new QHBoxLayout;
    partPlateFollowCombo_ = new QComboBox;
    partPlateFollowCombo_->addItem(QStringLiteral("近似に追従（派生）"));
    partPlateFollowCombo_->addItem(QStringLiteral("現在の形で固定（独立）"));
    partPlateFollowCombo_->setToolTip(QStringLiteral(
        "追従: 部材面(派生)から作り、近似の再計算で作り直されます。\n"
        "固定: 現在の形を独立ワイヤ+ルールド面として複製してから作り、以後は変わりません"));
    partOutputRow->addWidget(partPlateFollowCombo_, 1);
    listSectionLayout->addLayout(partOutputRow);
    auto* partOutputChecks = new QHBoxLayout;
    partOutputPlateCheck_ = new QCheckBox(QStringLiteral("板材"));
    partOutputPlateCheck_->setChecked(true);
    partOutputSurfaceCheck_ = new QCheckBox(QStringLiteral("面"));
    partOutputWiresCheck_ = new QCheckBox(QStringLiteral("縁ワイヤ"));
    partOutputChecks->addWidget(new QLabel(QStringLiteral("出力:")));
    partOutputChecks->addWidget(partOutputPlateCheck_);
    partOutputChecks->addWidget(partOutputSurfaceCheck_);
    partOutputChecks->addWidget(partOutputWiresCheck_);
    partOutputChecks->addStretch(1);
    listSectionLayout->addLayout(partOutputChecks);

    auto* makePlateButton = new QPushButton(QStringLiteral("選択部材を板材化・出力"));
    makePlateButton->setToolTip(QStringLiteral(
        "選択した部材からチェックした出力を作ります（複数チェックで全部出力）。\n"
        "板材入力なら板厚・材質は元と同じ、面入力なら「面入力の板厚」の値を使います。\n"
        "板材にすると開口の追加や1:1出力など既存の板材機能が使えます"));
    connect(makePlateButton, &QPushButton::clicked, this, [this] {
        if (onMakePlate) onMakePlate();
    });
    listSectionLayout->addWidget(makePlateButton);

    auto* patternButton = new QPushButton(QStringLiteral("型紙を表示"));
    patternButton->setObjectName("primaryButton");
    patternButton->setToolTip(
        QStringLiteral("選択中のモデル（部材を選んでいればその部材だけ）の展開図を表示します"));
    connect(patternButton, &QPushButton::clicked, this, [this] {
        if (onShowPatterns) onShowPatterns();
    });
    listSectionLayout->addWidget(patternButton);
    layout->addWidget(listSection, 2);
    sections_[1] = listSection;

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
    foldSlider_->setToolTip(QStringLiteral(
        "組立アニメーションです。0%=各部材の展開形(型紙と同じ)を少し離れた位置に表示、\n"
        "100%=折り線ごとの角度どおりに折った状態(近似モデルの位置)。\n"
        "折り角そのものは下の折り線ごとの行で指定します"));
    foldLabel_ = new QLabel(QStringLiteral("100%（折り曲げ状態）"));
    sliderRow->addWidget(foldSlider_, 1);
    sliderRow->addWidget(foldLabel_);
    foldLayout->addLayout(sliderRow);
    const auto foldChanged = [this] {
        const int value = foldSlider_->value();
        foldLabel_->setText(value == 0
            ? QStringLiteral("0%（展開を並べた状態）")
            : value == 100 ? QStringLiteral("100%（折り曲げ状態）")
                           : QStringLiteral("%1%（組立途中）").arg(value));
        if (onFoldStateChanged) onFoldStateChanged();
    };
    connect(foldSlider_, &QSlider::valueChanged, this, [foldChanged](int) { foldChanged(); });
    connect(foldPreviewCheck_, &QCheckBox::toggled, this, [foldChanged](bool) { foldChanged(); });
    connect(modelTree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (onFoldStateChanged) onFoldStateChanged();
    });
    auto* thicknessRow = new QHBoxLayout;
    auto* thicknessLabel = new QLabel(QStringLiteral("面入力の板厚"));
    foldThicknessSpin_ = new QDoubleSpinBox;
    foldThicknessSpin_->setRange(0.05, 10.0);
    foldThicknessSpin_->setDecimals(2);
    foldThicknessSpin_->setSingleStep(0.05);
    foldThicknessSpin_->setValue(0.5);
    foldThicknessSpin_->setSuffix(QStringLiteral(" mm"));
    foldThicknessSpin_->setToolTip(QStringLiteral(
        "面から作った近似モデルを板材化・出力するときに使う板厚です。\n"
        "板材から作ったモデルでは元の板厚が使われます"));
    thicknessRow->addWidget(thicknessLabel);
    thicknessRow->addWidget(foldThicknessSpin_, 1);
    foldLayout->addLayout(thicknessRow);
    // 可動折り線(合意10): 折り線ごとの角度⇄%を相互編集する行。選択モデルに合わせて作り直す。
    foldLinesContainer_ = new QWidget;
    foldLinesLayout_ = new QVBoxLayout(foldLinesContainer_);
    foldLinesLayout_->setContentsMargins(0, 0, 0, 0);
    foldLinesLayout_->setSpacing(2);
    foldLinesContainer_->setToolTip(QStringLiteral(
        "各折り線の折り具合です。角度(°)と進行度(%)はどちらを変えてももう一方が追従します。\n"
        "100%が近似完成形の折り角、0%は平ら。上の全体スライダーは「型紙⇄この折り状態」の補間です"));
    foldLayout->addWidget(foldLinesContainer_);
    auto* realizeButton = new QPushButton(QStringLiteral("この曲げ状態を板材化"));
    realizeButton->setToolTip(QStringLiteral(
        "スライダー0%なら平面に置いた展開状態（窓などの開口も実際の穴になります）、\n"
        "それ以外なら折り線ごとの角度どおりの折り状態を、このプロジェクトへ板材として追加します"));
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
    sections_[2] = foldGroup;

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
    sections_[3] = setGroup;

    // 1区画だけ表示するとき項目が縦へ散らばらないよう、余白を下端へ寄せる。
    bottomSpacer_ = new QWidget;
    bottomSpacer_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    bottomSpacer_->setVisible(false);
    layout->addWidget(bottomSpacer_, 3);
}

void PartModelPanel::SetVisibleSection(int index)
{
    for (int section = 0; section < static_cast<int>(sections_.size()); ++section) {
        if (sections_[section] != nullptr) {
            sections_[section]->setVisible(index < 0 || section == index);
        }
    }
    if (bottomSpacer_ != nullptr) {
        // 一覧(伸びるツリー)以外を単独表示するときだけ下の余白を効かせる。
        bottomSpacer_->setVisible(index >= 0 && index != 1);
    }
}

void PartModelPanel::RefreshFromProject(const kachakacha::model::Project& project)
{
    const QString previousPlate = plateCombo_->currentText();
    plateCombo_->clear();
    for (const auto& plate : project.Plates()) {
        plateCombo_->addItem(ToQString(plate.name));
        const int index = plateCombo_->count() - 1;
        plateCombo_->setItemData(index, ToQString(plate.name), kSourceNameRole);
        plateCombo_->setItemData(index, false, kSourceIsSurfaceRole);
    }
    for (const auto& surface : project.Surfaces()) {
        if (surface.partModelSourceName.has_value()) {
            continue; // 近似モデルの派生面は再近似の対象にしない。
        }
        plateCombo_->addItem(QStringLiteral("面: %1").arg(ToQString(surface.name)));
        const int index = plateCombo_->count() - 1;
        plateCombo_->setItemData(index, ToQString(surface.name), kSourceNameRole);
        plateCombo_->setItemData(index, true, kSourceIsSurfaceRole);
    }
    const int plateIndex = plateCombo_->findText(previousPlate);
    if (plateIndex >= 0) {
        plateCombo_->setCurrentIndex(plateIndex);
    }

    const QString previousModel = SelectedModelName();
    modelTree_->blockSignals(true);
    modelTree_->clear();
    // #16: ユニット→近似面(モデル)→部材 の3階層。ユニット = 自動セット
    // 「近似:<モデル名>」の親グループ名(空なら最上位に置く)。
    const auto unitNameOf = [&project](const std::string& modelName) -> QString {
        const std::string setName = "近似:" + modelName;
        for (const auto& set : project.ObjectSets()) {
            if (set.name == setName) {
                return ToQString(set.parentName);
            }
        }
        return {};
    };
    std::vector<std::pair<QString, QTreeWidgetItem*>> unitItems;
    const auto unitItemFor = [&](const QString& unitName) -> QTreeWidgetItem* {
        if (unitName.isEmpty()) {
            return nullptr;
        }
        for (const auto& [name, item] : unitItems) {
            if (name == unitName) {
                return item;
            }
        }
        auto* item = new QTreeWidgetItem(modelTree_,
            {QStringLiteral("ユニット %1").arg(unitName)});
        item->setExpanded(true);
        unitItems.emplace_back(unitName, item);
        return item;
    };
    for (const auto& model : project.PartModels()) {
        QTreeWidgetItem* unitParent = unitItemFor(unitNameOf(model.name));
        const QString label = QStringLiteral("%1 ← %2  （部材 %3、最大偏差 %4 mm%5）")
            .arg(ToQString(model.name),
                model.sourceSurfaceName.empty()
                    ? ToQString(model.sourcePlateName)
                    : QStringLiteral("面: %1").arg(ToQString(model.sourceSurfaceName)))
            .arg(model.result.parts.size())
            .arg(model.result.maximumDeviationMillimeters, 0, 'f', 3)
            .arg(model.result.reachedRequestedTolerance
                    ? QString()
                    : QStringLiteral("・許容超過"));
        auto* modelItem = unitParent != nullptr
            ? new QTreeWidgetItem(unitParent, {label})
            : new QTreeWidgetItem(modelTree_, {label});
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

void PartModelPanel::SetManualBoundaryParameters(const std::vector<double>& parameters)
{
    QStringList tokens;
    for (const double value : parameters) {
        tokens << QString::number(value, 'f', 4);
    }
    manualBoundariesEdit_->setText(tokens.join(QStringLiteral(", ")));
    automaticCheck_->setChecked(false);
}

QString PartModelPanel::SelectedPlateName() const
{
    return plateCombo_->currentText();
}

QString PartModelPanel::SelectedSourceName() const
{
    const QVariant data = plateCombo_->currentData(kSourceNameRole);
    return data.isValid() ? data.toString() : plateCombo_->currentText();
}

bool PartModelPanel::SelectedSourceIsSurface() const
{
    return plateCombo_->currentData(kSourceIsSurfaceRole).toBool();
}

double PartModelPanel::FoldThicknessMillimeters() const
{
    return foldThicknessSpin_ != nullptr ? foldThicknessSpin_->value() : 0.5;
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

void PartModelPanel::SetFoldLines(
    const QString& modelName,
    const std::vector<double>& fullAngleDegrees,
    const std::vector<double>& progress)
{
    const std::size_t count = fullAngleDegrees.size();
    const bool rebuild = modelName != foldLinesModelName_
        || count != foldAngleSpins_.size();
    foldLinesModelName_ = modelName;
    foldFullAngleDegrees_ = fullAngleDegrees;
    if (rebuild) {
        // 既存の行を作り直す。
        while (QLayoutItem* item = foldLinesLayout_->takeAt(0)) {
            if (QWidget* widget = item->widget()) {
                widget->deleteLater();
            }
            delete item;
        }
        foldAngleSpins_.clear();
        foldPercentSpins_.clear();
        for (std::size_t index = 0; index < count; ++index) {
            auto* row = new QWidget;
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(4);
            rowLayout->addWidget(
                new QLabel(QStringLiteral("折り線%1").arg(index + 1)));
            auto* angleSpin = new QDoubleSpinBox;
            angleSpin->setRange(-360.0, 360.0);
            angleSpin->setDecimals(1);
            angleSpin->setSingleStep(5.0);
            angleSpin->setSuffix(QStringLiteral(" °"));
            auto* percentSpin = new QDoubleSpinBox;
            percentSpin->setRange(-400.0, 400.0);
            percentSpin->setDecimals(0);
            percentSpin->setSingleStep(10.0);
            percentSpin->setSuffix(QStringLiteral(" %"));
            rowLayout->addWidget(angleSpin, 1);
            rowLayout->addWidget(percentSpin, 1);
            foldLinesLayout_->addWidget(row);
            foldAngleSpins_.push_back(angleSpin);
            foldPercentSpins_.push_back(percentSpin);
            const int railIndex = static_cast<int>(index);
            connect(angleSpin, &QDoubleSpinBox::valueChanged, this,
                [this, railIndex](double degrees) {
                    if (syncingFoldRows_) {
                        return;
                    }
                    const double full = foldFullAngleDegrees_[railIndex];
                    if (std::abs(full) <= 1.0e-6) {
                        return; // 完成形が平らな折り線は角度から進行度を決められない。
                    }
                    const double value = degrees / full;
                    syncingFoldRows_ = true;
                    foldPercentSpins_[railIndex]->setValue(value * 100.0);
                    syncingFoldRows_ = false;
                    if (onRailFoldEdited) onRailFoldEdited(railIndex, value);
                });
            connect(percentSpin, &QDoubleSpinBox::valueChanged, this,
                [this, railIndex](double percent) {
                    if (syncingFoldRows_) {
                        return;
                    }
                    const double value = percent / 100.0;
                    syncingFoldRows_ = true;
                    foldAngleSpins_[railIndex]->setValue(
                        value * foldFullAngleDegrees_[railIndex]);
                    syncingFoldRows_ = false;
                    if (onRailFoldEdited) onRailFoldEdited(railIndex, value);
                });
        }
    }
    // 値の反映(入力中の欄は上書きしない)。
    syncingFoldRows_ = true;
    for (std::size_t index = 0; index < count; ++index) {
        const double value = index < progress.size() ? progress[index] : 1.0;
        if (!foldPercentSpins_[index]->hasFocus()) {
            foldPercentSpins_[index]->setValue(value * 100.0);
        }
        if (!foldAngleSpins_[index]->hasFocus()) {
            foldAngleSpins_[index]->setValue(value * fullAngleDegrees[index]);
        }
        const bool flatCrease = std::abs(fullAngleDegrees[index]) <= 1.0e-6;
        foldAngleSpins_[index]->setEnabled(!flatCrease);
    }
    syncingFoldRows_ = false;
    foldLinesContainer_->setVisible(count > 0);
}

void PartModelPanel::AddUnitMembers(const std::vector<UnitMember>& members)
{
    for (const UnitMember& member : members) {
        const auto existing = std::find_if(unitMembers_.begin(), unitMembers_.end(),
            [&](const UnitMember& candidate) {
                return candidate.name == member.name && candidate.kind == member.kind;
            });
        if (existing == unitMembers_.end()) {
            unitMembers_.push_back(member);
        }
    }
    RefreshUnitTable();
}

void PartModelPanel::ClearUnitMembers()
{
    unitMembers_.clear();
    RefreshUnitTable();
}

QString PartModelPanel::UnitName() const
{
    return unitNameEdit_ != nullptr ? unitNameEdit_->text().trimmed() : QString();
}

void PartModelPanel::SetUnitName(const QString& name)
{
    if (unitNameEdit_ != nullptr) {
        unitNameEdit_->setText(name);
    }
}

bool PartModelPanel::UnitWantsNewKcd() const
{
    return unitOutputCombo_ != nullptr && unitOutputCombo_->currentIndex() == 1;
}

bool PartModelPanel::PartOutputPlate() const
{
    return partOutputPlateCheck_ == nullptr || partOutputPlateCheck_->isChecked();
}

bool PartModelPanel::PartOutputSurface() const
{
    return partOutputSurfaceCheck_ != nullptr && partOutputSurfaceCheck_->isChecked();
}

bool PartModelPanel::PartOutputWires() const
{
    return partOutputWiresCheck_ != nullptr && partOutputWiresCheck_->isChecked();
}

bool PartModelPanel::PartPlateFollows() const
{
    return partPlateFollowCombo_ == nullptr || partPlateFollowCombo_->currentIndex() == 0;
}

void PartModelPanel::RefreshUnitTable()
{
    if (unitTable_ == nullptr) {
        return;
    }
    unitTable_->setRowCount(static_cast<int>(unitMembers_.size()));
    for (int row = 0; row < static_cast<int>(unitMembers_.size()); ++row) {
        const UnitMember& member = unitMembers_[row];
        unitTable_->setItem(row, 0, new QTableWidgetItem(member.name));
        unitTable_->setItem(row, 1, new QTableWidgetItem(
            member.kind == 1 ? QStringLiteral("面")
            : member.kind == 2 ? QStringLiteral("板材") : QStringLiteral("ワイヤ")));
        auto* roleCombo = new QComboBox;
        roleCombo->addItem(QStringLiteral("近似する"));
        roleCombo->addItem(QStringLiteral("形状維持（接続）"));
        roleCombo->addItem(QStringLiteral("対象外"));
        // ワイヤは近似できない(面・板材のみ)。
        if (member.kind == 0 && member.role == 0) {
            unitMembers_[row].role = 1;
        }
        roleCombo->setCurrentIndex(unitMembers_[row].role);
        connect(roleCombo, &QComboBox::currentIndexChanged, this, [this, row](int role) {
            if (row >= 0 && row < static_cast<int>(unitMembers_.size())) {
                if (unitMembers_[row].kind == 0 && role == 0) {
                    // ワイヤは「近似する」にできない → 形状維持へ戻す。
                    RefreshUnitTable();
                    return;
                }
                unitMembers_[row].role = role;
            }
        });
        unitTable_->setCellWidget(row, 2, roleCombo);
    }
}
