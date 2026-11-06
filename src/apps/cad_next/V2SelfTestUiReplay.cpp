// Optional, reproducible UI scenarios. Geometry is made by visible controls,
// never by constructing features or passing shapes into the document.
#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2EntityTree.h"
#include "V2DocumentTiming.h"
#include "kachakacha/app/Ribbon.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/modeling/MeshPick.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QChar>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFormLayout>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointF>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QScrollArea>
#include <QScopeGuard>
#include <QStyle>
#include <QString>
#include <QStyleOptionViewItem>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidgetItem>
#include <QWidget>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <optional>
#include <utility>

namespace kachakacha::v2::selftest {
namespace {
using base::EntityId;
QString Utf8(std::string_view s){return QString::fromUtf8(s.data(),int(s.size()));}

void Reveal(QWidget& widget)
{
    for(auto* p=widget.parentWidget();p;p=p->parentWidget())
        if(auto* scroll=dynamic_cast<QScrollArea*>(p))scroll->ensureWidgetVisible(&widget);
    QApplication::processEvents();
}

bool Mouse(QWidget& widget,QPointF p,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
{
    if(!widget.isVisible()||!widget.isEnabled()||!widget.rect().contains(p.toPoint()))return false;
    for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
        QMouseEvent event(type,p,QPointF(widget.mapToGlobal(p.toPoint())),Qt::LeftButton,
            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,modifiers);
        QApplication::sendEvent(&widget,&event);
    }
    QApplication::processEvents();return true;
}

bool Key(QWidget& target,const QString& sequence)
{
    const QKeySequence keys(sequence);if(keys.isEmpty()||keys.count()!=1)return false;
    const auto chord=keys[0];
    QKeyEvent press(QEvent::KeyPress,chord.key(),chord.keyboardModifiers());
    QKeyEvent release(QEvent::KeyRelease,chord.key(),chord.keyboardModifiers());
    QApplication::sendEvent(&target,&press);QApplication::sendEvent(&target,&release);
    QApplication::processEvents();return true;
}

bool Text(QLineEdit& input,const QString& value,bool commit=true,bool exact=true)
{
    Reveal(input);if(!input.isVisible()||!input.isEnabled()||input.isReadOnly())return false;
    input.setFocus();QFocusEvent focus(QEvent::FocusIn);QApplication::sendEvent(&input,&focus);
    input.selectAll();
    for(const auto ch:value){
        const auto upper=ch.toUpper();
        QKeyEvent key(QEvent::KeyPress,upper.unicode(),Qt::NoModifier,QString(ch));
        QApplication::sendEvent(&input,&key);
    }
    if(exact&&input.text()!=value)return false;
    if(commit){Key(input,"Return");Key(input,"Tab");}
    QApplication::processEvents();return true;
}

bool Number(QWidget* field,double value)
{
    if(!field||!std::isfinite(value))return false;
    auto* spin=dynamic_cast<QAbstractSpinBox*>(field);
    if(!spin)return false;
    auto* input=field->findChild<QLineEdit*>();
    if(!input||!Text(*input,QString::number(value,'g',12),false,false))return false;
    // Enter is the normal tool confirmation shortcut. Leave a numeric field
    // with Tab so a multi-field tool can be configured before confirmation.
    if(!Key(*input,"Tab"))return false;
    // The embedded editor can receive our synthetic focus while its spin box
    // is not the platform focus widget (also with offscreen Qt). Complete the
    // departure at the owning control, as a real click on the next field does.
    // Qt validates/commits the text; never assign the value or skip comparison.
    QFocusEvent leave(QEvent::FocusOut,Qt::TabFocusReason);
    QApplication::sendEvent(spin,&leave);
    QApplication::processEvents();
    bool ok=false;const auto actual=field->property("value").toDouble(&ok);
    if(!ok||std::abs(actual-value)>1e-7*std::max(1.,std::abs(value)))
        std::cerr<<"UI numeric wanted="<<value<<" actual="<<actual<<" text="
                 <<input->text().toStdString()<<std::endl;
    return ok&&std::abs(actual-value)<=1e-7*std::max(1.,std::abs(value));
}

bool Mode(V2MainWindow& w,app::UiMode mode)
{
    if(w.Mode()==mode)return true;
    auto* bar=w.findChild<QToolBar*>("modeBar");if(!bar)return false;
    for(auto* b:bar->findChildren<QToolButton*>())if(b->text()==Utf8(app::UiModeNameJa(mode)))
        return Mouse(*b,b->rect().center())&&w.Mode()==mode;
    return false;
}

bool Tool(V2MainWindow& w,const QJsonObject& step)
{
    V2DocumentTiming timing("replay-tool");
    timing.Mark("begin");
    const auto command=step["command"].toString();const auto label=step["label"].toString();
    for(const auto mode:app::AllUiModes())for(const auto& category:app::RibbonCategoriesFor(mode))
        for(const auto& tool:category.tools)if((!command.isEmpty()||!label.isEmpty())
            &&(command.isEmpty()||Utf8(tool.commandId)==command)
            &&(label.isEmpty()||Utf8(tool.labelJa)==label)){
            if(!Mode(w,mode))return false;
            timing.Mark("mode");
            QToolButton* categoryButton=nullptr;
            for(auto* b:w.Ribbon().findChildren<QToolButton*>("ribbonCategory"))
                if(b->text()==Utf8(category.labelJa))categoryButton=b;
            if(!categoryButton||!Mouse(*categoryButton,categoryButton->rect().center()))return false;
            timing.Mark("category");
            for(auto* b:w.Ribbon().findChildren<QToolButton*>("ribbonTool"))
                if(b->text()==Utf8(tool.labelJa)){
                    const bool result=Mouse(*b,b->rect().center());
                    timing.Mark("tool");return result;
                }
            return false;
        }
    return false;
}

// Read positions from displayed edge polylines, then send the same mouse input
// as a person. Never construct a selection reference or change model geometry.
std::optional<geometry::Vector3> NativeEdgePoint(V2MainWindow& w,EntityId id,int edge,double parameter)
{
    if(edge<0||!std::isfinite(parameter)||parameter<0||parameter>1)return std::nullopt;
    for(const auto& shape:w.Viewport().ShapeViews())if(shape.entityId==id){
        if(static_cast<std::size_t>(edge)>=shape.mesh.edges.size())return std::nullopt;
        const auto& points=shape.mesh.edges[static_cast<std::size_t>(edge)];
        if(points.size()<2)return std::nullopt;
        double length=0;for(std::size_t i=1;i<points.size();++i)length+=geometry::Distance(points[i-1],points[i]);
        if(length<=1e-12)return std::nullopt;
        double remaining=length*parameter;
        for(std::size_t i=1;i<points.size();++i){
            const double span=geometry::Distance(points[i-1],points[i]);
            if(span>0&&remaining<=span)return points[i-1]+(points[i]-points[i-1])*(remaining/span);
            remaining-=span;
        }
        return points.back();
    }
    return std::nullopt;
}

QJsonObject Read(const QString& path)
{
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool Write(const QString& path,const QJsonObject& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());QFile file(path);
    return file.open(QIODevice::WriteOnly)&&file.write(QJsonDocument(data).toJson())>0;
}

struct Replay {
    V2MainWindow& w;
    QString root;
    QJsonObject aliases;
    QJsonArray trace;
    int index=0;
    std::vector<EntityId> markedEntities;

