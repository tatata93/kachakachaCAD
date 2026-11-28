//! Model Explorer(左の一覧)の右クリック(正本 3 HTML 2026-09-18、指示書 model_explorer)。
//!
//!   名前変更 / 表示・非表示 / 選択に正対 / グループへ移動 / 複製 / 削除 / プロパティ
//!
//! 台帳のコマンドと、現在の図形から指定できる部分要素を並べる。
//! 「グループへ移動」だけは、引きずって落とすのと同じ道(DropTreeItemsOnto)を通す。
//! 消せないときの理由は core(RemoveFeatureCommand)が人の言葉で出す。ここでは言い直さない。

#include "V2MainWindow.h"

#include "V2EdgeFinishTool.h"
#include "V2EditDock.h"
#include "V2EntityTree.h"
#include "V2FaceActions.h"
#include "V2ShellSplitTool.h"
#include "V2SurfaceEditTool.h"
#include "V2Viewport.h"
#include "kachakacha/app/GroupTree.h"
#include "kachakacha/geometry/CurveIntersection.h"

#include <QAction>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QObject>
#include <QPoint>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>
#include <map>

namespace {

//! 台帳の QAction を献立に足す。無い命令は足さない(「押せるが何も起きない」を作らない)。
void AddCatalogAction(V2MainWindow& window, QMenu& menu, const char* id)
{
    if (QAction* action = window.ActionFor(id); action != nullptr) {
        menu.addAction(action);
    }
}

using namespace kachakacha::v2;

void ApplyExplorerPick(V2Viewport& viewport, const app::PickCandidate& pick)
{
    if (!viewport.EntityShown(pick.entityId)) return;
    const bool pending = viewport.PickPending() || viewport.ReferencePickPending();
    const bool toolInput = viewport.ToolPickActive() && !pending;
    const auto modifiers = QApplication::keyboardModifiers();
    viewport.setProperty("treePickInput", pending);
    viewport.setProperty("treeToolInput", toolInput);
    viewport.setProperty("treeInputModifiers", static_cast<int>(modifiers));
    const auto mode = !pending && !toolInput && modifiers.testFlag(Qt::ControlModifier)
        ? app::SelectionMode::Toggle : app::SelectionMode::Replace;
    viewport.SetSelection(app::ApplySelection(viewport.Selection(), pick, mode));
}

double ParameterAtLengthFraction(const geometry::CurveSegment& curve, double fraction, double tolerance)
{
    if (fraction == 0.0 || fraction == 1.0
        || (curve.Kind() != geometry::CurveKind::CubicBezier && curve.Kind() != geometry::CurveKind::CubicBSpline))
        return fraction;

    const double target = fraction * curve.TotalLength(tolerance);
    double low = 0.0;
    double high = 1.0;
    for (int step = 0; step < 50; ++step) {
        const double mid = std::midpoint(low, high);
        const double length = curve.ArcLength(0.0, mid, tolerance);
        if (std::abs(length - target) <= tolerance)
            return mid;
        if (length < target) low = mid;
        else high = mid;
    }
    return std::midpoint(low, high);
}

void ApplyCurveLengthPoint(V2MainWindow& window, app::PickCandidate pick,
    double fraction, qulonglong inputGeneration)
{
    auto& viewport = window.Viewport();
    if (!viewport.PickPending() || viewport.property("inputPickGeneration").toULongLong() != inputGeneration)
        return;
    const auto& session = window.Session();
    const auto& curves = session.Scene().curves;
    const auto curve = std::find_if(curves.begin(), curves.end(), [&](const auto& entry) {
        return entry.entityId == pick.entityId && entry.segmentId == pick.segmentId;
    });
    if (curve == curves.end() || !viewport.EntityShown(pick.entityId)
        || (curve->construction && !viewport.DisplaySettingsNow().constructionVisible))
        return;
    const double tolerance = session.GetDocument().Snapshot().settings.tolerance.modelLinearMm;
    const double parameter = ParameterAtLengthFraction(curve->segment, fraction, tolerance);
    pick.curveParameter = parameter;
    pick.hitPoint = curve->segment.Evaluate(parameter);
    ApplyExplorerPick(viewport, pick);
}

void AddCurveLengthChoices(V2MainWindow& window, QMenu& segment, app::PickCandidate pick)
{
    const auto generation = window.Viewport().property("inputPickGeneration").toULongLong();
    segment.addAction(QStringLiteral("中点"), &window, [&window, pick, generation] {
        ApplyCurveLengthPoint(window, pick, 0.5, generation);
    });
    auto* position = segment.addMenu(QStringLiteral("始点からの長さ (%)"));
    position->setObjectName(QStringLiteral("explorerCurveLengthMenu"));
    QObject::connect(&segment, &QMenu::aboutToShow, position, [&window, position, pick, generation] {
        auto* entry = new QWidgetAction(position);
        auto* row = new QWidget(position);
        auto* layout = new QHBoxLayout(row);
        auto* percent = new QDoubleSpinBox(row);
        percent->setObjectName(QStringLiteral("explorerCurveLengthPercent"));
        percent->setAccessibleName(QStringLiteral("始点からの長さの割合"));
        percent->setRange(0.0, 100.0);
        percent->setDecimals(4);
        percent->setSuffix(QStringLiteral(" %"));
        percent->setValue(50.0);
        percent->setKeyboardTracking(false);
        auto* submit = new QPushButton(QStringLiteral("指定"), row);
        submit->setObjectName(QStringLiteral("explorerCurvePointApply"));
        layout->addWidget(percent);
        layout->addWidget(submit);
        entry->setDefaultWidget(row);
        position->addAction(entry);
        QObject::connect(submit, &QPushButton::clicked, position, [&window, position, percent, pick, generation] {
            if (!percent->hasAcceptableInput()) return;
            percent->interpretText();
            const double fraction = percent->value() / 100.0;
            for (auto* menu = position; menu; menu = qobject_cast<QMenu*>(menu->parentWidget()))
                menu->close();
            ApplyCurveLengthPoint(window, pick, fraction, generation);
        });
        if (auto* editor = percent->findChild<QLineEdit*>())
            QObject::connect(editor, &QLineEdit::returnPressed, submit, &QPushButton::click);
    }, Qt::SingleShotConnection);
}

void AddExplorerElements(V2MainWindow& window, QMenu& menu, base::EntityId id)
{
    auto& viewport = window.Viewport();
    if (!viewport.EntityShown(id)) return;
    const auto& session = window.Session();
    const auto& scene = session.Scene();
    const auto& tolerance = session.GetDocument().Snapshot().settings.tolerance;
    auto* elements = new QMenu(QStringLiteral("部分を指定"), &menu);
    elements->setObjectName(QStringLiteral("explorerElements"));
    elements->setStyleSheet(QStringLiteral("QMenu { menu-scrollable: 1; }"));
    const auto add = [&](QMenu& target, const QString& label, const app::PickCandidate& pick) {
        target.addAction(label, &window, [&window, &viewport, pick] {
            if (!viewport.EntityShown(pick.entityId)) return;
            if (pick.kind == app::SelectionElementKind::Face && pick.pickedFaceIndex
                && viewport.ToolPickActive() && !viewport.PickPending() && !viewport.ReferencePickPending()
                && window.ShellSplitTool().PickListedFace(pick.entityId, *pick.pickedFaceIndex, pick.hitPoint)) return;
            ApplyExplorerPick(viewport, pick);
        });
    };
    int segmentIndex = 0;
    for (const auto& curve : scene.curves) {
        if (curve.entityId != id || (curve.construction && !viewport.DisplaySettingsNow().constructionVisible))
            continue;
        app::PickCandidate pick;
        pick.entityId = id;
        pick.segmentId = curve.segmentId;
        pick.kind = app::SelectionElementKind::Edge;
        pick.curveParameter = .5;
        pick.hitPoint = curve.segment.Evaluate(.5);
        auto* segment = elements->addMenu(QStringLiteral("線分 %1").arg(++segmentIndex));
        if (!viewport.PickPending())
            add(*segment, QStringLiteral("線分を選ぶ"), pick);
        pick.kind = app::SelectionElementKind::Vertex;
        pick.curveParameter = 0;
        pick.hitPoint = curve.segment.StartPoint();
        add(*segment, QStringLiteral("始点"), pick);
        pick.curveParameter = 1;
        pick.hitPoint = curve.segment.EndPoint();
        add(*segment, QStringLiteral("終点"), pick);
        if (viewport.PickPending()) AddCurveLengthChoices(window, *segment, pick);
        if (scene.workPlane.active && viewport.PickPending()) {
            const auto roots = geometry::IntersectCurveWithPlane(curve.segment, scene.workPlane.origin,
                scene.workPlane.normal, tolerance);
            int index = 0;
            for (const auto& root : roots) {
                pick.curveParameter = root.parameter;
                pick.hitPoint = root.position;
                add(*segment, QStringLiteral("作業面との交点 %1").arg(++index), pick);
            }
        }
    }
    if (!viewport.PickPending() && viewport.DisplaySettingsNow().shapesVisible) {
        if (!viewport.ReferencePickPending() && viewport.ToolPickActive()) {
            window.EdgeFinishTool().AddListedEdges(*elements, id);
            window.SurfaceEdit().AddListedEdges(*elements, id);
        }
        for (const auto& shape : viewport.ShapeViews()) {
            if (shape.entityId != id) continue;
            std::map<std::size_t, const modeling::MeshTriangle*> faces;
            for (const auto& triangle : shape.mesh.triangles) {
                if (triangle.faceIndex == modeling::kNoFaceIndex) continue;
                auto& representative = faces[triangle.faceIndex];
                const auto area = [](const modeling::MeshTriangle& value) {
                    return geometry::Cross(value.points[1]-value.points[0],
                        value.points[2]-value.points[0]).LengthSquared();
                };
                if (representative == nullptr || area(triangle) > area(*representative))
                    representative = &triangle;
            }
            for (const auto& [index, triangle] : faces) {
                app::PickCandidate pick;
                pick.entityId = id;
                pick.kind = app::SelectionElementKind::Face;
                pick.pickedFaceIndex = index;
                pick.hitPoint = triangle->Center();
                add(*elements, QStringLiteral("面 %1").arg(index + 1), pick);
            }
        }
    }
    if (elements->isEmpty()) {
        delete elements;
        return;
    }
    menu.insertMenu(menu.actions().isEmpty() ? nullptr : menu.actions().front(), elements);
}

} // namespace

