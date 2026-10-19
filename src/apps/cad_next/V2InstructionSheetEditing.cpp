#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>
#include "V2InstructionSheet.h"
#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QTimer>
#include <algorithm>
#include <cmath>

int V2InstructionSheetView::HandleAt(QPointF point) const {
    if(!sheet_||selected_<0||selected_>=int(sheet_->items.size()))return -1;
    const auto rect=sheet_->items[selected_].rectMm;
    int index=0,best=-1;double distance=10.0;
    for(const auto mm:{rect.topLeft(),QPointF(rect.center().x(),rect.top()),rect.topRight(),
        QPointF(rect.right(),rect.center().y()),rect.bottomRight(),QPointF(rect.center().x(),rect.bottom()),
        rect.bottomLeft(),QPointF(rect.left(),rect.center().y())}) {
        const double candidate=QLineF(point,ToScreen(mm)).length();
        if(candidate<=distance){distance=candidate;best=index;}
        ++index;
    }
    return best;
}
void V2InstructionSheetView::BeginTextEdit() {
    if(!sheet_||selected_<0||selected_>=int(sheet_->items.size()))return;
    auto& item=sheet_->items[selected_];if(item.kind!=InstructionItemKind::Text)return;
    CancelInput();textItemId_=item.id;++textEditEpoch_;
    const QSignalBlocker blocked(textEditor_);textEditor_->setPlainText(item.text);
    const double scale=PaperRect().width()/sheet_->widthMm;
    auto font=item.font;font.setPixelSize(std::max(1,int(std::round(font.pointSizeF()*25.4/72*scale))));
    textEditor_->setFont(font);
    textEditor_->setGeometry(QRectF(ToScreen(item.rectMm.topLeft()),item.rectMm.size()*scale).toAlignedRect());
    textEditor_->show();textEditor_->raise();textEditor_->setFocus();textEditor_->selectAll();
}
bool V2InstructionSheetView::CommitTextEdit() {
    if(!textEditor_||textItemId_.isNull())return true;
    const auto text=textEditor_->toPlainText();
    if(text.trimmed().isEmpty()||text.size()>100000) {
        if(refused)refused(QStringLiteral("文章は1文字以上、100000文字以内で入力してください。Escで編集前に戻せます。"));
        textEditor_->setFocus();
        return false;
    }
    const auto id=textItemId_;textItemId_={};textEditor_->hide();
    if(sheet_)for(auto& item:sheet_->items)if(item.id==id&&item.kind==InstructionItemKind::Text&&item.text!=text) {
        if(beginChange)beginChange();item.text=text;if(endChange)endChange();break;
    }
    Refresh();return true;
}
void V2InstructionSheetView::CancelTextEdit() {
    if(textItemId_.isNull())return;
    if(sheet_)for(const auto& item:sheet_->items)if(item.id==textItemId_&&textDraftChanged)textDraftChanged(item.text);
    textItemId_={};textEditor_->hide();setFocus();Refresh();
}
bool V2InstructionSheetView::eventFilter(QObject* object,QEvent* event) {
    if(object==textEditor_&&event->type()==QEvent::KeyPress) {
        const auto* key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Escape){CancelTextEdit();return true;}
        if(key->key()==Qt::Key_Return&&(key->modifiers()&Qt::ControlModifier)) {
            if(CommitTextEdit()){setFocus();if(selectionChanged)selectionChanged();}return true;
        }
    }
    if(object==textEditor_&&event->type()==QEvent::FocusOut) {
        const auto epoch=textEditEpoch_;
        QTimer::singleShot(0,this,[this,epoch]{if(epoch==textEditEpoch_&&!textItemId_.isNull())CommitTextEdit();});
    }
    return QWidget::eventFilter(object,event);
}
void V2InstructionSheetView::contextMenuEvent(QContextMenuEvent* event) {
    if(!sheet_||tool_!="move"||!CommitTextEdit())return;
    const auto mm=ToMm(event->pos());int index=-1;
    for(std::size_t i=sheet_->items.size();i>0;--i)if(sheet_->items[i-1].rectMm.contains(mm)){index=int(i-1);break;}
    SetSelected(index);if(index<0)return;
    auto* menu=new QMenu(this);menu->setObjectName("instructionItemMenu");menu->setAttribute(Qt::WA_DeleteOnClose);
    if(sheet_->items[index].kind==InstructionItemKind::Text) {
        auto* action=menu->addAction(QStringLiteral("文字を編集"));action->setObjectName("instructionContextText");
        connect(action,&QAction::triggered,this,[this]{if(editText)editText();});
    }
    for(const auto& action:{std::pair{"duplicate","複製"},std::pair{"front","最前面へ"},std::pair{"back","最背面へ"}}) {
        auto* entry=menu->addAction(QString::fromUtf8(action.second));entry->setObjectName(QStringLiteral("instructionContext_")+action.first);
        connect(entry,&QAction::triggered,this,[this,id=QString::fromLatin1(action.first)]{AdjustItem(id);});
    }
    menu->addSeparator();auto* remove=menu->addAction(QStringLiteral("削除"));remove->setObjectName("instructionContextDelete");
    connect(remove,&QAction::triggered,this,[this]{if(removeItem)removeItem();});menu->popup(event->globalPos());
}