    QWidget* Field(const QJsonObject& step) const
    {
        QWidget* parent=&w;
        if(step.contains("within"))parent=w.findChild<QWidget*>(step["within"].toString());
        if(!parent)return nullptr;
        std::vector<QWidget*> matches;
        for(auto* field:parent->findChildren<QWidget*>()){
            if(!field->isVisible())continue;
            if(step.contains("widget")&&field->objectName()!=step["widget"].toString())continue;
            if(step.contains("fieldLabel")){
                bool labelled=false;
                for(auto* form:parent->findChildren<QFormLayout*>())
                    if(auto* label=dynamic_cast<QLabel*>(form->labelForField(field)))
                        labelled=labelled||label->text()==step["fieldLabel"].toString();
                if(!labelled)continue;
            }
            if(!step.contains("widget")&&!step.contains("fieldLabel"))continue;
            matches.push_back(field);
        }
        return matches.size()==1?matches.front():nullptr;
    }

    QString Resolve(QString value) const
    {
        if(value.startsWith('$'))return aliases[value.mid(1)].toString();
        return value;
    }

    QString Path(const QJsonObject& step) const
    {
        const auto name=step["path"].toString();
        return QFileInfo(name).isAbsolute()?name:QDir(root).absoluteFilePath(name);
    }

    bool Visibility(const QJsonObject& step)
    {
        auto* tree=w.EntityTree();if(!tree)return false;
        const auto id=Resolve(step["id"].toString());
        const auto key=step.contains("key")?step["key"].toString():
            (step["group"].toBool()?"group:":"entity:")+id;
        auto* row=tree->FindKey(key);if(!row)return false;
        for(auto* p=row->parent();p;p=p->parent())p->setExpanded(true);
        QApplication::processEvents();tree->scrollToItem(row);QApplication::processEvents();
        const auto wanted=step["value"].toBool()?Qt::Checked:Qt::Unchecked;
        if(row->checkState(0)==wanted)return true;
        QStyleOptionViewItem option;option.initFrom(tree);option.rect=tree->visualItemRect(row);
        option.features|=QStyleOptionViewItem::HasCheckIndicator;option.checkState=row->checkState(0);
        const auto hit=tree->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,&option,tree).center();
        if(!Mouse(*tree->viewport(),hit))return false;
        row=tree->FindKey(key);
        return row&&row->checkState(0)==wanted;
    }

