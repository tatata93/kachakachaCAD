#pragma once
#include "V2InstructionScene.h"
#include <QHash>
#include <QIcon>
#include <QWidget>
class QListWidget;
class QPushButton;
class QTimer;

// The list holds UUIDs, never document indices across an Undo or a file replacement.
class V2InstructionSceneBrowser final:public QWidget {
public:
    explicit V2InstructionSceneBrowser(QWidget* parent=nullptr);
    void Refresh(std::vector<InstructionPage>*,int selected,bool paper,bool choosing);
    void Invalidate(const QUuid& id={});
    std::function<void(int)> chooseScene,editScene,placeScene;
    std::function<void()> addScene,removeScene;
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    int SelectedScene() const;
    void UpdateThumbnails();
    QListWidget* list_=nullptr;
    QPushButton* edit_=nullptr;
    QPushButton* place_=nullptr;
    QPushButton* add_=nullptr;
    QPushButton* remove_=nullptr;
    QTimer* timer_=nullptr;
    std::vector<InstructionPage>* scenes_=nullptr;
    QHash<QUuid,QIcon> icons_;
};
