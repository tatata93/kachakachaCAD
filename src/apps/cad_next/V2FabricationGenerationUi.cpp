#include <QObject>
#include <QString>
#include <QWidget>
#include "V2FabricationDock.h"
#include "V2PanelFrame.h"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QTabWidget>
namespace {
QComboBox* GenerationPlacement(QWidget* parent,V2FabricationDock* dock) {
    auto* box=new QComboBox(parent);box->setObjectName("generationPlacement");
    box->addItems({QStringLiteral("元の位置を維持"),QStringLiteral("基準点を面へ配置（プレビューで指定）")});
    const auto peers=dock->findChildren<QComboBox*>("generationPlacement");
    if(!peers.isEmpty())box->setCurrentIndex(peers.front()->currentIndex());
    QObject::connect(box,&QComboBox::currentIndexChanged,dock,[dock](int value){
        for(auto* peer:dock->findChildren<QComboBox*>("generationPlacement"))if(peer->currentIndex()!=value)peer->setCurrentIndex(value);
    });return box;
}
QPushButton* RunButton(QWidget* parent,const QString& text,const char* command,V2FabricationDock* dock)
{
    auto* button=new QPushButton(text,parent);
    button->setCheckable(true);
    QObject::connect(button,&QPushButton::clicked,dock,[dock,command]{dock->ChooseGenerationCommand(command);});
    return button;
}
}
QWidget* V2FabricationDock::BuildFreezeSection(QWidget* body)
{
    auto* freezeButtons = new QWidget(body);
    freezeButtons->setObjectName(QStringLiteral("freezeWidget"));
    auto* freezeLayout = new QVBoxLayout(freezeButtons);
    freezeLayout->setContentsMargins(0, 0, 0, 0);
    freezeLayout->setSpacing(2);
    freeze_ = new QComboBox(freezeButtons);
    freeze_->addItem(QStringLiteral("ワイヤーのみ"));
    freeze_->addItem(QStringLiteral("部品のみ"));
    freeze_->addItem(QStringLiteral("ワイヤー＋厚み付き部品"));
    freeze_->addItem(QStringLiteral("ワイヤー＋近似面"));
    freeze_->setObjectName(QStringLiteral("fabricationGenerationOutput"));
    auto* freezeOutputRow = new QWidget(freezeButtons);
    auto* freezeOutputLayout = new QHBoxLayout(freezeOutputRow);
    freezeOutputLayout->setContentsMargins(0, 0, 0, 0);
    freezeOutputLayout->addWidget(new QLabel(QStringLiteral("生成するもの"), freezeOutputRow));
    freezeOutputLayout->addWidget(freeze_, 1);
    freezeLayout->addWidget(freezeOutputRow);
    freezeLayout->addWidget(BuildGenerationDestination(freezeButtons));
    freezeLayout->addWidget(GenerationPlacement(freezeButtons,this));
    freezeLayout->addWidget(new QLabel(QStringLiteral("指定した対象部材を生成します"),freezeButtons));
    freezeLayout->addWidget(new QLabel(QStringLiteral("曲げ状態"), freezeButtons));
    generateCards_.push_back(
        RunButton(freezeButtons, QStringLiteral("現在状態"), "fabrication.freeze_state", this));
    generateCards_.push_back(
        RunButton(freezeButtons, QStringLiteral("Flat 0%"), "fabrication.freeze_flat", this));
    generateCards_.push_back(
        RunButton(freezeButtons, QStringLiteral("Target 100%"), "fabrication.freeze_target", this));
    for (QPushButton* card : generateCards_) {
        freezeLayout->addWidget(card);
    }
    ChooseGenerationCommand("fabrication.freeze_state");
    QPushButton* cancel = nullptr;
    QPushButton* confirm = nullptr;
    generationFooter_ = new QWidget(actionFooter_);
    auto* actions = new QVBoxLayout(generationFooter_);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->addLayout(MakeCancelConfirmRow(generationFooter_, &cancel, &confirm));
    actionFooter_->layout()->addWidget(generationFooter_);
    generationFooter_->hide();
    confirm->setText(QStringLiteral("形状を作成"));
    confirm->setObjectName(QStringLiteral("generationConfirm"));
    QObject::connect(cancel, &QPushButton::clicked, this, [this] { HandleKey(Qt::Key_Escape); });
    QObject::connect(confirm, &QPushButton::clicked, this, [this] { const auto command = generationCommand_; PressRun(command.c_str()); });
    return freezeButtons;
}


