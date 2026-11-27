//! Model Explorer(左の一覧)の人の道(HP-XP)。正本 3 HTML(2026-09-18)、指示書 model_explorer。
//!
//! 節の並び(原点が先頭)、1つずつの ◉ での出し隠し、3D ↔ 一覧の選択の同期、右クリックの献立。
//! 見えている行を実際に触る。文書の値は core の Command を通ってしか変えない。

#include "V2SelfTest.h"

#include "V2EntityTree.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/UiMode.h"
#include "kachakacha/modeling/ToolController.h"
#include "kachakacha/view/ViewOrientation.h"

#include <QApplication>
#include <QAction>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPoint>
#include <QPointF>
#include <QPushButton>
#include <QString>
#include <QTreeWidgetItem>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QIODevice>
#include <QScrollBar>
#include <QMouseEvent>
#include <QEvent>
#include <QStyleOptionViewItem>
#include <QStyle>
#include <QCryptographicHash>
#include <QPixmap>
#include <QDir>
#include <QWidget>
#include <QByteArray>

#include <map>
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Visibility;

bool PickExplorerElement(V2MainWindow& window, EntityId id, const QString& label)
{
    auto* row = window.ItemOfEntity(id);
    if (!row) return false;
    window.EntityTree()->setCurrentItem(row);
    QMenu menu(&window);
    window.BuildExplorerMenu(menu);
    auto* elements = menu.findChild<QMenu*>(QStringLiteral("explorerElements"));
    if (!elements) return false;
    for (auto* action : elements->actions()) {
        if (action->text() == label) {
            action->trigger();
            return true;
        }
        if (auto* segment = action->menu()) {
            for (auto* choice : segment->actions()) {
                if (choice->text() == label) {
                    choice->trigger();
                    return true;
                }
            }
        }
    }
    return false;
}

bool CaseExplorerSubelements(V2MainWindow& window)
{
    using geometry::Vector3;
    using geometry::CurveSegment;
    window.RunCommand("file.new");
    const auto wire = window.AddPlainWire({CurveSegment::MakeLine({-24,-6,-3},{16,-6,2}).Value()},
        "作業面を横切る線", false);
    if (wire.IsNil() || !window.SaveAndReopen("explorer-workplane-crossing.kcd2")) return false;
    auto& viewport = window.Viewport();
    window.RunCommand("selection.activate");
    std::optional<V2Viewport::PickedPoint> point;
    viewport.BeginPointPick([&point](const auto& value) { point = value; }, "作図点を指定してください");
    if (!Explain("the left list gives an exact endpoint of the selected wire",
            PickExplorerElement(window,wire,QStringLiteral("終点")) && point && point->curve
            && point->curve->entityId==wire && point->point==Vector3{16,-6,2})) return false;
    point.reset();
    viewport.BeginPointPick([&point](const auto& value) { point = value; }, "作図点を指定してください");
    if (!Explain("the left list gives the finite curve/plane root with curve identity",
            PickExplorerElement(window,wire,QStringLiteral("作業面との交点 1")) && point
            && point->curve && point->curve->entityId==wire
            && geometry::Distance(point->point,{0,-6,0}) < 1e-10)) return false;

    const std::array<Vector3,4> corner{{{-12,-10,0},{12,-10,0},{12,10,0},{-12,10,0}}};
    std::vector<CurveSegment> boundaries;
    for (std::size_t i = 0; i < corner.size(); ++i)
        boundaries.push_back(CurveSegment::MakeLine(corner[i],corner[(i+1)%corner.size()]).Value());
    const auto boundary = window.AddPlainWire(boundaries, "面の外周", false);
    if (boundary.IsNil() || !window.SaveAndReopen("explorer-face-input.kcd2")) return false;
    viewport.SetSelection(app::SelectionSet{{boundary}});
    window.RunCommand("surface.from_lines");
    if (!window.HandleToolKey(Qt::Key_Return,nullptr)) return false;
    window.RunCommand("selection.activate");
    const auto& entities = window.Session().GetDocument().Snapshot().entities;
    const auto face = std::find_if(entities.begin(), entities.end(), [](const auto& entity) {
        return entity.kind==EntityKind::GuideSurface;
    });
    if (face==entities.end()) return false;
    const auto faceId=face->id;
    std::optional<app::PickCandidate> pickedFace;
    viewport.BeginElementPick([](const auto& pick) { return pick.kind==app::SelectionElementKind::Face; },
        [&pickedFace](const auto& pick) { pickedFace=pick; });
    return Explain("the left list submits the chosen face to a pending face role",
        PickExplorerElement(window,faceId,QStringLiteral("面 1")) && pickedFace
        && pickedFace->entityId==faceId && pickedFace->pickedFaceIndex==0
        && std::abs(pickedFace->hitPoint.z) < 1e-10 && !viewport.ReferencePickPending());
}

