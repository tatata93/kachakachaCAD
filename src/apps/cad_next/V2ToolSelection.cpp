#include "V2ToolSelection.h"
#include "V2Viewport.h"
#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"
#include "kachakacha/base/Version.h"
#include "kachakacha/app/Selection.h"
#include <QFileInfo>
#include <QFontMetrics>
#include <QObject>
#include <QString>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>
#include <QVariant>
#include <QWidget>

namespace V2ToolSelection {
namespace {
constexpr auto property="toolEditSelectionActive";
class State final : public QObject {
public:
    explicit State(V2Viewport& view):QObject(&view){setObjectName("toolEditSelectionState");}
    kachakacha::v2::app::SelectionSet previous;
    kachakacha::v2::base::DocumentId document;
    QToolButton* button=nullptr;
};
State* Of(const V2Viewport& view){return static_cast<State*>(view.findChild<QObject*>("toolEditSelectionState"));}
void Show(V2Viewport& view,bool on){
    view.setProperty(property,on);
    if(auto* state=Of(view);state&&state->button){
        const QSignalBlocker block(state->button);state->button->setChecked(on);
        state->button->setText(on?QStringLiteral("ツールに戻る"):QStringLiteral("選択・削除"));
    }
    view.CancelBoxSelect();view.RefreshCursorShape();view.update();
}
}
bool Active(const V2Viewport& view){return view.property(property).toBool();}
bool HasTool(const V2Viewport& view){
    auto& window=static_cast<V2MainWindow&>(*view.window());
    return window.ToolWantsConfirmKeys()||window.OperationHost().HasTemporaryPage();
}
bool Key(V2Viewport& view,int key,bool typing){
    if(!typing&&(key==Qt::Key_Escape||key==Qt::Key_Return||key==Qt::Key_Enter)){End(view);return true;}
    return false;
}
void Install(V2Viewport& view,QToolBar& bar){
    auto* state=new State(view);state->button=new QToolButton(&bar);
    auto* button=state->button;button->setObjectName("toolEditSelection");button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);button->setText(QStringLiteral("選択・削除"));
    button->setMinimumWidth(button->fontMetrics().horizontalAdvance(QStringLiteral("ツールに戻る"))+16);
    button->setToolTip(QStringLiteral("道具の入力と設定を保って、任意の線・面・部品を選択します。Deleteで削除、Ctrlで選び足し、もう一度押すかEscで道具へ戻ります。"));
    bar.addWidget(button);QObject::connect(button,&QToolButton::toggled,&view,[&view](bool on){if(on)Begin(view);else End(view);});
}
void Begin(V2Viewport& view){
    if(Active(view))return;auto* state=Of(view);if(!state)return;
    auto& window=static_cast<V2MainWindow&>(*view.window());state->previous=view.Selection();
    state->document=window.Session().GetDocument().Snapshot().id;Show(view,true);
    view.SetSelection({});view.PruneSelection();view.setFocus();
}
void End(V2Viewport& view){
    if(!Active(view))return;
    if(auto* state=Of(view)){
        const auto& snapshot=static_cast<V2MainWindow&>(*view.window()).Session().GetDocument().Snapshot();
        // Restore while the edit-selection gate still suppresses role handlers.
        view.SetSelection(state->document==snapshot.id?kachakacha::v2::app::PruneSelection(state->previous,snapshot):kachakacha::v2::app::SelectionSet{});
        state->previous={};
    }
    view.PruneSelection();Show(view,false);view.setFocus();
}
void Reset(V2Viewport& view){if(auto* state=Of(view))state->previous={};Show(view,false);}
void Caption(QWidget& window,const QString& path){
    const auto name=path.isEmpty()?QStringLiteral("無題"):QFileInfo(path).fileName();
    window.setWindowFilePath(path);
    window.setWindowTitle(QStringLiteral("kachakachaCAD %1 — %2").arg(
        QString::fromStdString(kachakacha::v2::base::ProductVersionString()),name));
}
}

bool V2MainWindow::RunQuickCommand(std::string_view id) {
    // Permanent toolbar actions remain available while editing a tool selection.
    if(id=="grid.edit"){ShowGridDock();return true;}
    if(V2ToolSelection::Active(*viewport_)){
        if(id=="edit.delete"){DeleteSelected();return true;}
        if(id=="edit.undo"||id=="edit.redo"){RunHistoryCommand(id=="edit.undo");return true;}
        if(id=="file.save"||id=="file.save_as"){RunFileCommand(id);return true;}
        if(!id.starts_with("view.")&&id!="tools.search")V2ToolSelection::End(*viewport_);
    }
    return false;
}
