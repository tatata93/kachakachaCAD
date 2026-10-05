#pragma once
#include "V2InstructionScene.h"
#include <QWidget>
#include <QJsonObject>
#include <QPointer>
#include <vector>
#include <string_view>
#include <functional>
class V2MainWindow;
class QComboBox;
class QLineEdit;
class QLabel;
class QListWidget;
class QSpinBox;
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
    bool ExportImage(const QString&,QSize);
    void Capture();
    bool ReadModel(const QString&);
    void AcceptParts();
    void Command(std::string_view);
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    void NewPage(bool copy);
    void ShowPage(int);
    void ShowSettings();
    void ShowModelPicker();
    bool Collect(V2MainWindow&);
    void Remember();
    void Undo(bool redo);
    bool CheckSaved();
    void FileAction(std::string_view);
    V2MainWindow& window_;
    V2InstructionScene* view_=nullptr;
    QComboBox* pages_=nullptr;
    QLineEdit* title_=nullptr;
    QPointer<QLabel> hint_;
    QPointer<QWidget> settings_;
    QPointer<QListWidget> partList_;
    std::vector<InstructionPage> scenes_;
    InstructionPage source_;
    int current_=0;
    bool loading_=false,dirty_=false,choosing_=false;
    QString path_,activeTool_="move";
    std::function<QString(bool)> pathChooser_;
    QJsonObject before_;
    std::vector<QJsonObject> undo_,redo_;
};
