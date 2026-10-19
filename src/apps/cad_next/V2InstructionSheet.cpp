#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QUuid>
#include <QWidget>
#include "V2InstructionSheet.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QPen>
#include <QLineF>
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QPlainTextEdit>
#include <utility>
#include <algorithm>
#include <cmath>

namespace {
QRectF FittedPaper(QSize size,const InstructionSheet& sheet,double margin) {
    const double usableWidth=std::max(1.0,size.width()-margin*2);
    const double usableHeight=std::max(1.0,size.height()-margin*2);
    const double scale=std::min(usableWidth/sheet.widthMm,usableHeight/sheet.heightMm);
    const QSizeF paperSize(sheet.widthMm*scale,sheet.heightMm*scale);
    return QRectF(QPointF((size.width()-paperSize.width())/2,
                         (size.height()-paperSize.height())/2),paperSize);
}
QRectF ItemPixels(const InstructionSheetItem& item,const QRectF& paper,double scale) {
    return QRectF(paper.topLeft()+item.rectMm.topLeft()*scale,item.rectMm.size()*scale);
}
InstructionPage* SceneFor(std::vector<InstructionPage>* scenes,const QUuid& id) {
    if(!scenes)return nullptr;
    const auto found=std::find_if(scenes->begin(),scenes->end(),
        [&id](const InstructionPage& scene){return scene.id==id;});
    return found==scenes->end()?nullptr:&*found;
}
QString SceneImageKey(const InstructionPage& scene,QSize size) {
    QByteArray data;QDataStream stream(&data,QIODevice::WriteOnly);
    stream<<scene.id<<size<<scene.yaw<<scene.pitch<<scene.span<<scene.center.x<<scene.center.y<<scene.center.z;
    stream<<scene.legacyPng<<scene.legacyImage.size()<<quint64(scene.parts.size())<<quint64(scene.marks.size());
    for(const auto& part:scene.parts) {
        stream<<part.id<<part.visible<<part.offset.x<<part.offset.y<<part.offset.z<<part.rotation;
        // Captured assets are immutable snapshots; pointer identity also separates reloaded assets with the same UUID.
        stream<<part.asset->id<<quint64(reinterpret_cast<quintptr>(part.asset.get()));
    }
    for(const auto& mark:scene.marks)
        stream<<mark.arrow<<mark.text<<mark.font.family()<<mark.font.pointSizeF()
              <<mark.font.bold()<<mark.font.italic()<<mark.start.x<<mark.start.y<<mark.start.z
              <<mark.end.x<<mark.end.y<<mark.end.z;
    return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());
}
QImage SceneImage(InstructionPage& scene,QSize size,QHash<QString,QImage>& cache) {
    const auto key=SceneImageKey(scene,size);const auto found=cache.constFind(key);
    if(found!=cache.constEnd())return *found;
    V2InstructionScene renderer(nullptr);renderer.SetPage(&scene);auto image=renderer.Render(size);
    constexpr qsizetype maxBytes=32*1024*1024;
    if(image.sizeInBytes()<=maxBytes) {
        qsizetype used=0;for(auto it=cache.constBegin();it!=cache.constEnd();++it)used+=it->sizeInBytes();
        while(!cache.isEmpty()&&(cache.size()>=16||used+image.sizeInBytes()>maxBytes)) {
            auto it=cache.begin();used-=it->sizeInBytes();cache.erase(it);
        }
        cache.insert(key,image);
    }
    return image;
}
void DrawItem(QPainter& painter,const InstructionSheetItem& item,const QRectF& pixels,
              double scale,std::vector<InstructionPage>* scenes,QHash<QString,QImage>& cache) {
    painter.save();painter.setClipRect(pixels);
    if(item.kind==InstructionItemKind::Text) {
        auto font=item.font;
        font.setPixelSize(std::max(1,int(std::round(font.pointSizeF()*25.4/72*scale))));
        painter.setFont(font);painter.setPen(Qt::black);
        painter.drawText(pixels,Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap,item.text);
    } else if(item.kind==InstructionItemKind::Image) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(pixels,item.image);
    } else if(auto* scene=SceneFor(scenes,item.sceneId)) {
        const QSize renderSize(std::clamp(int(std::ceil(pixels.width()*2)),1,4096),
                               std::clamp(int(std::ceil(pixels.height()*2)),1,4096));
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(pixels,SceneImage(*scene,renderSize,cache));
    }
    painter.restore();
}
bool PlacementTool(const QString& tool) {
    return tool=="text"||tool=="image"||tool=="scene"||
           tool=="place_scene"||tool=="insert_image";
}
QPointF LimitedPosition(QPointF position,QSizeF item,const InstructionSheet& sheet) {
    return QPointF(std::clamp(position.x(),0.0,std::max(0.0,sheet.widthMm-item.width())),
                   std::clamp(position.y(),0.0,std::max(0.0,sheet.heightMm-item.height())));
}
}

