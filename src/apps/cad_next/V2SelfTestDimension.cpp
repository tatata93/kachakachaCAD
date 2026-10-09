#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2DimensionTool.h"
#include "kachakacha/document/Dimension.h"
#include <QApplication>
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
    auto& viewport=window.Viewport();viewport.SetViewDirection(ViewDirection::Top);viewport.SetVisibleWidthMm(200);
    const double scale=viewport.width()/200.0;const QPointF center(viewport.width()*0.5,viewport.height()*0.5);
    window.SelectTool(modeling::DrawingTool::Line);
    viewport.ClickAt(center+QPointF(-30*scale,-20*scale));viewport.ClickAt(center+QPointF(30*scale,-20*scale));
    window.RunCommand("dimension.open");QApplication::processEvents();
    auto* tool=dynamic_cast<V2DimensionTool*>(window.findChild<QWidget*>(QStringLiteral("dimensionTool")));
    if(!Explain("寸法コマンドで右ペインの道具を開く",tool!=nullptr))return false;
    const auto& curve=window.Session().Scene().curves.back();
    app::SelectionRef ref;ref.entityId=curve.entityId;ref.segmentId=curve.segmentId;
    if(!Explain("道具の後から線を選べる",tool->AddTarget(ref)))return false;
    auto* value=tool->findChild<QDoubleSpinBox*>(QStringLiteral("dimensionValue"));
    value->setValue(20);
    if(!Explain("数値指定で寸法を作れる",tool->Commit()))return false;
    auto dim=window.Session().GetDocument().Snapshot().referenceDimensions.back();
    if(!Explain("線の長さが寸法値へ変わる",std::abs(document::EvaluateDimension(window.Session().GetDocument().Snapshot(),dim).Value().value-20)<1e-5))return false;
    if(!Explain("連続作成のパネルが残る",tool->isVisible()))return false;
    auto* driving=tool->findChild<QCheckBox*>(QStringLiteral("dimensionDriving"));driving->setChecked(false);
    if(!tool->AddTarget(ref)||!tool->Commit())return false;
    if(!Explain("参照寸法も作れる",window.Session().GetDocument().Snapshot().referenceDimensions.size()==2))return false;
    tool->Load(dim.id);value->setValue(30);
    if(!tool->Commit())return false;
    if(!Explain("保存済み寸法を編集できる",std::abs(document::EvaluateDimension(window.Session().GetDocument().Snapshot(),dim).Value().value-30)<1e-5))return false;
    if(qEnvironmentVariableIsSet("KACHACAD_DIMENSION_SCREENSHOT"))
        viewport.grab().save(window.Theme()==UiTheme::Normal ? QStringLiteral("_claudeout/dimension-normal.png") : QStringLiteral("_claudeout/dimension-win95.png"));
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&viewport,&escape);
    return Explain("Escでも寸法ツールを維持",tool->isVisible() && viewport.KeptDimensionCount()==2);
}
bool DimensionNormal(V2MainWindow& window){window.ApplyTheme(UiTheme::Normal);return DimensionFlow(window);}
bool DimensionWin95(V2MainWindow& window){window.ApplyTheme(UiTheme::Windows95);return DimensionFlow(window);}
}
std::vector<SelfTestCase> DimensionCases()
{
    return {{"HP-DIM-01 寸法作成と編集を続ける",&DimensionNormal},{"HP-DIM-02 Win95寸法操作",&DimensionWin95}};
}
}
