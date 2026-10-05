#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QPointer>
#include <vector>
#include <string_view>
#include <functional>
class V2MainWindow;
class QGraphicsScene;
class QGraphicsView;
class QGraphicsPathItem;
class QComboBox;
class QLineEdit;
class QLabel;
class V2InstructionMode final:public QWidget {
public:
    static void Sync(V2MainWindow&);
    static bool Run(V2MainWindow&,std::string_view);
    static void SetPathChooser(V2MainWindow&,std::function<QString(bool)>);
    explicit V2InstructionMode(V2MainWindow&);
    QJsonObject Snapshot() const;
    bool Restore(const QJsonObject&);
    bool Save(const QString&);
    bool Load(const QString&);
    bool ExportPdf(const QString&);
    void Capture();
    void Command(std::string_view);
protected:
    bool eventFilter(QObject*,QEvent*) override;
    void resizeEvent(QResizeEvent*) override;
private:
    void NewPage(bool copy);
    void ShowPage(int);
    void ShowSettings();
    void Remember();
    void Undo(bool redo);
    void CancelArrow();
    void DrawArrow(const QPointF&);
    bool CheckSaved();
    void FileAction(std::string_view);
    V2MainWindow& window_;
    QGraphicsView* view_=nullptr;
    QComboBox* pages_=nullptr;
    QLineEdit* title_=nullptr;
    QPointer<QLabel> hint_;
    QPointer<QWidget> settings_;
    std::vector<QGraphicsScene*> scenes_;
    std::vector<QString> titles_;
    int current_=0;
    bool loading_=false,arrowTool_=false,dirty_=false;
    QString path_;
    std::function<QString(bool)> pathChooser_;
    QGraphicsPathItem* arrow_=nullptr;
    QPointF arrowStart_;
    QJsonObject before_;
    std::vector<QJsonObject> undo_,redo_;
};
