#include "V2FabricationDock.h"
#include "V2PanelFrame.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QObject>
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>

QWidget* V2FabricationDock::ArrangeQuickOptions(QWidget* details)
{
    auto* wrapper=new QWidget(details->parentWidget());
    auto* layout=new QVBoxLayout(wrapper);layout->setContentsMargins(0,0,0,0);
    auto* quick=new QWidget(wrapper);quick->setObjectName(QStringLiteral("fabricationQuickSettings"));
    auto* form=new QFormLayout(quick);form->setContentsMargins(0,0,0,0);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->addRow(MakePanelSectionTitle(quick,QStringLiteral("近似の設定")));
    for(QWidget* field : {static_cast<QWidget*>(equalParts_),static_cast<QWidget*>(deviation_),
            static_cast<QWidget*>(splitAxis_),static_cast<QWidget*>(adaptive_)}) {
        const auto row=form_->takeRow(field);
        auto* label=row.labelItem ? row.labelItem->widget() : nullptr;
        field->setParent(quick);if(label)label->setParent(quick);
        form->addRow(label,field);delete row.labelItem;delete row.fieldItem;
    }
    equalParts_->setObjectName(QStringLiteral("fabricationPartCount"));
    deviation_->setObjectName(QStringLiteral("fabricationTolerance"));
    splitAxis_->setObjectName(QStringLiteral("fabricationDirection"));
    method_->setObjectName(QStringLiteral("fabricationMethod"));
    layout->addWidget(quick);
    auto* toggle=new QPushButton(QStringLiteral("▸ 詳細設定"),wrapper);
    toggle->setObjectName(QStringLiteral("fabricationDetailsToggle"));toggle->setCheckable(true);
    layout->addWidget(toggle);
    advancedWidget_=new QWidget(wrapper);
    auto* advanced=new QVBoxLayout(advancedWidget_);advanced->setContentsMargins(0,0,0,0);
    advanced->addWidget(details);layout->addWidget(advancedWidget_);advancedWidget_->hide();
    QObject::connect(toggle,&QPushButton::toggled,advancedWidget_,&QWidget::setVisible);
    QObject::connect(toggle,&QPushButton::toggled,toggle,[toggle](bool open){
        toggle->setText(open ? QStringLiteral("▾ 詳細設定") : QStringLiteral("▸ 詳細設定"));
    });
    return wrapper;
}

void V2FabricationDock::QuickOptionsChanged()
{
    if(loading_)return;
    // Everyday split settings select the configured split preview; they must not
    // remain inert because a classification candidate was previously displayed.
    loading_=true;method_->setCurrentIndex(0);
    if(equalParts_->value()>maxParts_->value())maxParts_->setValue(equalParts_->value());
    loading_=false;
    if(candidateHandler_)candidateHandler_(1);
    RefreshMethodRows();Emit();
}