bool CaseExplorerCurveLength(V2MainWindow& window)
{
    using geometry::CurveSegment;
    using geometry::Vector3;
    window.RunCommand("file.new");
    const auto line = CurveSegment::MakeLine({-24,-6,-3},{16,-6,2}).Value();
    const auto arc = CurveSegment::MakeCircularArc({4,20,0},{0,0,1},{1,0,0},10,1.2,-2.4).Value();
    const auto bezier = CurveSegment::MakeCubicBezier({{0,-28,0},{2,-28,0},{4,-28,0},{19,-28,0}}).Value();
    const auto wire = window.AddPlainWire({line},"Line through the workplane",false);
    const auto curved = window.AddPlainWire({arc},"Reversed circular arc",false);
    const auto spline = window.AddPlainWire({bezier},"Nonuniform length parameter",false);
    if (wire.IsNil() || curved.IsNil() || spline.IsNil() || !window.SaveAndReopen("explorer-length.kcd2")) return false;
    window.RunCommand("selection.activate");
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter({});
    viewport.SetVisibleWidthMm(100);
    const auto revision = window.Session().GetDocument().Revision();
    std::optional<V2Viewport::PickedPoint> point;
    const auto begin = [&] {
        point.reset();
        viewport.BeginPointPick([&point](const auto& value) { point = value; },"作図点を指定してください");
    };
    const auto at = [&](EntityId id, const Vector3& expected) {
        return point && point->curve && point->curve->entityId==id
            && geometry::Distance(point->point,expected)<1e-6;
    };
    begin();
    if (!Explain("listed line midpoint preserves its out-of-plane coordinate",
        PickExplorerElement(window,wire,QStringLiteral("中点")) && at(wire,line.Evaluate(.5)))) return false;
    begin();
    const auto screen = viewport.Mapping().Project(line.Evaluate(.5));
    if (!screen) return false;
    viewport.ClickAt(QPointF(screen->x,screen->y));
    if (!Explain("the viewport click and the listed midpoint return the same curve point",
        at(wire,line.Evaluate(.5)))) return false;
    begin();
    if (!Explain("nonuniform curves use half of the length rather than half of the parameter",
        PickExplorerElement(window,spline,QStringLiteral("中点")) && at(spline,{9.5,-28,0})
        && std::abs(point->curve->curveParameter.value_or(0.5)-0.5)>0.1)) return false;

    begin();
    auto* treeRow = window.ItemOfEntity(curved);
    if (!treeRow) return false;
    window.EntityTree()->setCurrentItem(treeRow);
    QMenu menu(&window);
    window.BuildExplorerMenu(menu);
    auto* elements = menu.findChild<QMenu*>(QStringLiteral("explorerElements"));
    auto* segment = elements && !elements->actions().isEmpty() ? elements->actions().front()->menu() : nullptr;
    if (!segment) return false;
    segment->popup(window.mapToGlobal(QPoint{20,60}));
    auto* numeric = segment->findChild<QMenu*>(QStringLiteral("explorerCurveLengthMenu"));
    if (!numeric) return false;
    numeric->popup(window.mapToGlobal(QPoint{180,60}));
    auto* percent = numeric->findChild<QDoubleSpinBox*>(QStringLiteral("explorerCurveLengthPercent"));
    auto* editor = percent ? percent->findChild<QLineEdit*>() : nullptr;
    if (!editor) return false;
    percent->setValue(34.0);
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
    QApplication::sendEvent(numeric,&escape);
    if (!Explain("escaping the percentage menu leaves the point request active",
        !numeric->isVisible() && viewport.PickPending() && !point)) return false;
    numeric->popup(window.mapToGlobal(QPoint{180,60}));
    percent->setValue(25.0);
    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
    QApplication::sendEvent(editor,&enter);
    return Explain("the percentage and Enter use the reversed arc's actual start and leave the document unchanged",
        at(curved,arc.Evaluate(.25)) && !viewport.PickPending() && !numeric->isVisible()
        && window.Session().GetDocument().Revision()==revision);
}