QWidget* V2FabricationDock::BuildGenerationDestination(QWidget* parent)
{
    auto* row=new QWidget(parent); auto* layout=new QHBoxLayout(row); layout->setContentsMargins(0,0,0,0);
    layout->addWidget(new QLabel(QStringLiteral("生成先"),row));
    auto* target=new QComboBox(row); target->addItems({QStringLiteral("同じkcd2内"),QStringLiteral("別のkcd2ファイル")});
    target->setObjectName(QStringLiteral("fabricationGenerationDestination")); target->setCurrentIndex(generationDestination_);
    generationDestinations_.push_back(target); layout->addWidget(target,1);
    QObject::connect(target,&QComboBox::currentIndexChanged,this,[this](int value){SetGenerationDestination(value);});
    return row;
}
QWidget* V2FabricationDock::BuildGenerationOptions(QWidget* parent)
{
    auto* body=new QWidget(parent); auto* layout=new QVBoxLayout(body); layout->setContentsMargins(0,0,0,0);
    auto* mode=new QComboBox(body); mode->setObjectName(QStringLiteral("fabricationAutomaticGeneration"));
    mode->addItems({QStringLiteral("確定時：近似モデルのみ"),QStringLiteral("確定時：ワイヤーも生成"),QStringLiteral("確定時：ワイヤー＋近似面も生成")});
    mode->setCurrentIndex(automaticGeneration_); automaticGenerations_.push_back(mode); layout->addWidget(mode);
    QObject::connect(mode,&QComboBox::currentIndexChanged,this,[this](int value){
        automaticGeneration_=value;
        for (auto* box:automaticGenerations_) { if(box->currentIndex()!=value) { box->setCurrentIndex(value); } }
    });
    layout->addWidget(BuildGenerationDestination(body));layout->addWidget(GenerationPlacement(body,this)); return body;
}
void V2FabricationDock::SetGenerationDestination(int value)
{
    generationDestination_=value;
    for(auto* box:generationDestinations_) { if(box->currentIndex()!=value) { box->setCurrentIndex(value); } }
}

QWidget* V2FabricationDock::BuildTargetSection(QWidget* body)
{
    auto* bendWidget = new QWidget(body);
    auto* bend = new QFormLayout(bendWidget);
    parts_ = new QLineEdit(bendWidget);
    parts_->setText(QStringLiteral("すべて"));
    parts_->setObjectName(QStringLiteral("fabricationTargets"));
    QObject::connect(parts_, &QLineEdit::textChanged, this, [this] {
        if (partNumbersChanged_) {
            partNumbersChanged_();
        }
    });
    parts_->setPlaceholderText(QStringLiteral("1, 3 のように部材番号を指定"));
    parts_->setToolTip(QStringLiteral(
        "3D で部材を押すと、押した番号がここに入ります(Ctrl で足す・外す)。"
        "手で番号(1 から)を書いてもかまいません。"
        "挙げた部材だけが曲がります(V1 と同じ)。"
        "全体を対象にするときは「すべて」を押してください。"));
    // ラベルは短く「対象部材」に。詳しい使い方は欄のツールチップに既にある
    // (380px の棚でラベルが折り返さないため、ここでは短く)。
    auto* partsLabel = new QLabel(QStringLiteral("対象部材"), bendWidget);
    partsLabel->setToolTip(QStringLiteral("3D で押す、または番号を書きます。"));
    auto* targetRow = new QWidget(bendWidget);
    auto* targetLayout = new QHBoxLayout(targetRow);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    targetLayout->addWidget(parts_, 1);
    auto* all = new QPushButton(QStringLiteral("すべて"), targetRow);
    all->setObjectName(QStringLiteral("fabricationAllTargets"));
    QObject::connect(all, &QPushButton::clicked, this, [this] {
        parts_->setText(QStringLiteral("すべて"));
    });
    targetLayout->addWidget(all);
    bend->addRow(partsLabel, targetRow);
    partInfo_ = new QLabel(QStringLiteral("(3D で部材を押すと出ます)"), bendWidget);
    partInfo_->setWordWrap(true);
    bend->addRow(QStringLiteral("方式 / 最大誤差"), partInfo_);
    return bendWidget;
}

