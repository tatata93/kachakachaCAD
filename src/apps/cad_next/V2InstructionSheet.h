#pragma once
#include "V2InstructionScene.h"
#include <QRectF>
#include <QByteArray>
#include <QJsonArray>
#include <QHash>
enum class InstructionItemKind { Scene, Image, Text };
struct InstructionSheetItem {
    QUuid id=QUuid::createUuid();
    InstructionItemKind kind=InstructionItemKind::Scene;
    QUuid sceneId;
    QRectF rectMm{15,15,80,60};
    QImage image;
    QByteArray imageBytes;
    QString imageFormat,text;
    QFont font=QFont(QString(),12);
};
struct InstructionSheet {
    QUuid id=QUuid::createUuid();
    QString title;
    double widthMm=210,heightMm=297;
    std::vector<InstructionSheetItem> items;
};
class V2InstructionSheetView final:public QWidget {
public:
    explicit V2InstructionSheetView(QWidget* parent=nullptr);
    void SetDocument(InstructionSheet*,std::vector<InstructionPage>*);
    void SetTool(const QString&,const InstructionSheetItem& item={});
    void CancelInput();
    void Refresh();
    int Selected() const {return selected_;}
    void SetSelected(int);
    QPointF ToScreen(QPointF mm) const;
    QImage Render(QSize,bool selection=false);
    std::function<void()> beginChange,endChange,selectionChanged;
    std::function<void(QUuid)> openScene;
    std::function<void(QString)> refused;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
private:
    QPointF ToMm(QPointF) const;
    QRectF PaperRect() const;
    InstructionSheet* sheet_=nullptr;
    std::vector<InstructionPage>* scenes_=nullptr;
    QString tool_="move";
    InstructionSheetItem pending_;
    int selected_=-1;
    bool dragging_=false,resizing_=false;
    QPointF last_;
    QRectF dragOriginal_;
    QHash<QString,QImage> sceneImageCache_;
};
QJsonArray EncodeInstructionSheets(const std::vector<InstructionSheet>&);
bool DecodeInstructionSheets(const QJsonArray&,const std::vector<InstructionPage>&,std::vector<InstructionSheet>&);
