#include <QSize>
#include <QStyle>
#include <QWidget>
#include "V2ErgonomicStyle.h"
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QString>
#include <algorithm>
QSize V2ControlSize(QStyle::ContentsType type,QSize size){
    int height=0;
    switch(type){
    case QStyle::CT_PushButton:case QStyle::CT_ToolButton:height=28;break;
    case QStyle::CT_ComboBox:case QStyle::CT_SpinBox:case QStyle::CT_LineEdit:case QStyle::CT_TabBarTab:height=28;break;
    case QStyle::CT_CheckBox:case QStyle::CT_RadioButton:case QStyle::CT_MenuBarItem:height=24;break;
    default:break;
    }
    if(height){size.setHeight(std::max(height,size.height()));size.setWidth(std::max(24,size.width()));}
    return size;
}
namespace {
class ErgonomicStyle final:public QProxyStyle {
public:
    ErgonomicStyle():QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))){}
    QSize sizeFromContents(ContentsType t,const QStyleOption* o,const QSize& s,const QWidget* w) const override{
        return V2ControlSize(t,QProxyStyle::sizeFromContents(t,o,s,w));
    }
};
}
QStyle* CreateV2NormalStyle(){return new ErgonomicStyle();}