void V2MainWindow::ShowExplorerMenu(const QPoint& at)
{
    if (entityTree_ == nullptr) {
        return;
    }
    // 右クリックした行がまだ選ばれていなければ、その行を選ぶ(選択の同期は普段の道)。
    QTreeWidgetItem* under = entityTree_->itemAt(at);
    if (under != nullptr && (under->flags() & Qt::ItemIsSelectable)) {
        if (!under->isSelected()) entityTree_->setCurrentItem(under);
        else if (under != entityTree_->currentItem())
            entityTree_->setCurrentItem(under, 0, QItemSelectionModel::NoUpdate);
    }
    if (under == nullptr) entityTree_->clearSelection();
    QMenu menu(this);
    BuildExplorerMenu(menu);
    if (!menu.isEmpty()) {
        menu.exec(entityTree_->mapToGlobal(at));
    }
}

//! 献立を組むだけ。出すのは呼び出し側なので、試験は exec を通さずに並びを確かめられる。
void V2MainWindow::BuildExplorerMenu(QMenu& menu)
{
    if (entityTree_ != nullptr && viewport_ != nullptr) {
        const auto* current = entityTree_->currentItem();
        const auto row = std::find_if(entityItems_.begin(), entityItems_.end(),
            [current](const auto& item) { return current == item.first; });
        if (row != entityItems_.end()) AddExplorerElements(*this, menu, row->second);
    }
    AddCatalogAction(*this, menu, "entity.rename");
    AddCatalogAction(*this, menu, "group.rename");
    menu.addSeparator();
    AddCatalogAction(*this, menu, "view.hide_selected");
    AddCatalogAction(*this, menu, "view.show_all");
    auto* show=menu.addAction(QStringLiteral("選択を表示"));
    QObject::connect(show,&QAction::triggered,this,[this]{const auto rows=entityTree_->selectedItems();ApplyExplorerVisibility(std::vector<QTreeWidgetItem*>(rows.begin(),rows.end()),true);});
    AddCatalogAction(*this, menu, "view.align_selection");
    AddCatalogAction(*this, menu, "view.align_selection_back");
    AddCatalogAction(*this, menu, "workplane.from_face");
    V2FaceActions::AddSourceOutlineAction(*this,menu);
    menu.addSeparator();
    BuildMoveToGroupMenu(menu);
    auto* sibling=menu.addAction(QStringLiteral("同じ階層にグループを作る"));
    sibling->setObjectName("explorerSiblingGroupAction");QObject::connect(sibling,&QAction::triggered,this,[this]{CreateSiblingGroup();});
    auto* child=menu.addAction(QStringLiteral("子グループを作る"));child->setEnabled(SelectedGroupId().has_value());
    QObject::connect(child,&QAction::triggered,this,[this]{CreateGroupFromSelection();});
    AddCatalogAction(*this, menu, "group.dissolve");
    menu.addSeparator();
    AddCatalogAction(*this, menu, "wire.copy");
    AddCatalogAction(*this, menu, "edit.delete");
    menu.addSeparator();
    AddCatalogAction(*this, menu, "measure.open");
    QAction* properties = menu.addAction(QStringLiteral("プロパティ"));
    properties->setStatusTip(QStringLiteral("選んだものの直せる値を右の欄に出します。"));
    QObject::connect(properties, &QAction::triggered, this, [this] { ShowProperties(); });
}