    bool Ray(const QJsonObject& step) const
    {
        const auto vector=[](const QJsonArray& a){return geometry::Vector3{a[0].toDouble(),a[1].toDouble(),a[2].toDouble()};};
        const auto origin=vector(step["origin"].toArray()),direction=vector(step["direction"].toArray());
        const auto parsed=EntityId::Parse(Resolve(step["id"].toString()).toStdString());if(!parsed)return false;
        for(const auto& shape:w.Viewport().ShapeViews())if(shape.entityId==*parsed){
            const bool hit=std::any_of(shape.mesh.triangles.begin(),shape.mesh.triangles.end(),[&](const auto& t){
                return modeling::RayHitsTriangle(origin,direction,t);});
            return hit==step["hit"].toBool();
        }
        return false;
    }

    bool Point(const QJsonObject& step)
    {
        const auto id=EntityId::Parse(Resolve(step["id"].toString()).toStdString());if(!id)return false;
        const double parameter=step["parameter"].toDouble();
        if(!std::isfinite(parameter)||parameter<0||parameter>1)return false;
        std::optional<geometry::Vector3> position;
        if(step.contains("native_edge"))position=NativeEdgePoint(w,*id,step["native_edge"].toInt(-1),parameter);
        else {
            int at=step["segment"].toInt();
            for(const auto& curve:w.Session().Scene().curves)if(curve.entityId==*id&&at--==0){
                position=curve.segment.Evaluate(parameter);break;
            }
        }
        if(!position)return false;
        const auto p=w.Viewport().Mapping().Project(*position);if(!p)return false;
        const QPointF pixel(p->x,p->y);
        const auto modifiers=step["add"].toBool()?Qt::ControlModifier:Qt::NoModifier;
        QMouseEvent move(QEvent::MouseMove,pixel,QPointF(w.Viewport().mapToGlobal(pixel.toPoint())),
            Qt::NoButton,Qt::NoButton,modifiers);
        QApplication::sendEvent(&w.Viewport(),&move);QApplication::processEvents();
        const bool sent=Mouse(w.Viewport(),pixel,modifiers);
        QString picked;for(const auto& selected:w.Viewport().Selection().entityIds)picked+=QString::fromStdString(selected.ToString())+' ';
        Note(QStringLiteral("replay point %1 at %2,%3 add=%4 selected=%5")
            .arg(Resolve(step["id"].toString())).arg(pixel.x()).arg(pixel.y()).arg(step["add"].toBool()).arg(picked).toUtf8().constData());
        return sent;
    }

    bool Select(const QJsonObject& step)
    {
        const auto parsed=EntityId::Parse(Resolve(step["id"].toString()).toStdString());
        auto* tree=w.EntityTree();if(!parsed||!tree)return false;
        // Platform mouse dispatch focuses the item view before its press. Our
        // direct Qt events must do this before scrolling, otherwise focus can
        // reveal the previous current row after we have located the new row.
        tree->setFocus(Qt::MouseFocusReason);QApplication::processEvents();
        const auto findRow=[&]{auto* row=w.ItemOfEntity(*parsed);
            return row?row:tree->FindKey("group:"+Resolve(step["id"].toString()));};
        auto* row=findRow();bool group=false;
        if(row)group=V2EntityTree::KeyOf(row).startsWith("group:");
        if(!row)return false;
        for(auto* p=row->parent();p;p=p->parent())p->setExpanded(true);
        QApplication::processEvents();
        tree->doItemsLayout();
        tree->scrollToItem(row,QAbstractItemView::PositionAtCenter);QApplication::processEvents();
        row=findRow();if(!row)return false;
        // Long labels can make the logical row wider than the viewport. A
        // person clicks its visible portion, rather than the offscreen centre.
        const auto point=tree->visualItemRect(row).intersected(tree->viewport()->rect()).center();
        if(tree->itemAt(point)!=row)return false;
        std::cout<<"UI tree "<<Resolve(step["id"].toString()).toStdString()<<" row="<<row->text(0).toStdString()
            <<" pixel="<<point.x()<<","<<point.y()<<" viewport="<<tree->viewport()->width()<<","<<tree->viewport()->height()<<std::endl;
        if(!Mouse(*tree->viewport(),point,step["add"].toBool()?Qt::ControlModifier:Qt::NoModifier))return false;
        if(step.contains("rename")){
            tree->setFocus();if(!Key(*tree,"F2"))return false;
            auto* editor=tree->findChild<QLineEdit*>();
            return editor&&Text(*editor,step["rename"].toString());
        }
        if(group)return tree->FindKey("group:"+Resolve(step["id"].toString()))->isSelected();
        const auto& selected=w.Viewport().Selection().entityIds;
        return std::find(selected.begin(),selected.end(),*parsed)!=selected.end();
    }

