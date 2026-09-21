//! 「面の解析」の人の道(HP-SA、プロンプト surface_analysis)。
//!
//! 棚の見えているボタンを実際に押して、ゼブラ・ガウス曲率・U/V 線・境目・ずれを切り替える。
//! 平面は「ほぼ可展」、回転体の下見(曲がった断面)は二重曲率と言い、下見が消えれば塗りも
//! 消える。棚を閉じても表示は残り、ゼブラの入り切りは棚を開かずにできる。

#include "V2SelfTest.h"

#include "V2DrawingDock.h"
#include "V2MainWindow.h"
#include "V2SurfaceAnalysisDock.h"
#include "V2SurfaceAnalysisTool.h"
#include "V2SurfaceDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/SurfaceAnalysis.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

#include <string>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::app::SurfaceAnalysisMode;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::modeling::DrawingTool;

//! 試験が終われば(途中で落ちても)解析を消して棚を閉じる。後の試験の見た目に残さない。
struct AnalysisReset {
    V2MainWindow& window;
    ~AnalysisReset()
    {
        window.SurfaceAnalysis().SetMode(SurfaceAnalysisMode::None);
        window.SurfaceAnalysis().Close();
    }
};

//! 上から見て、画面の割合で指した 3 点(始点・通過点・終点)で円弧を引く。
[[nodiscard]] EntityId DrawArcAtByHand(V2MainWindow& window, const QPointF& start,
    const QPointF& through, const QPointF& end)
{
    auto& viewport = window.Viewport();
    const int before = CountOfKind(window, EntityKind::Wire);
    auto settings = window.DrawingDock().Settings();
    settings.arcMode = kachakacha::v2::modeling::ArcMode::ThreePoints;
    window.DrawingDock().SetSettings(settings);
    window.SelectTool(DrawingTool::Arc);
    viewport.SetSnapSuppressed(true);
    for (const QPointF& at : {start, through, end}) {
        const QPointF screen(viewport.width() * at.x(), viewport.height() * at.y());
        viewport.HoverAt(screen);
        viewport.ClickAt(screen);
    }
    viewport.SetSnapSuppressed(false);
    window.SelectTool(DrawingTool::Select);
    if (CountOfKind(window, EntityKind::Wire) != before + 1) {
        return EntityId{};
    }
    EntityId newest;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Wire) {
            newest = entity.id;
        }
    }
    return newest;
}

//! 上から見て、画面の割合で指した 2 点を直線で結ぶ。
[[nodiscard]] EntityId DrawLineByHand(V2MainWindow& window, double x0, double y0, double x1,
    double y1)
{
    auto& viewport = window.Viewport();
    const int before = CountOfKind(window, EntityKind::Wire);
    window.SelectTool(DrawingTool::Line);
    viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(viewport.width() * x0, viewport.height() * y0));
    viewport.HoverAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.ClickAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.SetSnapSuppressed(false);
    window.SelectTool(DrawingTool::Select);
    if (CountOfKind(window, EntityKind::Wire) != before + 1) {
        return EntityId{};
    }
    EntityId newest;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Wire) {
            newest = entity.id;
        }
    }
    return newest;
}

//! 矩形を引いて平面の面にする(線を拾う → 面を作る → Enter)。
[[nodiscard]] bool PlaneFromRectangle(V2MainWindow& window, double x0, double y0, double x1,
    double y1)
{
    const EntityId wire = DrawRectangleAtByHand(window, x0, y0, x1, y1);
    if (wire.IsNil()) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    if (!ClickOnCurveOf(window, wire)) {
        return false;
    }
    window.RunCommand("surface.create");
    return window.SurfacePreviewShown() && window.HandleToolKey(Qt::Key_Return, nullptr)
        && CountOfKind(window, EntityKind::GuideSurface) == 1;
}

[[nodiscard]] std::size_t PreviewViews(V2MainWindow& window)
{
    std::size_t count = 0;
    for (const auto& view : window.Viewport().AnalysisViews()) {
        count += view.entityId.IsNil() && view.Painted() ? 1 : 0;
    }
    return count;
}

