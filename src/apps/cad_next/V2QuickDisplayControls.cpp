#include "V2QuickDisplayControls.h"
#include "V2Viewport.h"
#include "kachakacha/app/PlaneFocus.h"
#include <QCheckBox>
#include <QObject>
#include <QString>
#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <set>
#include <cmath>
using namespace kachakacha::v2;
namespace {
class PlaneWireDisplay final : public QWidget {
public:
    PlaneWireDisplay(V2Viewport& view,app::DrawingSession& source):QWidget(&view),session(source) {setObjectName("planeWireDisplayState");hide();}
    app::DrawingSession& session;
    bool enabled=false;
    std::set<base::EntityId> visible;
};
PlaneWireDisplay* State(const V2Viewport& view) {
    return static_cast<PlaneWireDisplay*>(view.findChild<QWidget*>("planeWireDisplayState"));
}
QToolButton* Button(QToolBar& bar,const char* id,const QString& text,const QString& tip) {
    auto* button=new QToolButton(&bar);button->setObjectName(id);button->setText(text);
    button->setToolTip(tip);button->setCheckable(true);button->setFocusPolicy(Qt::NoFocus);
    bar.addWidget(button);return button;
}
}
void InstallQuickDisplayControls(V2Viewport& view,QToolBar& bar,app::DrawingSession& session) {
    auto* state=new PlaneWireDisplay(view,session);
    auto* grid=Button(bar,"quickGridOnTop",QStringLiteral("グリッド透過"),
        QStringLiteral("オン: 面やソリッド越しにグリッドを表示。オフ: 形状の後ろにあるグリッドを隠す。"));
    grid->setChecked(true);
    QObject::connect(grid,&QToolButton::toggled,&view,[&view](bool on){view.SetGridOnTop(on);});
    auto* plane=Button(bar,"quickPlaneWiresOnly",QStringLiteral("平面ワイヤのみ"),
        QStringLiteral("現在の作図面内に全体が載るワイヤー・補助線・点のみ表示。作図面の変更に追従し、オフで元の表示に戻ります。"));
    QObject::connect(plane,&QToolButton::toggled,&view,[&view,state](bool on){
        state->enabled=on;RefreshPlaneWireDisplay(view);view.ApplyVisibilityFilter();view.PruneSelection();view.update();
    });
}
void BindQuickGridControl(V2Viewport& view,QCheckBox& check) {
    auto* button=view.window()->findChild<QToolButton*>("quickGridOnTop");if(!button)return;
    check.setChecked(button->isChecked());
    QObject::connect(button,&QToolButton::toggled,&check,&QCheckBox::setChecked);
    QObject::connect(&check,&QCheckBox::toggled,button,[button](bool on){QSignalBlocker block(button);button->setChecked(on);});
}
bool PlaneWireDisplayActive(const V2Viewport& view) {const auto* state=State(view);return state&&state->enabled;}
bool PlaneWireDisplayAllows(const V2Viewport& view,base::EntityId id) {
    const auto* state=State(view);return !state||!state->enabled||state->visible.contains(id);
}
void RefreshPlaneWireDisplay(V2Viewport& view) {
    auto* state=State(view);if(!state||!state->enabled)return;
    state->visible.clear();std::set<base::EntityId> rejected;
    const auto& scene=state->session.Scene();
    for(const auto& curve:scene.curves){
        const auto* entity=state->session.GetDocument().FindEntity(curve.entityId);
        if(!entity||entity->kind!=domain::EntityKind::Wire)continue;
        if(app::CurveLiesOnPlane(curve.segment,view.WorkPlane()))state->visible.insert(curve.entityId);
        else rejected.insert(curve.entityId);
    }
    for(auto id:rejected)state->visible.erase(id);
    for(const auto& point:scene.points){const auto* entity=state->session.GetDocument().FindEntity(point.entityId);
        if(entity&&entity->kind==domain::EntityKind::Point
            &&std::abs(geometry::Dot(point.position-view.WorkPlane().origin,view.WorkPlane().normal))<=app::kOnPlaneToleranceMm)
            state->visible.insert(point.entityId);
    }
}
