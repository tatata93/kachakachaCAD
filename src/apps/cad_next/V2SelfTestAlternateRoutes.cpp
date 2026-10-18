#include <QEvent>
#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QShortcutEvent>
#include <array>

namespace kachakacha::v2::selftest {
bool AlternateShortcutRoutes(V2MainWindow& w)
{
    w.SetMode(app::UiMode::Drawing);
    const std::array<std::pair<const char*,modeling::DrawingTool>,4> keys={{{"L",modeling::DrawingTool::Line},{"C",modeling::DrawingTool::Circle},{"V",modeling::DrawingTool::Select},{"M",modeling::DrawingTool::Measure}}};
    for(const auto& [key,tool]:keys){QAction* shortcut=nullptr;int count=0;
        for(auto* action:w.findChildren<QAction*>())if(action->isEnabled()&&action->isVisible()&&action->shortcut()==QKeySequence(key)){shortcut=action;++count;}
        if(!Explain((std::string("one registered shortcut: ")+key).c_str(),count==1))return false;
        w.SelectTool(modeling::DrawingTool::Select);QShortcutEvent event(QKeySequence(key),0,false);QApplication::sendEvent(shortcut,&event);
        if(!Explain((std::string("resolved Qt shortcut enters tool: ")+key).c_str(),w.Session().CurrentTool()==tool))return false;
    }
    w.ActivateCommand("draw.line");w.Viewport().ClickAt({160,180});
    const auto points=w.Session().PlacedPointCount();w.ActivateCommand("view.number_settings");
    QShortcutEvent repeat(QKeySequence("L"),0,false);QApplication::sendEvent(w.ActionFor("draw.line"),&repeat);
    return Explain("same tool shortcut restores pane and preserves unfinished points",points>0&&w.Session().PlacedPointCount()==points&&w.ShelfShown(app::Shelf::Drawing));
}
bool AlternateViewDuringInput(V2MainWindow& w)
{
    w.SetMode(app::UiMode::Drawing);
    for(const auto end:{geometry::Vector3{20,0,0},geometry::Vector3{20,20,0}}){
        w.SelectTool(modeling::DrawingTool::Line);
        const auto start=w.Viewport().Mapping().Project(end.y==0?geometry::Vector3{0,0,0}:geometry::Vector3{20,0,0});
        const auto finish=w.Viewport().Mapping().Project(end);if(!start||!finish)return false;
        w.Viewport().ClickAt({start->x,start->y});w.Viewport().ClickAt({finish->x,finish->y});w.SelectTool(modeling::DrawingTool::Select);
    }
    w.Viewport().SetSelection({});w.ActivateCommand("wire.join");
    const auto emptyLabel=w.PendingCommandLabel();w.ActivateCommand("view.fit_selection");
    if(!Explain("view without targets gives a reason and keeps the editing tool",!emptyLabel.isEmpty()&&w.PendingCommandLabel()==emptyLabel&&w.OperationHost().TemporaryPage()))return false;
    auto point=w.Viewport().Mapping().Project({10,0,0});if(!point)return false;w.Viewport().SelectAt({point->x,point->y},Qt::NoModifier);
    const auto label=w.PendingCommandLabel();const auto revision=w.Session().GetDocument().Revision();
    for(const auto* view:{"view.fit_all","view.fit_selection"}){
        w.ActivateCommand(view);
        if(!Explain((std::string("view operation keeps pending input: ")+view).c_str(),!label.isEmpty()&&w.PendingCommandLabel()==label&&w.Viewport().Selection().entityIds.size()==1&&w.OperationHost().TemporaryPage()))return false;
    }
    point=w.Viewport().Mapping().Project({20,10,0});if(!point)return false;w.Viewport().SelectAt({point->x,point->y},Qt::NoModifier);
    if(!Explain("can collect the second target after zooming",w.Viewport().Selection().entityIds.size()==2))return false;
    w.ConfirmPendingCommand();
    return Explain("continued tool commits after view changes",w.Session().GetDocument().Revision()>revision&&w.PendingCommandLabel().isEmpty());
}
}