bool CaseExplorerDirection(V2MainWindow& window)
{
    using geometry::CurveSegment;
    using geometry::Vector3;
    window.RunCommand("file.new");
    window.resize(1440, 850);
    const Vector3 start{-25,-12,0}, end{10,9,0};
    const auto line = window.AddPlainWire({CurveSegment::MakeLine(start,{5,-3,0}).Value()},
        "Direction start", false);
    const auto cutter = window.AddPlainWire({CurveSegment::MakeLine({4,6,-3},{16,12,3}).Value()},
        "Direction across the workplane", false);
    if (line.IsNil() || cutter.IsNil() || !window.SaveAndReopen("explorer-direction.kcd2")) return false;
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter({});
    viewport.SetVisibleWidthMm(100);
    window.RunCommand("grid.edit");
    auto* fit = window.findChild<QPushButton*>("gridPickAngle");
    if (!fit || !fit->isVisible()) return false;
    const auto revision = window.Session().GetDocument().Revision();
    const auto selectedPlane = window.ActiveWorkPlaneId();
    const auto wanted = geometry::Normalized(end-start);
    const auto gridMatches = [&] {
        const auto actual = window.Session().Scene().grid.uDirection;
        return !viewport.DirectionPickPending() && std::abs(geometry::Dot(actual,wanted)) > 0.99999999;
    };
    const auto pickEnd = [&] { return PickExplorerElement(window,cutter,QStringLiteral("作業面との交点 1")); };
    const auto pickStart = [&] { return PickExplorerElement(window,line,QStringLiteral("始点")); };
    fit->click();
    if (!Explain("two explorer endpoints can set the grid direction",
            pickStart() && viewport.DirectionPickPending() && pickStart() && viewport.DirectionPickPending()
            && pickEnd() && gridMatches())) return false;

    const auto clickModelPoint = [&](const Vector3& p) {
        const auto pos = viewport.Mapping().Project(p);
        if (!pos) return false;
        viewport.ClickAt(QPointF(pos->x,pos->y));
        return true;
    };
    viewport.SetSnapSuppressed(true);
    fit->click();
    const bool viewThenExplorer = clickModelPoint(start) && pickEnd() && gridMatches();
    fit->click();
    const bool explorerThenView = pickStart() && clickModelPoint(end) && gridMatches();
    viewport.SetSnapSuppressed(false);
    if (!Explain("both directions of mixed explorer and viewport input work", viewThenExplorer && explorerThenView)) return false;
    fit->click();
    if (!Explain("the workplane component of a listed off-plane direction is used",
            PickExplorerElement(window,cutter,QStringLiteral("始点")) && pickEnd()
            && !viewport.DirectionPickPending()
            && std::abs(geometry::Dot(window.Session().Scene().grid.uDirection,
                geometry::Normalized(Vector3{6,3,0})))>0.99999999)) return false;
    const auto configuredAngle = window.Session().Scene().grid.angleDegrees;
    fit->click();
    if (!pickStart() || !window.HandleToolKey(Qt::Key_Escape,nullptr)
        || viewport.DirectionPickPending() || window.Session().Scene().grid.angleDegrees != configuredAngle) return false;

    const auto forward = view::ForwardOf(viewport.Orientation());
    window.RunCommand("view.align_horizontal");
    if (!pickStart() || !pickEnd()) return false;
    const auto a = viewport.Mapping().Project(start);
    const auto b = viewport.Mapping().Project(end);
    return Explain("explorer points also align the screen without changing document or grid",
        !viewport.DirectionPickPending() && a && b && std::abs(a->y-b->y)<1e-7
        && geometry::Dot(view::ForwardOf(viewport.Orientation()),forward)>0.99999999
        && window.Session().Scene().grid.angleDegrees==configuredAngle
        && window.Session().GetDocument().Revision()==revision && window.ActiveWorkPlaneId()==selectedPlane);
}