//! 「グループへ移動」の小献立。文書のグループを並べ、選ぶと落としたのと同じ道を通す。
//! グループが1つも無ければ、押せない項目として理由を出す。
void V2MainWindow::BuildMoveToGroupMenu(QMenu& menu)
{
    QMenu* into = menu.addMenu(QStringLiteral("グループへ移動"));
    const auto& snapshot = session_->GetDocument().Snapshot();
    QStringList moved;
    for (QTreeWidgetItem* item : entityTree_->selectedItems()) {
        if (item != nullptr && (item->flags() & Qt::ItemIsDragEnabled)) {
            moved.push_back(V2EntityTree::KeyOf(item));
        }
    }
    if (moved.empty()) {
        QAction* none = into->addAction(QStringLiteral("(移せるものを選んでいません)"));
        none->setEnabled(false);
        return;
    }
    const auto drop=[this,moved](const QString& targetKey){
        std::vector<QTreeWidgetItem*> rows;
        for(const auto& key:moved){auto* row=entityTree_->FindKey(key);if(!row){SetStatus(QStringLiteral("移動する項目が変わりました。選び直してください。"));return;}rows.push_back(row);}
        auto* target=targetKey.isEmpty()?nullptr:entityTree_->FindKey(targetKey);
        if(!targetKey.isEmpty()&&!target){SetStatus(QStringLiteral("移動先のグループがありません。選び直してください。"));return;}
        DropTreeItemsOnto(rows,target);
    };
    std::map<QString,int> paths;
    for(const auto& group:snapshot.groups)++paths[QString::fromStdString(kachakacha::v2::app::GroupPathJa(snapshot,group.id))];
    for (const auto& [item, groupId] : groupItems_) {
        const auto key=V2EntityTree::KeyOf(item);
        auto label=QString::fromStdString(kachakacha::v2::app::GroupPathJa(snapshot,groupId));
        if(paths[label]>1)label+=QStringLiteral(" [%1]").arg(QString::fromStdString(groupId.ToString()));
        QAction* action=into->addAction(label);
        action->setEnabled(!moved.contains(key));
        QObject::connect(action,&QAction::triggered,this,[drop,key]{drop(key);});
    }
    if (snapshot.groups.empty()) {
        QAction* none = into->addAction(QStringLiteral("(グループがありません。先に「グループを作る」)"));
        none->setEnabled(false);
    }
    into->addSeparator();
    QAction* out = into->addAction(QStringLiteral("グループの外へ"));
    QObject::connect(out, &QAction::triggered, this,
        [drop] { drop(QString()); });
}

//! プロパティ: 選んだものの直せる値は右の「直す」欄に出る。欄を前に出すだけ。
void V2MainWindow::ShowProperties()
{
    RefreshEditDock();
    if (editDock_ != nullptr) {
        editDock_->show();
        editDock_->raise();
    }
}

std::vector<QString> V2MainWindow::ExplorerMenuLabels()
{
    QMenu menu(this);
    BuildExplorerMenu(menu);
    std::vector<QString> labels;
    for (QAction* action : menu.actions()) {
        if (!action->isSeparator()) {
            labels.push_back(action->text());
        }
    }
    return labels;
}