    bool Combo(const QJsonObject& step)
    {
        QComboBox* field=nullptr;
        if(step.contains("widget")||step.contains("fieldLabel"))field=dynamic_cast<QComboBox*>(Field(step));
        else for(auto* item:w.findChildren<QComboBox*>())
            if(item->isVisible()&&item->findText(step["text"].toString())>=0){field=item;break;}
        if(!field)return false;Reveal(*field);if(!field->isVisible()||!field->isEnabled())return false;
        const int wanted=step.contains("index")?step["index"].toInt(-1):field->findText(step["text"].toString());
        if(wanted<0||wanted>=field->count())return false;
        field->setFocus();
        for(int guard=0;field->currentIndex()!=wanted&&guard<=field->count();++guard)
            if(!Key(*field,field->currentIndex()<wanted?"Down":"Up"))return false;
        QApplication::processEvents();return field->currentIndex()==wanted;
    }

    bool Click(const QJsonObject& step)
    {
        auto* field=Field(step);
        auto* parent=step.contains("within")?w.findChild<QWidget*>(step["within"].toString()):static_cast<QWidget*>(&w);
        if(!field&&parent&&step.contains("text"))for(auto* b:parent->findChildren<QAbstractButton*>())
            if(b->isVisible()&&b->text()==step["text"].toString()){field=b;break;}
        if(!field)return false;
        Reveal(*field);
        if(step.contains("dialogText")){
            const auto value=step["dialogText"].toString();
            QTimer::singleShot(0,&w,[value]{
                if(auto* dialog=QApplication::activeModalWidget())
                    if(auto* input=dialog->findChild<QLineEdit*>())Text(*input,value);
            });
        }
        return Mouse(*field,field->rect().center());
    }

    bool Act(const QJsonObject& step)
    {
        const auto op=step["op"].toString();
        if(op=="tool")return Tool(w,step);
        if(op=="select")return Select(step);
        if(op=="combo")return Combo(step);
        if(op=="click")return Click(step);
        if(op=="visibility")return Visibility(step);
        if(op=="ray")return Ray(step);
        if(op=="point")return Point(step);
        if(op=="number")return Number(Field(step),step["value"].toDouble());
        if(op=="text"){
            auto* field=dynamic_cast<QLineEdit*>(Field(step));
            return field&&Text(*field,step["value"].toString(),step["commit"].toBool(true));
        }
        if(op=="tab"){
            auto* tabs=dynamic_cast<QTabWidget*>(Field(step));
            const int wanted=step["index"].toInt(-1);
            return tabs&&wanted>=0&&wanted<tabs->count()&&tabs->isVisible()
                &&Mouse(*tabs->tabBar(),tabs->tabBar()->tabRect(wanted).center())&&tabs->currentIndex()==wanted;
        }
        if(op=="check"){
            auto* button=dynamic_cast<QAbstractButton*>(Field(step));
            if(!button||!button->isCheckable()||!button->isVisible())return false;
            const bool wanted=step["value"].toBool();
            if(button->isChecked()!=wanted&&!Mouse(*button,button->rect().center()))return false;
            return button->isChecked()==wanted;
        }
        if(op=="key"){
            w.Viewport().setFocus();return Key(w.Viewport(),step["key"].toString());
        }
        if(op=="remember"){
            const auto snapshot=w.Session().GetDocument().Snapshot();
            if(step["group"].toBool()){
                if(snapshot.groups.empty())return false;
                aliases[step["as"].toString()]=QString::fromStdString(snapshot.groups.back().id.ToString());return true;
            }
            const auto kind=step["kind"].toInt(-1);
            int offset=step["offset"].toInt();
            for(auto it=snapshot.entities.rbegin();it!=snapshot.entities.rend();++it)
                if(kind<0||int(it->kind)==kind){
                    if(offset-->0)continue;
                    if(step["new"].toBool()&&std::find(markedEntities.begin(),markedEntities.end(),it->id)!=markedEntities.end())return false;
                    aliases[step["as"].toString()]=QString::fromStdString(it->id.ToString());return true;
                }
            return false;
        }
        if(op=="mark"){
            markedEntities.clear();
            for(const auto& e:w.Session().GetDocument().Snapshot().entities)markedEntities.push_back(e.id);
            return true;
        }
        if(op=="snapshot"){
            QDir().mkpath(QFileInfo(Path(step)).absolutePath());QApplication::processEvents();
            return w.grab().save(Path(step));
        }
        if(op=="fit"){
            w.Viewport().SetViewDirection(ViewDirection::Isometric);w.Viewport().FitToDocument();
            QApplication::processEvents();return true;
        }
        if(op=="save"){
            const auto output=Path(step);
            if(QFileInfo(output).absoluteFilePath()==QFileInfo(w.DocumentPath()).absoluteFilePath())return false;
            QDir().mkpath(QFileInfo(output).absolutePath());w.SetPathChooser([output](bool){return output;});
            w.RunCommand("file.save_as");w.SetPathChooser([](bool){return QString();});
            return w.DocumentPath()==output&&QFileInfo::exists(output);
        }
        if(op=="count")return CountOfKind(w,static_cast<domain::EntityKind>(step["kind"].toInt()))==step["value"].toInt();
        if(op=="inspect")return Inspect(Path(step));
        return false;
    }