//! 線を1本引いて、その物の番号を返す。取れなければ Nil。
[[nodiscard]] EntityId DrawLineAt(V2MainWindow& window, double offsetMm)
{
    auto& viewport = window.Viewport();
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    const auto first = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{-20.0, offsetMm, 0.0});
    const auto second = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{20.0, offsetMm, 0.0});
    if (!first.has_value() || !second.has_value()) {
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        return EntityId{};
    }
    viewport.ClickAt(QPointF(first->x, first->y));
    viewport.ClickAt(QPointF(second->x, second->y));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    EntityId made;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Wire) {
            made = entity.id;
        }
    }
    return made;
}

[[nodiscard]] Visibility VisibilityOf(V2MainWindow& window, const EntityId& id)
{
    const auto* entity = window.Session().GetDocument().FindEntity(id);
    return entity == nullptr ? Visibility::Hidden : entity->visibility;
}

//! HP-XP-01。節は正本の並びで、原点が先頭。文書の行が根。
[[nodiscard]] bool CaseExplorerSectionsInOrder(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const char* expected[7] = {"原点", "作業面", "ワイヤー", "面", "立体", "近似", "生成物"};
    if (!Explain((std::string("節が7つ(実際 ") + std::to_string(window.GroupRowCount()) + ")").c_str(),
            window.GroupRowCount() == 7)) {
        return false;
    }
    for (int row = 0; row < 7; ++row) {
        if (!Explain((std::string("節の並び ") + expected[row] + "(実際 "
                         + window.GroupRowText(row).toStdString() + ")").c_str(),
                window.GroupRowText(row) == QString::fromUtf8(expected[row]))) {
            return false;
        }
    }
    const auto rows = window.ExplorerRows();
    if (!Explain("節の見出しから中身を選べる",
            (rows[3]->flags() & Qt::ItemIsSelectable))) {
        return false;
    }
    return Explain("根は文書の行", window.EntityTree()->topLevelItemCount() == 1
        && window.EntityTree()->topLevelItem(0)->text(1) == QStringLiteral("文書"));
}

//! HP-XP-02。引いた線はワイヤーの節に出て、◉ を外すと文書の visibility が Hidden になる。
//! 取り消しで戻る(Command を通っている証拠)。
[[nodiscard]] bool CaseExplorerToggleVisibilityPerEntity(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId line = DrawLineAt(window, 0.0);
    if (!Explain("線が引ける", !line.IsNil())) {
        return false;
    }
    QTreeWidgetItem* item = window.ItemOfEntity(line);
    if (!Explain("線の行がある", item != nullptr)
        || !Explain("線の行はワイヤーの節の下",
            item->parent() != nullptr && item->parent()->text(0) == QStringLiteral("ワイヤー"))
        || !Explain("最初は ◉ が付いている", item->checkState(0) == Qt::Checked)) {
        return false;
    }
    item->setCheckState(0, Qt::Unchecked);
    QApplication::processEvents();
    if (!Explain("◉ を外すと文書で隠れる", VisibilityOf(window, line) == Visibility::Hidden)) {
        return false;
    }
    QTreeWidgetItem* rebuilt = window.ItemOfEntity(line);
    if (!Explain("作り直した行も ◉ が外れている",
            rebuilt != nullptr && rebuilt->checkState(0) == Qt::Unchecked)) {
        return false;
    }
    window.RunCommand("edit.undo");
    if (!Explain("取り消すと出る", VisibilityOf(window, line) == Visibility::Visible)) {
        return false;
    }
    rebuilt = window.ItemOfEntity(line);
    return Explain("行の ◉ も戻る", rebuilt != nullptr && rebuilt->checkState(0) == Qt::Checked);
}