//! HP-SA-01。平面の面を棚の見えているボタンで解析する。
[[nodiscard]] bool CaseSurfaceAnalysisPlane(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const AnalysisReset reset{window};
    if (!Explain("平面の面が 1 枚できる", PlaneFromRectangle(window, 0.30, 0.30, 0.70, 0.70))) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    auto& tool = window.SurfaceAnalysis();
    window.RunCommand("view.surface_analysis");
    V2SurfaceAnalysisDock& dock = *tool.Dock();
    if (!Explain("面の解析を押すと棚が出る", window.ShelfShown(Shelf::SurfaceAnalysis))
        || !Explain("ガウス曲率を押せる", dock.ClickMode(SurfaceAnalysisMode::GaussianCurvature))
        || !Explain("選んでいなければ全部の面(1 枚)を塗る", tool.TargetCount() == 1
                && window.Viewport().AnalysisViews().size() == 1
                && window.Viewport().AnalysisViews().front().Painted())) {
        return false;
    }
    const std::string developable = tool.DevelopabilityLabelJa();
    const QString legend = dock.LegendTextJa();
    if (!Explain(("平面はほぼ可展(" + developable + ")").c_str(),
            developable.rfind("ほぼ可展", 0) == 0)
        || !Explain("読み方に目安と基準と断定しない一文が出る",
            legend.contains(QStringLiteral("製作性の目安: ほぼ可展"))
                && legend.contains(QStringLiteral("0.1 % 未満"))
                && legend.contains(QStringLiteral("診断材料")))) {
        return false;
    }
    if (!Explain("ゼブラを押せる", dock.ClickMode(SurfaceAnalysisMode::Zebra))
        || !Explain("面がゼブラで塗られる",
            window.Viewport().AnalysisViews().front().mode == SurfaceAnalysisMode::Zebra)
        || !Explain("U/V 線を押せる", dock.ClickMode(SurfaceAnalysisMode::IsoCurves))
        || !Explain("U/V 線が重なる", !window.Viewport().AnalysisViews().front().lines.empty())
        || !Explain("境目の連続を押せる", dock.ClickMode(SurfaceAnalysisMode::Continuity))
        || !Explain(("隣の無い縁 4 本と言う(" + dock.TargetTextJa().toStdString() + ")").c_str(),
            dock.TargetTextJa().contains(QStringLiteral("隣の面が無い縁 4 本"))
                && window.Viewport().AnalysisViews().front().lines.size() == 4)
        || !Explain("入力線からのずれを押せる", dock.ClickMode(SurfaceAnalysisMode::Deviation))
        || !Explain(("線から作った面のずれを言う(" + dock.TargetTextJa().toStdString() + ")").c_str(),
            dock.TargetTextJa().contains(QStringLiteral("最大 0.0000 mm")))) {
        return false;
    }
    if (!Explain("閉じるを押せる", dock.ClickClose())
        || !Explain("棚は引っ込む", !window.ShelfShown(Shelf::SurfaceAnalysis))
        || !Explain("表示は残る", !window.Viewport().AnalysisViews().empty())) {
        return false;
    }
    window.RunCommand("view.analysis_zebra");
    if (!Explain("ゼブラは棚を開かずに入る", tool.Mode() == SurfaceAnalysisMode::Zebra
                && !window.ShelfShown(Shelf::SurfaceAnalysis))) {
        return false;
    }
    window.RunCommand("view.analysis_zebra");
    return Explain("もう一度で消える", tool.Mode() == SurfaceAnalysisMode::None
            && window.Viewport().AnalysisViews().empty());
}

//! HP-SA-02。面を作るの下見(回転体)も塗り、製作性の目安が棚に出て、下見が消えれば消える。
[[nodiscard]] bool CaseSurfaceAnalysisPreview(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const AnalysisReset reset{window};
    if (!Explain("平面の面が 1 枚できる", PlaneFromRectangle(window, 0.10, 0.20, 0.30, 0.40))) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    auto& tool = window.SurfaceAnalysis();
    window.RunCommand("view.surface_analysis");
    if (!Explain("ガウス曲率を押せる",
            tool.Dock()->ClickMode(SurfaceAnalysisMode::GaussianCurvature))
        || !Explain("閉じても表示は残る", tool.Dock()->ClickClose() && tool.TargetCount() == 1)) {
        return false;
    }
    // 曲がった断面(円弧)を、離れた縦の軸のまわりに回す → 二重曲率の面。
    const EntityId arc = DrawArcAtByHand(window, QPointF(0.62, 0.40), QPointF(0.70, 0.50),
        QPointF(0.62, 0.60));
    const EntityId axis = DrawLineByHand(window, 0.50, 0.30, 0.50, 0.70);
    if (!Explain("円弧と軸を手で引ける", !arc.IsNil() && !axis.IsNil())) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    window.RunCommand("guide.revolve");
    if (!Explain("断面を拾える", ClickOnCurveOf(window, arc))
        || !Explain("軸を拾える", ClickOnCurveOf(window, axis))
        || !Explain("回した面の下見が出る", window.SurfacePreviewShown())) {
        return false;
    }
    const std::string developable = tool.DevelopabilityLabelJa();
    const QString status = window.SurfaceDock().StatusTextJa();
    if (!Explain("下見の面も塗る(平面 + 下見)", tool.TargetCount() == 2 && PreviewViews(window) == 1)
        || !Explain(("いちばん作りにくい面は二重曲率(" + developable + ")").c_str(),
            developable.find("二重曲率") != std::string::npos)
        || !Explain(("面を作るの棚に製作性の目安が出る(" + status.toStdString() + ")").c_str(),
            status.contains(QStringLiteral("製作性の目安")) && status.contains(QStringLiteral("二重曲率"))
                && status.contains(QStringLiteral("診断材料")))) {
        return false;
    }
    if (!Explain("Esc でやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))) {
        return false;
    }
    return Explain("下見が消えれば塗りも消える", tool.TargetCount() == 1 && PreviewViews(window) == 0)
        && Explain("平面だけに戻ればほぼ可展", tool.DevelopabilityLabelJa().rfind("ほぼ可展", 0) == 0);
}

} // namespace

std::vector<SelfTestCase> SurfaceAnalysisCases()
{
    return {
        {"HP-SA-01 面の解析の棚でゼブラ・ガウス曲率・U/V 線・境目・ずれを切り替え、平面はほぼ可展",
            CaseSurfaceAnalysisPlane},
        {"HP-SA-02 面を作るの下見も塗り、二重曲率を棚に出し、下見が消えれば塗りも消える",
            CaseSurfaceAnalysisPreview},
    };
}

} // namespace kachakacha::v2::selftest
