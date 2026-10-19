#pragma once
#include "V2InstructionScene.h"
#include <QRectF>
#include <QByteArray>
#include <QJsonArray>
#include <QHash>
#include <QWheelEvent>
#include <QKeyEvent>
class QPlainTextEdit;
class QContextMenuEvent;
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
    double Zoom() const {return zoom_;}
    std::size_t SceneRenderCount() const {return sceneRenders_;}
    qsizetype SceneCacheBytes() const;
    void SetZoom(double);
    void FitView();
    bool KeepAspect() const {return keepAspect_;}
    void SetKeepAspect(bool value) {keepAspect_=value;}
    void AdjustItem(const QString& action);
    void BeginTextEdit();
    bool CommitTextEdit();
    QPointF ToScreen(QPointF mm) const;
    QImage Render(QSize,bool selection=false);
    std::function<void()> beginChange,endChange,selectionChanged;
    std::function<void(QUuid)> openScene;
    std::function<void(QString)> refused;
    std::function<void()> viewChanged;
    std::function<void()> editText;
    std::function<void()> removeItem;
    std::function<void(QString)> textDraftChanged;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
private:
    QPointF ToMm(QPointF) const;
    QRectF PaperRect() const;
    QRectF ViewPaperRect(QSize) const;
    void ZoomAt(double,QPointF);
    InstructionSheet* sheet_=nullptr;
    std::vector<InstructionPage>* scenes_=nullptr;
    QString tool_="move";
    InstructionSheetItem pending_;
    int selected_=-1;
    bool dragging_=false,resizing_=false;
    int resizeHandle_=-1;
    QPlainTextEdit* textEditor_=nullptr;
    QUuid textItemId_;
    unsigned textEditEpoch_=0;
    void CancelTextEdit();
    int HandleAt(QPointF) const;
    bool placementHover_=false;
    QPointF placementAt_;
    QPointF last_;
    QRectF dragOriginal_;
    QHash<QString,QImage> sceneImageCache_;
    std::size_t sceneRenders_=0;
    QUuid viewSheetId_;
    double zoom_=1;
    QPointF pan_,panLast_;
    bool panning_=false,keepAspect_=false;
};
QJsonArray EncodeInstructionSheets(const std::vector<InstructionSheet>&);
bool DecodeInstructionSheets(const QJsonArray&,const std::vector<InstructionPage>&,std::vector<InstructionSheet>&);
