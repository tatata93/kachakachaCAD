#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2DimensionTool.h"
#include "kachakacha/document/Dimension.h"
#include <QApplication>
#include <QPushButton>
#include <QWidget>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointF>
#include <QPixmap>
#include <QString>
#include <cmath>
namespace kachakacha::v2::selftest {
namespace {
bool DimensionFlow(V2MainWindow& window)
{
    window.resize(1500,900);QApplication::processEvents();
    auto& viewport=window.Viewport();viewport.SetViewDirection(ViewDirection::Top);viewport.SetVisibleWidthMm(200);
    const double scale=viewport.width()/200.0;const QPointF center(viewport.width()*0.5,viewport.height()*0.5);
    window.SelectTool(modeling::DrawingTool::Line);
    viewport.ClickAt(center+QPointF(-30*scale,-20*scale));viewport.ClickAt(center+QPointF(30*scale,-20*scale));
    window.RunCommand("dimension.open");QApplication::processEvents();
    auto* tool=dynamic_cast<V2DimensionTool*>(window.findChild<QWidget*>(QStringLiteral("dimensionTool")));
    if(!Explain("寸法コマンドで右ペインの道具を開く",tool!=nullptr))return false;
    const auto& curve=window.Session().Scene().curves.back();
    app::SelectionRef ref;ref.entityId=curve.entityId;ref.segmentId=curve.segmentId;
    tool->Reset();
    const auto screen=viewport.Mapping().Project((curve.segment.StartPoint()+curve.segment.EndPoint())*0.5);
    if(!screen)return false;
    QMouseEvent pick(QEvent::MouseButtonPress,QPointF(screen->x,screen->y),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&viewport,&pick);
    auto* value=tool->findChild<QDoubleSpinBox*>(QStringLiteral("dimensionValue"));
    if(!Explain("道具を先に選び3Dクリックで対象を拾う",std::abs(value->value()-(curve.segment.EndPoint()-curve.segment.StartPoint()).Length())<1e-4))return false;
    value->setValue(20);
    if(!Explain("数値指定で寸法を作れる",tool->Commit()))return false;
    auto dim=window.Session().GetDocument().Snapshot().referenceDimensions.back();
    if(!Explain("線の長さが寸法値へ変わる",std::abs(document::EvaluateDimension(window.Session().GetDocument().Snapshot(),dim).Value().value-20)<1e-5))return false;
    if(!Explain("連続作成のパネルが残る",tool->isVisible()))return false;
    auto* driving=tool->findChild<QCheckBox*>(QStringLiteral("dimensionDriving"));
    if(!tool->AddTarget(ref))return false;
    value->setValue(99);driving->setChecked(false);
    if(!Explain("参照へ切替で右ペインの値も現在の形に戻る",std::abs(value->value()-20)<1e-5))return false;
    if(!tool->Commit())return false;
    if(!Explain("参照寸法も作れる",window.Session().GetDocument().Snapshot().referenceDimensions.size()==2))return false;
    tool->Load(dim.id);value->setValue(30);
    if(!tool->Commit())return false;
    if(!Explain("保存済み寸法を編集できる",std::abs(document::EvaluateDimension(window.Session().GetDocument().Snapshot(),dim).Value().value-30)<1e-5))return false;
    tool->Load(dim.id);
    tool->findChild<QComboBox*>(QStringLiteral("dimensionKind"))->setCurrentIndex(1);value->setValue(35);
    if(!tool->Commit())return false;
    if(!Explain("種類の変更でも同じ寸法を編集する",window.Session().GetDocument().Snapshot().referenceDimensions.size()==2
        && std::abs(document::EvaluateDimension(window.Session().GetDocument().Snapshot(),dim).Value().value-35)<1e-5))return false;
    if(qEnvironmentVariableIsSet("KACHACAD_DIMENSION_SCREENSHOT"))
        viewport.grab().save(window.Theme()==UiTheme::Normal ? QStringLiteral("_claudeout/dimension-normal.png") : QStringLiteral("_claudeout/dimension-win95.png"));
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&viewport,&escape);
    return Explain("Escでも寸法ツールを維持",tool->isVisible() && viewport.KeptDimensionCount()==2);
}
bool DimensionRoleReplacement(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const auto wire=DrawRectangleAtByHand(window,0.3,0.3,0.7,0.7);
    if(wire.IsNil())return false;
    std::vector<app::SelectionRef> refs;
    for(const auto& curve:window.Session().Scene().curves)if(curve.entityId==wire) {
        app::SelectionRef ref;ref.entityId=wire;ref.segmentId=curve.segmentId;refs.push_back(ref);
    }
    if(refs.size()<3)return false;
    window.Viewport().SetSelection({});window.RunCommand("dimension.open");
    auto* tool=dynamic_cast<V2DimensionTool*>(window.findChild<QWidget*>(QStringLiteral("dimensionTool")));
    if(!tool)return false;
    tool->findChild<QComboBox*>(QStringLiteral("dimensionKind"))->setCurrentIndex(5);
    tool->findChild<QCheckBox*>(QStringLiteral("dimensionDriving"))->setChecked(false);
    auto* a=tool->findChild<QPushButton*>(QStringLiteral("dimensionPickRole0"));
    auto* b=tool->findChild<QPushButton*>(QStringLiteral("dimensionPickRole1"));
    auto* clear=tool->findChild<QPushButton*>(QStringLiteral("dimensionClearRole0"));
    if(!a||!b||!clear)return false;
    b->click();(void)tool->AddTarget(refs[1]);
    a->click();if(!tool->AddTarget(refs[0]))return false;
    clear->click();if(!tool->AddTarget(refs[2]))return false;
    if(!tool->Commit())return false;
    const auto& dim=window.Session().GetDocument().Snapshot().referenceDimensions.back();
    return Explain("Bを先に選びAのみ交換してもBを保持する",dim.segments.size()==2
        &&dim.segments[0].segmentId==*refs[2].segmentId&&dim.segments[1].segmentId==*refs[1].segmentId);
}

bool DimensionNormal(V2MainWindow& window){window.ApplyTheme(UiTheme::Normal);return DimensionFlow(window);}
bool DimensionWin95(V2MainWindow& window){window.ApplyTheme(UiTheme::Windows95);return DimensionFlow(window);}
bool CloseFocusedViewport(V2MainWindow&)
{
    for(int iteration=0;iteration<5;++iteration) {
        auto child=std::make_unique<V2MainWindow>();child->show();QApplication::processEvents();
        child->Viewport().setFocus();QApplication::processEvents();
        child->Viewport().SetAxisConstraintByKey(true);
        child->Viewport().SetSnapSuppressedByKey(true);
        child.reset();QApplication::processEvents();
    }
    return true;
}
}
std::vector<SelfTestCase> DimensionCases()
{
    return {{"HP-DIM-03 role replacement preserves the other input",&DimensionRoleReplacement},{"HP-DIM-01 寸法作成と編集を続ける",&DimensionNormal},{"HP-DIM-02 Win95寸法操作",&DimensionWin95},
        {"HP-LIFE-01 拘束中のビューを持つウインドウを終了できる",&CloseFocusedViewport}};
}
}