V2InstructionSheetView::V2InstructionSheetView(QWidget* parent):QWidget(parent) {
    setObjectName("instructionPaperCanvas");setMinimumSize(300,260);
    setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);
    textEditor_=new QPlainTextEdit(this);textEditor_->setObjectName("instructionInlineText");textEditor_->hide();
    textEditor_->installEventFilter(this);
    connect(textEditor_,&QPlainTextEdit::textChanged,this,[this]{if(textEditor_->isVisible()&&textDraftChanged)textDraftChanged(textEditor_->toPlainText());});
}
void V2InstructionSheetView::SetDocument(InstructionSheet* sheet,std::vector<InstructionPage>* scenes) {
    CommitTextEdit();CancelTextEdit();CancelInput();
    if(sheet&&sheet->id!=viewSheetId_){zoom_=1;pan_={};viewSheetId_=sheet->id;}
    sheet_=sheet;scenes_=scenes;selected_=-1;sceneImageCache_.clear();CancelInput();Refresh();
}
void V2InstructionSheetView::SetTool(const QString& tool,const InstructionSheetItem& item) {
    CommitTextEdit();CancelInput();tool_=tool;pending_=item;
    setCursor(PlacementTool(tool)?Qt::CrossCursor:Qt::ArrowCursor);Refresh();
}
void V2InstructionSheetView::CancelInput() {
    if(dragging_&&sheet_&&selected_>=0&&selected_<int(sheet_->items.size()))sheet_->items[selected_].rectMm=dragOriginal_;
    dragging_=false;resizing_=false;placementHover_=false;Refresh();
    panning_=false;
}
void V2InstructionSheetView::Refresh() {update();}
void V2InstructionSheetView::SetSelected(int index) {
    CancelInput();
    selected_=sheet_&&index>=0&&index<int(sheet_->items.size())?index:-1;
    if(selectionChanged)selectionChanged();Refresh();
}
QRectF V2InstructionSheetView::PaperRect() const {
    return ViewPaperRect(size());
}
QRectF V2InstructionSheetView::ViewPaperRect(QSize size) const {
    if(!sheet_)return {};
    const auto fitted=FittedPaper(size,*sheet_,20);
    const QSizeF scaled=fitted.size()*zoom_;
    return QRectF(fitted.center()-QPointF(scaled.width()/2,scaled.height()/2)+pan_,scaled);
}
QPointF V2InstructionSheetView::ToScreen(QPointF mm) const {
    if(!sheet_)return {};
    const auto paper=PaperRect();return paper.topLeft()+mm*(paper.width()/sheet_->widthMm);
}
QPointF V2InstructionSheetView::ToMm(QPointF screen) const {
    if(!sheet_)return {};
    const auto paper=PaperRect();return (screen-paper.topLeft())/(paper.width()/sheet_->widthMm);
}
QImage V2InstructionSheetView::Render(QSize size,bool selection) {
    if(!sheet_||size.width()<1||size.height()<1)return {};
    QImage image(size,QImage::Format_RGB32);image.fill(selection?QColor(220,222,225):QColor(Qt::white));
    QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);
    const auto paper=selection?ViewPaperRect(size):FittedPaper(size,*sheet_,0);
    const double scale=paper.width()/sheet_->widthMm;
    painter.fillRect(paper,Qt::white);painter.save();painter.setClipRect(paper);
    for(std::size_t i=0;i<sheet_->items.size();++i) {
        const auto pixels=ItemPixels(sheet_->items[i],paper,scale);
        DrawItem(painter,sheet_->items[i],pixels,scale,scenes_,sceneImageCache_);
    }
    if(selection&&selected_>=0&&selected_<int(sheet_->items.size())) {
            const auto pixels=ItemPixels(sheet_->items[selected_],paper,scale);
            painter.setPen(QPen(QColor(20,100,210),1.5,Qt::DashLine));painter.setBrush(Qt::NoBrush);
            painter.drawRect(pixels);painter.setPen(QPen(QColor(20,100,210),1));
            painter.setBrush(Qt::white);
            for(const auto point:{pixels.topLeft(),QPointF(pixels.center().x(),pixels.top()),pixels.topRight(),
                QPointF(pixels.right(),pixels.center().y()),pixels.bottomRight(),QPointF(pixels.center().x(),pixels.bottom()),
                pixels.bottomLeft(),QPointF(pixels.left(),pixels.center().y())})
                painter.drawRect(QRectF(point-QPointF(4,4),QSizeF(8,8)));
    }
    if(selection&&placementHover_&&PlacementTool(tool_)){
        auto ghost=pending_;ghost.rectMm.setWidth(std::clamp(ghost.rectMm.width(),.01,sheet_->widthMm));ghost.rectMm.setHeight(std::clamp(ghost.rectMm.height(),.01,sheet_->heightMm));
        ghost.rectMm.moveTopLeft(LimitedPosition(placementAt_,ghost.rectMm.size(),*sheet_));
        const auto pixels=ItemPixels(ghost,paper,scale);painter.setOpacity(.5);DrawItem(painter,ghost,pixels,scale,scenes_,sceneImageCache_);painter.setOpacity(1);
        painter.setPen(QPen(QColor(20,100,210),1.5,Qt::DashLine));painter.setBrush(Qt::NoBrush);painter.drawRect(pixels);
    }
    painter.restore();
    if(selection){painter.setPen(QPen(QColor(130,135,140),1));painter.setBrush(Qt::NoBrush);painter.drawRect(paper);}
    return image;
}
void V2InstructionSheetView::paintEvent(QPaintEvent*) {
    QPainter painter(this);painter.fillRect(rect(),QColor(220,222,225));
    painter.drawImage(0,0,Render(size(),true));
}
void V2InstructionSheetView::mousePressEvent(QMouseEvent* event) {
    if(!sheet_)return;
    if(event->button()==Qt::MiddleButton){CancelInput();panning_=true;panLast_=event->position();setCursor(Qt::ClosedHandCursor);return;}
    if(event->button()!=Qt::LeftButton)return;
    if(!CommitTextEdit())return;setFocus(Qt::MouseFocusReason);
    const auto point=event->position();last_=ToMm(point);
    if(PlacementTool(tool_)) {
        if(!PaperRect().contains(point))return;
        QString error;
        if(sheet_->items.size()>=1000)error=QStringLiteral("1枚の用紙には1000個まで配置できます。別の用紙を追加してください。");
        else if(pending_.kind==InstructionItemKind::Text&&pending_.text.trimmed().isEmpty())error=QStringLiteral("右ペインに文章を入力してから置いてください。");
        else if(pending_.kind==InstructionItemKind::Text&&pending_.text.size()>100000)error=QStringLiteral("文章は100000文字以内で入力してください。");
        else if(pending_.kind==InstructionItemKind::Scene&&!SceneFor(scenes_,pending_.sceneId))error=QStringLiteral("配置する場面を選んでください。");
        else if(pending_.kind==InstructionItemKind::Image&&pending_.image.isNull())error=QStringLiteral("貼る画像を選んでください。");
        if(!error.isEmpty()){if(refused)refused(error);return;}
        if(beginChange)beginChange();auto item=pending_;item.id=QUuid::createUuid();
        item.rectMm.setWidth(std::clamp(item.rectMm.width(),.01,sheet_->widthMm));
        item.rectMm.setHeight(std::clamp(item.rectMm.height(),.01,sheet_->heightMm));
        item.rectMm.moveTopLeft(LimitedPosition(last_,item.rectMm.size(),*sheet_));
        sheet_->items.push_back(std::move(item));SetSelected(int(sheet_->items.size())-1);
        if(endChange)endChange();Refresh();return;
    }
    if(selected_>=0&&selected_<int(sheet_->items.size())) {
        resizeHandle_=HandleAt(point);
        if(resizeHandle_>=0) {
            if(beginChange)beginChange();dragOriginal_=sheet_->items[selected_].rectMm;resizing_=true;dragging_=true;Refresh();return;
        }
    }
    int index=-1;
    for(std::size_t i=sheet_->items.size();i>0;--i)
        if(sheet_->items[i-1].rectMm.contains(last_)){index=int(i-1);break;}
    SetSelected(index);
    if(index>=0){if(beginChange)beginChange();dragOriginal_=sheet_->items[index].rectMm;dragging_=true;resizing_=false;}
}
void V2InstructionSheetView::mouseMoveEvent(QMouseEvent* event) {
    if(!sheet_)return;
    if(panning_){pan_+=event->position()-panLast_;panLast_=event->position();Refresh();return;}
    if(PlacementTool(tool_)){placementHover_=PaperRect().contains(event->position());placementAt_=ToMm(event->position());Refresh();return;}
    if(!dragging_||selected_<0||selected_>=int(sheet_->items.size())) {
        if(!PlacementTool(tool_)&&selected_>=0&&selected_<int(sheet_->items.size()))
            {const int handle=HandleAt(event->position());
            const Qt::CursorShape cursors[]={Qt::SizeFDiagCursor,Qt::SizeVerCursor,Qt::SizeBDiagCursor,Qt::SizeHorCursor,
                Qt::SizeFDiagCursor,Qt::SizeVerCursor,Qt::SizeBDiagCursor,Qt::SizeHorCursor};
            setCursor(handle>=0?cursors[handle]:Qt::ArrowCursor);}
        return;
    }
    auto& item=sheet_->items[selected_];const auto next=ToMm(event->position());
    if(resizing_) {
        const int horizontal[]={-1,0,1,1,1,0,-1,-1},vertical[]={-1,-1,-1,0,1,1,1,0};
        const int dx=horizontal[resizeHandle_],dy=vertical[resizeHandle_];
        auto rect=dragOriginal_;
        const auto widthLimits=[](double anchor,int direction) {
            if(direction<0){const double upper=std::clamp(anchor+100000,.01,100000.0);return std::pair{std::clamp(anchor-100000,.01,upper),upper};}
            if(direction>0)return std::pair{.01,100000.0};
            const double upper=std::clamp(2*(anchor+100000),.01,100000.0);
            return std::pair{std::clamp(2*(anchor-100000),.01,upper),upper};
        };
        const double anchorX=dx<0?dragOriginal_.right():dx>0?dragOriginal_.left():dragOriginal_.center().x();
        const double anchorY=dy<0?dragOriginal_.bottom():dy>0?dragOriginal_.top():dragOriginal_.center().y();
        const auto [minWidth,maxWidth]=widthLimits(anchorX,dx);
        const auto [minHeight,maxHeight]=widthLimits(anchorY,dy);
        double newWidth=dx?std::clamp(dx<0?anchorX-next.x():next.x()-anchorX,minWidth,maxWidth):dragOriginal_.width();
        double newHeight=dy?std::clamp(dy<0?anchorY-next.y():next.y()-anchorY,minHeight,maxHeight):dragOriginal_.height();
        if(keepAspect_||(event->modifiers()&Qt::ShiftModifier)) {
            const double ratio=dragOriginal_.height()/dragOriginal_.width();
            const double minimum=std::max(minWidth,minHeight/ratio);
            const double maximum=std::max(minimum,std::min(maxWidth,maxHeight/ratio));
            newWidth=std::clamp(dx?newWidth:newHeight/ratio,minimum,maximum);
            newHeight=std::clamp(newWidth*ratio,.01,100000.0);
        }
        const double left=dx<0?anchorX-newWidth:dx>0?anchorX:anchorX-newWidth/2;
        const double top=dy<0?anchorY-newHeight:dy>0?anchorY:anchorY-newHeight/2;
        rect=QRectF(QPointF(std::clamp(left,-100000.0,100000.0),std::clamp(top,-100000.0,100000.0)),QSizeF(newWidth,newHeight));
        item.rectMm=rect;
    } else item.rectMm.moveTopLeft(LimitedPosition(item.rectMm.topLeft()+next-last_,item.rectMm.size(),*sheet_));
    last_=next;Refresh();
}
void V2InstructionSheetView::mouseReleaseEvent(QMouseEvent* event) {
    if(event->button()==Qt::MiddleButton&&panning_){panning_=false;setCursor(PlacementTool(tool_)?Qt::CrossCursor:Qt::ArrowCursor);return;}
    if(event->button()!=Qt::LeftButton||!dragging_)return;
    dragging_=false;resizing_=false;if(endChange)endChange();if(selectionChanged)selectionChanged();Refresh();
}
void V2InstructionSheetView::mouseDoubleClickEvent(QMouseEvent* event) {
    if(!sheet_||tool_!="move"||event->button()!=Qt::LeftButton)return;
    const auto point=ToMm(event->position());
    for(std::size_t i=sheet_->items.size();i>0;--i) {
        const auto& item=sheet_->items[i-1];
        if(item.rectMm.contains(point)) {
            if(item.kind==InstructionItemKind::Text){dragging_=false;SetSelected(int(i-1));if(editText)editText();}
            if(item.kind==InstructionItemKind::Scene&&openScene)openScene(item.sceneId);
            return;
        }
    }
}