    bool Inspect(const QString& path)
    {
        QJsonArray entities;
        for(const auto& e:w.Session().GetDocument().Snapshot().entities)
            entities.append(QJsonObject{{"id",QString::fromStdString(e.id.ToString())},{"name",QString::fromStdString(e.displayName)},
                {"kind",int(e.kind)},{"visibility",int(e.visibility)}});
        QJsonArray fields;
        for(auto* widget:w.findChildren<QWidget*>())if(widget->isVisible()&&!widget->objectName().isEmpty()){
            QJsonObject row{{"widget",widget->objectName()},{"type",widget->metaObject()->className()},{"enabled",widget->isEnabled()}};
            if(auto* line=dynamic_cast<QLineEdit*>(widget))row["value"]=line->text();
            if(auto* label=dynamic_cast<QLabel*>(widget))row["text"]=label->text();
            if(auto* log=dynamic_cast<QPlainTextEdit*>(widget))row["text"]=log->toPlainText();
            if(auto* log=dynamic_cast<QTextEdit*>(widget))row["text"]=log->toPlainText();
            if(auto* box=dynamic_cast<QComboBox*>(widget)){
                QJsonArray options;for(int i=0;i<box->count();++i)options.append(box->itemText(i));
                row["options"]=options;row["index"]=box->currentIndex();
            }
            if(auto* button=dynamic_cast<QAbstractButton*>(widget))row["text"]=button->text();
            fields.append(row);
        }
        QJsonArray groups;
        for(const auto& group:w.Session().GetDocument().Snapshot().groups)
            groups.append(QJsonObject{{"id",QString::fromStdString(group.id.ToString())},{"name",QString::fromStdString(group.displayName)}});
        QJsonArray notices;
        if(auto* dock=w.findChild<QWidget*>("diagnosticDock"))
            for(auto* list:dock->findChildren<QListWidget*>())
                for(int i=0;i<list->count();++i)notices.append(list->item(i)->text());
        return Write(path,{{"entities",entities},{"groups",groups},{"fields",fields},{"notices",notices},
            {"aliases",aliases},{"status",w.StatusText()}});
    }

    bool Run(const QJsonObject& phase)
    {
        if(phase["name"].toString().isEmpty()||!phase["steps"].isArray()||phase["steps"].toArray().isEmpty())return false;
        // RunOneCase installs automatic answers for older isolated tests. A
        // human-operation replay must use the product's real settings instead.
        w.SetPathChooser({});w.SetExtrudeChooser({});w.SetWorkPlaneChooser({});
        w.SetAssemblyChooser({});w.SetGuideChoiceChooser({});
        // Exercise the real Qt file chooser. The optional event replay cannot
        // send its widget events into a Windows-native file picker.
        const bool nativeDialogsDisabled=QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
        const auto restoreDialogs=qScopeGuard([&]{QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,nativeDialogsDisabled);});
        w.resize(1600,1000);QApplication::processEvents();
        const auto input=phase["input"].toString();
        if(!input.isEmpty()&&!w.OpenDocumentFile(QFileInfo(input).isAbsolute()?input:QDir(root).absoluteFilePath(input))){
            Inspect(QDir(root).filePath("replay-failure.json"));
            w.grab().save(QDir(root).filePath("replay-failure.png"));return false;
        }
        for(const auto& value:phase["steps"].toArray()){
            QElapsedTimer elapsed;elapsed.start();
            const auto step=value.toObject();const bool ok=Act(step);
            trace.append(QJsonObject{{"step",index},{"input",step},{"ok",ok},{"status",w.StatusText()},
                {"elapsed_ms",elapsed.nsecsElapsed()/1000000.0}});
            Write(QDir(root).filePath("replay-trace.json"),{{"steps",trace},{"aliases",aliases}});
            if(!ok){Inspect(QDir(root).filePath("replay-failure.json"));w.grab().save(QDir(root).filePath("replay-failure.png"));
                std::cerr<<"UI replay failed at "<<index<<" "<<QJsonDocument(step).toJson(QJsonDocument::Compact).constData()<<std::endl;return false;}
            ++index;
        }
        return Write(QDir(root).filePath("aliases.json"),aliases);
    }
};