void V2FabricationDock::FocusCommand(std::string_view command)
{
    if (command == "fabrication.freeze_output") return;
    const bool approx = command == "fabrication.create" || command == "fabrication.preview_update"
        || command == "fabrication.set_method" || command == "fabrication.set_connection_scope";
    if (approx) activeCommand_.clear();
    else if (command == "fabrication.set_assembly" || command == "fabrication.edit_part"
        || command == "fabrication.split_part" || command == "fabrication.merge_parts"
        || command == "fabrication.create_pattern" || command == "fabrication.set_unfold_base"
        || command.find("fabrication.freeze_") == 0) activeCommand_ = std::string(command);
    SetStageIndex(approx ? 0 : 1);
    approxFooter_->setVisible(approx);
    const bool bending = command == "fabrication.set_assembly" || command == "fabrication.edit_part";
    const bool unfolding = command == "fabrication.create_pattern";
    const bool generating = command.find("fabrication.freeze_") == 0;
    generationFooter_->setVisible(generating);
    actionFooter_->findChild<QWidget*>(QStringLiteral("bendActions"))->setVisible(bending);
    actionFooter_->findChild<QWidget*>(QStringLiteral("unfoldActions"))->setVisible(unfolding);
    actionFooter_->findChild<QWidget*>(QStringLiteral("partitionActions"))->setVisible(
        command == "fabrication.split_part" || command == "fabrication.merge_parts");
    const auto show = [this](const char* name, bool visible) {
        if (auto* widget = stages_->findChild<QWidget*>(QString::fromUtf8(name)))
            widget->setVisible(visible);
    };
    show("bendWidget", bending);
    show("materialOptions", command == "fabrication.edit_part");
    show("unfoldWidget", unfolding);
    show("freezeWidget", generating);
    if (generating) ChooseGenerationCommand(command);
    show("editWidget", !approx && !bending && !unfolding && !generating);
    show("splitRow", command == "fabrication.split_part");
    for (const char* id : {"fabrication.merge_parts", "fabrication.assign_relief_cut",
             "fabrication.set_unfold_base"}) show(id, command == id);
}

void V2FabricationDock::ChooseGenerationCommand(std::string_view command)
{
    generationCommand_ = std::string(command);
    const char* ids[] = {"fabrication.freeze_state", "fabrication.freeze_flat", "fabrication.freeze_target"};
    for (std::size_t at = 0; at < generateCards_.size(); ++at)
        generateCards_[at]->setChecked(generationCommand_ == ids[at]
            || (at == 0 && command == "fabrication.freeze_wires"));
    freeze_->setEnabled(command != "fabrication.freeze_wires");
    freeze_->setToolTip(command == "fabrication.freeze_wires"
        ? QStringLiteral("輪郭 Wire はワイヤーのみを生成します。") : QString());
}

bool V2FabricationDock::ClickGenerateConfirm()
{
    auto* button = generationFooter_->findChild<QPushButton*>(QStringLiteral("generationConfirm"));
    if (button == nullptr || !button->isVisible() || !button->isEnabled()) return false;
    button->click();
    return true;
}

bool V2FabricationDock::HandleKey(int key)
{
    if (!ToolActive()) return false;
    if (key == Qt::Key_Escape) {
        SetMessage(QStringLiteral("道具はそのままです。対象や条件を選び直せます。")); return true;
    }
    if (key != Qt::Key_Return && key != Qt::Key_Enter) return false;
    if (activeCommand_.find("fabrication.freeze_") == 0) {
        (void)ClickGenerateConfirm(); return true;
    }
    if (activeCommand_ == "fabrication.set_assembly" || activeCommand_ == "fabrication.edit_part") {
        PressApplyAssembly(); return true;
    }
    if (activeCommand_ == "fabrication.create_pattern") { PressUnfold(); return true; }
    if (activeCommand_ == "fabrication.split_part" || activeCommand_ == "fabrication.merge_parts") {
        actionFooter_->findChild<QWidget*>(QStringLiteral("partitionActions"))
            ->findChild<QPushButton*>(QStringLiteral("panelConfirm"))->click();
        return true;
    }
    const auto command = activeCommand_;
    PressRun(command.c_str()); return true;
}
