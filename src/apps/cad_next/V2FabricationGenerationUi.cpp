#include <QObject>
#include <QString>
#include <QWidget>
#include "V2FabricationDock.h"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
namespace {
QPushButton* RunButton(QWidget* parent,const QString& text,const char* command,V2FabricationDock* dock)
{
    auto* button=new QPushButton(text,parent);
    QObject::connect(button,&QPushButton::clicked,dock,[dock,command]{dock->PressRun(command);});
    return button;
}
}
QWidget* V2FabricationDock::BuildFreezeSection(QWidget* body)
{
    auto* freezeButtons = new QWidget(body);
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
    freezeLayout->addWidget(new QLabel(QStringLiteral("対象部材の指定だけ生成（空欄なら全部）"),freezeButtons));
    freezeLayout->addWidget(new QLabel(QStringLiteral("生成(作り方)"), freezeButtons));
    generateCards_.push_back(
        RunButton(freezeButtons, QStringLiteral("現在状態"), "fabrication.freeze_state", this));
    generateCards_.push_back(
        RunButton(freezeButtons, QStringLiteral("Flat 0%"), "fabrication.freeze_flat", this));
    generateCards_.push_back(
        RunButton(freezeButtons, QStringLiteral("Target 100%"), "fabrication.freeze_target", this));
    for (QPushButton* card : generateCards_) {
        freezeLayout->addWidget(card);
    }
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
    layout->addWidget(BuildGenerationDestination(body)); return body;
}
void V2FabricationDock::SetGenerationDestination(int value)
{
    generationDestination_=value;
    for(auto* box:generationDestinations_) { if(box->currentIndex()!=value) { box->setCurrentIndex(value); } }
}