bool Smoke(V2MainWindow& w)
{
    Replay replay{w,QString(),{}, {},0};
    const QJsonObject tool{{"op","tool"},{"command","draw.line"}};
    if(!replay.Act(tool)||!replay.Act({{"op","tab"},{"widget","drawingInputModes"},{"index",1}}))return false;
    if(!replay.Act({{"op","combo"},{"widget","drawingCoordinateKind"},{"index",0}}))return false;
    // Consecutive edits must leave Qt's committed value equal to the text and
    // must not run the tool's Enter action while configuring its fields.
    for(double value:{.025,-.125,1.0})
        if(!replay.Act({{"op","number"},{"widget","drawingCoordinateP1_0"},{"value",value}})
            ||CountOfKind(w,domain::EntityKind::Wire)!=0)return false;
    for(const auto& field:std::vector<std::pair<QString,double>>{{"drawingCoordinateP1_0",1},{"drawingCoordinateP1_1",2},
        {"drawingCoordinateP2_0",5},{"drawingCoordinateP2_1",7}})
        if(!replay.Act({{"op","number"},{"widget",field.first},{"value",field.second}}))return false;
    if(!replay.Act({{"op","click"},{"widget","drawingCreateCoordinates"}})||CountOfKind(w,domain::EntityKind::Wire)!=1)return false;
    const QString longName(160,QChar('W'));
    if(!replay.Act({{"op","remember"},{"kind",2},{"as","longLabel"}})
        ||!replay.Act({{"op","select"},{"id","$longLabel"},{"rename",longName}})
        ||!replay.Act({{"op","select"},{"id","$longLabel"}}))return false;
    if(!replay.Act(tool))return false;
    w.resize(1600,1000);QApplication::processEvents();
    for(const auto& field:std::vector<std::pair<QString,double>>{{"drawingCoordinateP1_0",-40},{"drawingCoordinateP1_1",30},
        {"drawingCoordinateP2_0",-10},{"drawingCoordinateP2_1",40}})
        if(!replay.Act({{"op","number"},{"widget",field.first},{"value",field.second}}))return false;
    if(!replay.Act({{"op","click"},{"widget","drawingCreateCoordinates"}})
        ||!Explain("second coordinate line created",CountOfKind(w,domain::EntityKind::Wire)==2)
        ||!replay.Act({{"op","remember"},{"kind",2},{"as","secondLine"}})
        ||!replay.Act({{"op","click"},{"within","modeBar"},{"text",QStringLiteral("選択")}})
        ||!replay.Act({{"op","fit"}})
        ||!replay.Act({{"op","point"},{"id","$longLabel"},{"parameter",.5}})
        ||!replay.Act({{"op","point"},{"id","$secondLine"},{"parameter",.5},{"add",true}}))return false;
    Note(QStringLiteral("replay Ctrl-click: %1 entities, %2 ordered elements")
        .arg(w.Viewport().Selection().entityIds.size()).arg(w.Viewport().Selection().ordered.size()).toUtf8().constData());
    if(w.Viewport().Selection().entityIds.size()!=2){
        replay.Inspect("replay-smoke-selection.json");w.grab().save("replay-smoke-selection.png");
        Note(QJsonDocument(replay.aliases).toJson(QJsonDocument::Compact).constData());
    }
    if(!Explain("replay Ctrl-click adds a second visible wire",w.Viewport().Selection().entityIds.size()==2))return false;
    if(replay.Act({{"op","point"},{"id","$secondLine"},{"native_edge",-1},{"parameter",.5}})
        ||replay.Act({{"op","point"},{"id","$secondLine"},{"parameter",1.5}}))return false;
    // Unknown operations and hidden UI must fail; they cannot silently become API calls.
    if(replay.Act({{"op","create_shape"}})||replay.Act({{"op","click"},{"widget","does-not-exist"}})
        ||replay.Act({{"op","number"},{"widget","drawingCoordinateP3_0"},{"value",4}}))return false;
    return replay.Act({{"op","mark"}})
        &&!replay.Act({{"op","remember"},{"kind",2},{"as","not-created"},{"new",true}});
}