//! HP-XP-03。3D で押すと一覧の行が選ばれ、一覧で選ぶと 3D でも選ばれる。
[[nodiscard]] bool CaseExplorerSelectionSyncBothWays(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId a = DrawLineAt(window, 0.0);
    const EntityId b = DrawLineAt(window, 10.0);
    if (!Explain("線が2本引ける", !a.IsNil() && !b.IsNil())) {
        return false;
    }
    if (!Explain("3D で1本目を押せる", ClickOnCurveOf(window, a))) {
        return false;
    }
    QTreeWidgetItem* rowA = window.ItemOfEntity(a);
    QTreeWidgetItem* rowB = window.ItemOfEntity(b);
    if (!Explain("一覧で1本目の行が選ばれる", rowA != nullptr && rowA->isSelected())
        || !Explain("2本目の行は選ばれない", rowB != nullptr && !rowB->isSelected())) {
        return false;
    }
    window.EntityTree()->clearSelection();
    rowB->setSelected(true);
    QApplication::processEvents();
    const auto& picked = window.Viewport().Selection().entityIds;
    return Explain("一覧で2本目を選ぶと 3D も2本目だけ",
        picked.size() == 1 && picked.front() == b);
}

//! HP-XP-04。右クリックの献立は正本の順(名前変更/表示・非表示/正対/グループへ移動/複製/削除/プロパティ)。
//! 「グループへ移動」で実際にグループへ入る(落としたのと同じ道)。
[[nodiscard]] bool CaseExplorerContextMenuOrderAndMove(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId line = DrawLineAt(window, 0.0);
    if (!Explain("線が引ける", !line.IsNil()) || !Explain("3D で押せる", ClickOnCurveOf(window, line))) {
        return false;
    }
    const std::vector<QString> labels = window.ExplorerMenuLabels();
    const char* wanted[] = {"名前を変える", "選択を隠す", "選択に正対", "グループへ移動", "削除", "プロパティ"};
    std::size_t at = 0;
    for (const QString& label : labels) {
        if (at < 6 && label.contains(QString::fromUtf8(wanted[at]))) {
            ++at;
        }
    }
    if (!Explain((std::string("献立が正本の順(見つかった ") + std::to_string(at) + "/6)").c_str(), at == 6)) {
        return false;
    }
    // グループを作ってから、線を献立の道で入れる。
    window.Viewport().SetSelection({});
    window.RunCommand("group.create");
    if (!Explain("グループができる", !window.GroupItems().empty())) {
        return false;
    }
    if (!Explain("3D で線を押せる", ClickOnCurveOf(window, line))) {
        return false;
    }
    std::vector<QTreeWidgetItem*> moved{window.ItemOfEntity(line)};
    window.DropTreeItemsOnto(moved, window.GroupItems().front().first);
    const auto* entity = window.Session().GetDocument().FindEntity(line);
    return Explain("線がグループに入る", entity != nullptr && entity->groupId.has_value())
        && Explain("線の行がグループの行の下",
            window.ItemOfEntity(line) != nullptr
                && window.ItemOfEntity(line)->parent()->parent() == window.GroupItems().front().first);
}

bool CaseExplorerKeepsViewState(V2MainWindow& window) {
    window.RunCommand("file.new");
    const auto line=DrawLineAt(window,0);if(line.IsNil())return false;
    auto* row=window.ItemOfEntity(line);if(!row)return false;
    row->parent()->setExpanded(false);
    const auto wireKey=V2EntityTree::KeyOf(row->parent());
    EntityId plane;
    for(const auto& e:window.Session().GetDocument().Snapshot().entities)
        if(e.kind==EntityKind::WorkPlane){plane=e.id;break;}
    window.ItemOfEntity(plane)->setCheckState(0,Qt::Unchecked);QApplication::processEvents();
    Note((std::string("tree wire expanded=")+std::to_string(window.ItemOfEntity(line)->parent()->isExpanded())
        +" plane hidden="+std::to_string(VisibilityOf(window,plane)==Visibility::Hidden)+" status="+window.StatusText().toStdString()).c_str());
    if(!Explain("hiding a plane preserves collapsed wire category",!window.ItemOfEntity(line)->parent()->isExpanded()
        &&VisibilityOf(window,plane)==Visibility::Hidden))return false;
    window.ItemOfEntity(line)->setCheckState(0,Qt::Unchecked);QApplication::processEvents();
    window.RunCommand("group.create");
    if(!Explain("unrelated changes preserve hidden entities and category identity",VisibilityOf(window,line)==Visibility::Hidden
        &&VisibilityOf(window,plane)==Visibility::Hidden&&V2EntityTree::KeyOf(window.ItemOfEntity(line)->parent())==wireKey
        &&!window.ItemOfEntity(line)->parent()->isExpanded()))return false;
    auto* group=window.GroupItems().front().first;const auto groupId=window.GroupItems().front().second;
    window.EntityTree()->clearSelection();group->setSelected(true);window.RunCommand("group.create");
    window.EntityTree()->clearSelection();
    for(const auto& entry:window.GroupItems())if(entry.second==groupId)entry.first->setSelected(true);
    window.RunCommand("group.create");window.EntityTree()->clearSelection();
    std::map<QString,bool> categories;
    for(const auto& entry:window.GroupItems()) {
        for(int i=0;i<entry.first->childCount();++i) {
            auto* item=entry.first->child(i);if(item->text(1)!=QStringLiteral("節"))continue;
            const bool open=categories.size()%2==0;item->setExpanded(open);categories[V2EntityTree::KeyOf(item)]=open;
        }
    }
    window.ItemOfEntity(plane)->setCheckState(0,Qt::Checked);QApplication::processEvents();
    for(const auto& entry:window.GroupItems())for(int i=0;i<entry.first->childCount();++i) {
        auto* item=entry.first->child(i);const auto found=categories.find(V2EntityTree::KeyOf(item));
        if(found!=categories.end()&&!Explain("nested sibling categories preserve independent expansion",item->isExpanded()==found->second))return false;
    }
    window.SetEntityFilterText(QString::fromStdString(window.Session().GetDocument().FindEntity(line)->displayName));
    if(!Explain("search reveals matching category",window.ItemOfEntity(line)->parent()->isExpanded()))return false;
    window.SetEntityFilterText(QString());
    if(!Explain("clearing search restores previous collapse",!window.ItemOfEntity(line)->parent()->isExpanded()))return false;
    window.RunCommand("edit.undo");window.RunCommand("edit.redo");
    return Explain("undo redo preserves collapse and unrelated visibility",!window.ItemOfEntity(line)->parent()->isExpanded()
        &&VisibilityOf(window,line)==Visibility::Hidden&&VisibilityOf(window,plane)==Visibility::Visible);
}

} // namespace