bool RealSettings(V2MainWindow& w)
{
    // Each isolated case has its own working directory. Keep event traces and
    // failure captures there so a failed step remains inspectable.
    const auto artifactRoot=QDir::current().filePath("real-settings");
    if(!QDir().mkpath(artifactRoot))return false;
    w.RunCommand("file.new");const int planes=CountOfKind(w,domain::EntityKind::WorkPlane);
    Replay plane{w,artifactRoot+"/plane",{},{},0};
    if(!QDir().mkpath(plane.root))return false;
    const QJsonArray planeSteps{
        QJsonObject{{"op","tool"},{"command","workplane.create"}},
        QJsonObject{{"op","count"},{"kind",1},{"value",planes}},
        QJsonObject{{"op","combo"},{"widget","workPlaneMethod"},{"text",QStringLiteral("平面から離す")}},
        QJsonObject{{"op","combo"},{"widget","workPlaneReference"},{"text","top_XY"}},
        QJsonObject{{"op","number"},{"widget","workPlaneDistance"},{"value",17.5}},
        QJsonObject{{"op","count"},{"kind",1},{"value",planes}},
        QJsonObject{{"op","click"},{"widget","panelConfirm"},{"text",QStringLiteral("平面を作る")}},
        QJsonObject{{"op","count"},{"kind",1},{"value",planes+1}},
        QJsonObject{{"op","click"},{"within","modeBar"},{"text",QStringLiteral("選択")}}};
    if(!Explain("opening workplane settings does not auto-confirm a test answer",
        plane.Run({{"name","real workplane settings"},{"steps",planeSteps}})))return false;
    const auto wire=DrawRectangleAtByHand(w,.3,.3,.6,.6);if(wire.IsNil())return false;
    Replay extrude{w,artifactRoot+"/extrude",{},{},0};
    if(!QDir().mkpath(extrude.root))return false;
    const QJsonArray extrudeSteps{
        QJsonObject{{"op","select"},{"id",QString::fromStdString(wire.ToString())}},
        QJsonObject{{"op","tool"},{"command","part.extrude"}},
        QJsonObject{{"op","count"},{"kind",4},{"value",0}},
        QJsonObject{{"op","number"},{"widget","extrudeDistance"},{"value",3}},
        QJsonObject{{"op","combo"},{"widget","extrudeDirection"},{"text",QStringLiteral("Z方向")}},
        QJsonObject{{"op","count"},{"kind",4},{"value",0}},
        QJsonObject{{"op","key"},{"key","Return"}},
        QJsonObject{{"op","count"},{"kind",4},{"value",1}}};
    if(!Explain("extrusion is created only after visible settings and confirmation",
        extrude.Run({{"name","real extrusion settings"},{"steps",extrudeSteps}})))return false;
    const auto& shapes=w.Viewport().ShapeViews();
    return Explain("entered plane offset and extrusion distance determine the actual shape",
        shapes.size()==1&&shapes.front().mesh.closed
        &&std::abs(shapes.front().mesh.minimum.z-17.5)<1e-6
        &&std::abs(shapes.front().mesh.maximum.z-20.5)<1e-6);
}

bool FoldOwnerFocus(V2MainWindow& w)
{
    if(!MakeCurvedGuideSurface(w))return false;
    w.RunCommand("fabrication.create");w.RunCommand("fabrication.create");
    const auto models=app::SelectAllOfKind(w.Session().GetDocument().Snapshot(),domain::EntityKind::FabricationModel);
    if(models.entityIds.size()!=1||w.Viewport().FoldPreviewRailCount()<2)return false;
    const auto model=models.entityIds.front();
    const std::array<geometry::Vector3,4> corners{{{250,0,30},{260,0,30},{260,10,30},{250,10,30}}};
    std::vector<geometry::CurveSegment> curves;
    for(std::size_t i=0;i<corners.size();++i){
        const auto line=geometry::CurveSegment::MakeLine(corners[i],corners[(i+1)%corners.size()]);
        if(!line.HasValue())return false;curves.push_back(line.Value());
    }
    const auto made=w.Session().AddWire(curves,false,"separate focus target");
    if(!made.committed||made.createdEntityIds.size()!=1)return false;
    if(!w.SaveAndReopen(QStringLiteral("fold-owner-fixture.kcd2")))return false;
    const auto target=QString::fromStdString(made.createdEntityIds.front().ToString());
    const auto owner=QString::fromStdString(model.ToString());
    const auto root=QDir::current().filePath("fold-owner");if(!QDir().mkpath(root))return false;
    Replay replay{w,root,{},{},0};
    const QJsonArray startSteps{
        QJsonObject{{"op","click"},{"within","modeBar"},{"text",QStringLiteral("選択")}},
        QJsonObject{{"op","select"},{"id",owner}},
        QJsonObject{{"op","fit"}},QJsonObject{{"op","select"},{"id",target}},
        QJsonObject{{"op","tool"},{"command","view.fit_selection"}}};
    if(!replay.Run({{"name","focus an unrelated object"},{"steps",startSteps}}))return false;
    if(!Explain("selection zoom excludes unrelated manufacturing models",w.Viewport().VisibleWidthMm()<40))return false;
    if(!replay.Act({{"op","fit"}}))return false;
    std::optional<QPointF> bandPoint;
    for(int y=100;y<w.Viewport().height()&&!bandPoint;y+=16)
        for(int x=8;x<w.Viewport().width()&&!bandPoint;x+=16)
            if(!w.Viewport().FoldBandsAt(QPointF(x,y)).empty())bandPoint=QPointF(x,y);
    if(!Explain("visible manufacturing pose is pickable",bandPoint.has_value()))return false;
    if(!replay.Act({{"op","visibility"},{"id",owner},{"value",false}})
        ||!Explain("hidden manufacturing pose cannot intercept picking",w.Viewport().FoldBandsAt(*bandPoint).empty())
        ||!replay.Act({{"op","visibility"},{"id",owner},{"value",true}})
        ||!Explain("showing the model restores picking",!w.Viewport().FoldBandsAt(*bandPoint).empty())
        ||!replay.Act({{"op","select"},{"id",target}})
        ||!replay.Act({{"op","tool"},{"command","view.isolate"}})
        ||!Explain("isolating another object hides manufacturing picking",w.Viewport().FoldBandsAt(*bandPoint).empty())
        ||!replay.Act({{"op","tool"},{"command","view.restore_isolation"}})
        ||!Explain("leaving isolation restores manufacturing picking",!w.Viewport().FoldBandsAt(*bandPoint).empty())
        ||!replay.Act({{"op","select"},{"id",owner}})
        ||!replay.Act({{"op","tool"},{"command","view.fit_selection"}})
        ||!Explain("the manufacturing model itself fits its current pose",w.Viewport().VisibleWidthMm()>40&&w.Viewport().VisibleWidthMm()<200))return false;
    w.RunCommand("group.create");
    const auto& groups=w.Session().GetDocument().Snapshot().groups;
    if(groups.empty())return false;
    const auto parent=QString::fromStdString(groups.back().id.ToString());
    if(!replay.Act({{"op","fit"}}))return false;
    bandPoint.reset();
    for(int y=100;y<w.Viewport().height()&&!bandPoint;y+=16)
        for(int x=8;x<w.Viewport().width()&&!bandPoint;x+=16)
            if(!w.Viewport().FoldBandsAt(QPointF(x,y)).empty())bandPoint=QPointF(x,y);
    if(!bandPoint||!replay.Act({{"op","visibility"},{"id",parent},{"group",true},{"value",false}})
        ||!Explain("a hidden parent hides its manufacturing pose",w.Viewport().FoldBandsAt(*bandPoint).empty())
        ||!replay.Act({{"op","visibility"},{"id",parent},{"group",true},{"value",true}})
        ||!Explain("showing the parent restores manufacturing picking",!w.Viewport().FoldBandsAt(*bandPoint).empty()))return false;
    for(const int percent:{0,50,100}){
        if(!replay.Act({{"op","select"},{"id",owner}}))return false;
        w.RunCommand("fabrication.set_assembly");
        if(!replay.Act({{"op","click"},{"within","bendWidget"},{"text",QString::number(percent)}})
            ||!replay.Act({{"op","click"},{"text",QStringLiteral("曲げを適用")}})
            ||!replay.Act({{"op","tool"},{"command","view.fit_selection"}})
            ||!Explain("each bend state focuses on its owner",w.Viewport().VisibleWidthMm()>40&&w.Viewport().VisibleWidthMm()<200)
            ||!replay.Act({{"op","select"},{"id",target}})
            ||!replay.Act({{"op","tool"},{"command","view.fit_selection"}})
            ||!Explain("each bend state excludes unrelated rails",w.Viewport().VisibleWidthMm()<40))return false;
    }
    if(!w.SaveAndReopen(QStringLiteral("fold-owner.kcd2")))return false;
    return replay.Act({{"op","select"},{"id",target}})
        &&replay.Act({{"op","tool"},{"command","view.fit_selection"}})
        &&Explain("reloaded selection zoom excludes the other model",w.Viewport().VisibleWidthMm()<40);
}