namespace {
void ExplorerMouseClick(QWidget* widget,const QPointF& position){
    QMouseEvent press(QEvent::MouseButtonPress,position,widget->mapToGlobal(position.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,position,widget->mapToGlobal(position.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(widget,&press);QApplication::sendEvent(widget,&release);QApplication::processEvents();
}
QByteArray ExplorerInputHash(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex();}
}
bool ExplorerOwnerLatency(V2MainWindow& w)
{
    const auto path=qEnvironmentVariable("KACHACAD_EXPLORER_OWNER_INPUT");const auto original=ExplorerInputHash(path);QElapsedTimer load;load.start();
    if(original.isEmpty()||!w.OpenDocumentFile(path))return false;const double openMs=load.nsecsElapsed()/1e6;
    w.SelectTool(modeling::DrawingTool::Select);auto* tree=w.EntityTree();
    std::vector<base::EntityId> wires;for(const auto& e:w.Session().GetDocument().Snapshot().entities)
        if(e.kind==EntityKind::Wire)wires.push_back(e.id);
    if(wires.size()<10)return false;
    QJsonArray selecting,toggling;QElapsedTimer timer;bool correct=true;
    const auto builds=w.property("explorerBuilds").toInt();
    for(int i=0;i<10;++i){auto* row=w.ItemOfEntity(wires[i]);tree->scrollToItem(row);QApplication::processEvents();timer.start();
        ExplorerMouseClick(tree->viewport(),tree->visualItemRect(row).center());selecting.append(timer.nsecsElapsed()/1e6);
        correct=correct&&tree->currentItem()==row&&std::find(w.Viewport().Selection().entityIds.begin(),w.Viewport().Selection().entityIds.end(),wires[i])!=w.Viewport().Selection().entityIds.end();}
    auto* row=w.ItemOfEntity(wires.back());auto* selected=tree->currentItem();const auto key=V2EntityTree::KeyOf(selected);
    tree->scrollToItem(row);QApplication::processEvents();const auto scroll=tree->verticalScrollBar()->value();
    const bool visible=w.Session().GetDocument().FindEntity(wires.back())->visibility==Visibility::Visible;bool stable=true;
    for(int i=0;i<50;++i){auto* item=w.ItemOfEntity(wires.back());QStyleOptionViewItem option;option.initFrom(tree);option.rect=tree->visualItemRect(item);
        option.features|=QStyleOptionViewItem::HasCheckIndicator;option.checkState=item->checkState(0);
        const QPointF checkbox=tree->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,&option,tree).center();timer.start();
        ExplorerMouseClick(tree->viewport(),checkbox);toggling.append(timer.nsecsElapsed()/1e6);
        const bool expected=i%2?visible:!visible;
        stable=stable&&w.ItemOfEntity(wires.back())==row&&V2EntityTree::KeyOf(tree->currentItem())==key&&tree->verticalScrollBar()->value()==scroll
            &&(w.Session().GetDocument().FindEntity(wires.back())->visibility==Visibility::Visible)==expected;}
    const bool unchanged=ExplorerInputHash(path)==original;
    const QJsonObject report{{"input_sha256",QString::fromLatin1(original)},{"input_unchanged",unchanged},{"input_transport",QStringLiteral("Qt mouse events; OS injection unavailable")},
        {"open_ms",openMs},{"wire_count",int(wires.size())},{"selection_ms",selecting},{"visibility_ms",toggling},{"tree_rebuilds",w.property("explorerBuilds").toInt()-builds},
        {"correct_selection",correct},{"stable_rows_and_scroll",stable}};
    QFile file(qEnvironmentVariable("KACHACAD_EXPLORER_OWNER_REPORT"));if(!file.open(QIODevice::WriteOnly))return false;file.write(QJsonDocument(report).toJson());
    const auto shots=qEnvironmentVariable("KACHACAD_FOLDER_SHOTS");if(!shots.isEmpty()){QDir dir(shots);dir.mkpath(".");w.grab().save(dir.filePath("owner-explorer.png"));}
    return Explain("owner mouse selection and checkbox changes preserve targets rows scroll and source file",stable&&correct&&unchanged);
}

std::vector<SelfTestCase> ExplorerUpdateCases();
std::vector<SelfTestCase> ExplorerCases()
{
    std::vector<SelfTestCase> cases={
        {"HP-XP-01 一覧の節は正本の順で原点が先頭", CaseExplorerSectionsInOrder},
        {"HP-XP-02 ◉ で1つずつ出し隠しでき、取り消しで戻る", CaseExplorerToggleVisibilityPerEntity},
        {"HP-XP-03 3D と一覧の選択が両方向に同期する", CaseExplorerSelectionSyncBothWays},
        {"HP-XP-04 右クリックの献立は正本の順で、グループへ移せる", CaseExplorerContextMenuOrderAndMove},
        {"HP-XP-05 collapse, nested groups, visibility, search and undo redo", CaseExplorerKeepsViewState},
        {"HP-XP explicit tree endpoints, workplane roots and face roles", CaseExplorerSubelements},
        {"HP-XP line and curve positions by length from the explorer", CaseExplorerCurveLength},
        {"HP-XP explorer and viewport two-point directions", CaseExplorerDirection},
    };
    const auto updates=ExplorerUpdateCases();cases.insert(cases.end(),updates.begin(),updates.end());
    if(qEnvironmentVariableIsSet("KACHACAD_EXPLORER_OWNER_INPUT"))cases.push_back({"HP-XP-OWNER actual explorer latency and checkbox position",ExplorerOwnerLatency});
    return cases;
}

} // namespace kachakacha::v2::selftest