constexpr std::size_t kMaximumPhases=128;
std::array<QJsonObject,kMaximumPhases> phases;
std::array<std::string,kMaximumPhases> names;
QString replayRoot;
template<std::size_t I> bool Phase(V2MainWindow& w)
{
    Replay replay{w,replayRoot,Read(QDir(replayRoot).filePath("aliases.json")),{},0};return replay.Run(phases[I]);
}
template<std::size_t... I> constexpr auto Runners(std::index_sequence<I...>)
{
    return std::array<bool(*)(V2MainWindow&),sizeof...(I)>{&Phase<I>...};
}
}

std::vector<SelfTestCase> UiReplayCases()
{
    std::vector<SelfTestCase> cases{{"HP-REPLAY visible coordinate entry and rejected hidden operations",Smoke},
        {"HP-REPLAY real workplane and extrusion settings require confirmation",RealSettings},
        {"HP-REPLAY manufacturing ownership controls visibility picking and focus",FoldOwnerFocus}};
    const auto script=qEnvironmentVariable("KACHACAD_UI_REPLAY");if(script.isEmpty())return cases;
    const auto document=Read(script);replayRoot=QFileInfo(script).absolutePath();
    if(!document["phases"].isArray()||document["phases"].toArray().isEmpty()){
        cases.push_back({"UI-REPLAY missing or empty phases",[](V2MainWindow&){return false;}});return cases;
    }
    constexpr auto runners=Runners(std::make_index_sequence<kMaximumPhases>{});
    std::size_t index=0;
    for(const auto& value:document["phases"].toArray()){
        if(index==kMaximumPhases){cases.push_back({"UI-REPLAY too many phases",[](V2MainWindow&){return false;}});break;}
        phases[index]=value.toObject();
        names[index]=QString("UI-REPLAY-%1 %2").arg(index,2,10,QChar('0')).arg(phases[index]["name"].toString()).toStdString();
        cases.push_back({names[index].c_str(),runners[index]});++index;
    }
    if(index==0)cases.push_back({"UI-REPLAY invalid script",[](V2MainWindow&){return false;}});
    return cases;
}
}
