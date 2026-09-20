//! 引いた線が「打ったとおり・狙ったとおり」になるか(オーナー報告 2026-09-21)。
//!
//!   HP-AC-01 ほぼ真下へ引いた線が、黙って 89.95 度にならない(直角スナップ)。
//!   HP-AC-02 長さを打って Tab で角度へ移り、Enter で打ったとおりの線ができる。
//!
//! どちらも「画面を押す・欄へ打つ」の人の道だけを通る。値を直接入れない。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

#include <cmath>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::DrawingTool;

[[nodiscard]] int WireCount(V2MainWindow& window)
{
    return CountOfKind(window, EntityKind::Wire);
}

//! 最後に作られた線の、作業平面の上での向き(度)と長さ(mm)。
[[nodiscard]] bool LastLineAngleAndLength(V2MainWindow& window, double& angleDeg,
    double& lengthMm)
{
    const auto& curves = window.Session().Scene().curves;
    if (curves.empty()) {
        return false;
    }
    const auto& plane = window.Viewport().WorkPlane();
    const auto& segment = curves.back().segment;
    const double du = plane.CoordinateU(segment.EndPoint())
        - plane.CoordinateU(segment.StartPoint());
    const double dv = plane.CoordinateV(segment.EndPoint())
        - plane.CoordinateV(segment.StartPoint());
    constexpr double kPi = 3.14159265358979323846;
    angleDeg = std::atan2(dv, du) * 180.0 / kPi;
    lengthMm = std::hypot(du, dv);
    return true;
}

//! HP-AC-01。真下へ引いたつもりの線が、1px の狙いのずれで 89.95 度にならない。
[[nodiscard]] bool CaseNearVerticalLineBecomesExactlyVertical(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    auto& viewport = window.Viewport();
    // グリッドへ吸い付かない場所で引く。吸着先があるときは、そちらが正しい。
    viewport.SetSnapSuppressed(true);
    window.SelectTool(DrawingTool::Line);
    const int before = WireCount(window);
    const double x = viewport.width() * 0.5;
    const double y = viewport.height() * 0.35;
    viewport.ClickAt(QPointF(x, y));
    // 1px だけ横へずらして、真下へ引く。人の手はこれ以上そろわない。
    viewport.SetSnapSuppressed(false);
    viewport.HoverAt(QPointF(x + 1.0, y + 200.0));
    viewport.ClickAt(QPointF(x + 1.0, y + 200.0));
    window.SelectTool(DrawingTool::Select);
    if (!Explain("線が1本できる", WireCount(window) == before + 1)) {
        return false;
    }
    double angleDeg = 0.0;
    double lengthMm = 0.0;
    if (!Explain("線の向きが読める", LastLineAngleAndLength(window, angleDeg, lengthMm))) {
        return false;
    }
    const double offBy = std::abs(std::abs(angleDeg) - 90.0);
    return Explain((std::string("真下ちょうどになる(実際 ") + std::to_string(angleDeg)
                       + " 度)").c_str(),
        offBy < 1.0e-6);
}

//! HP-AC-02。長さを打ち、Tab で角度へ移り、角度を打って Enter。
//! 打った長さが Tab で捨てられていたので、線が引けなかった。
[[nodiscard]] bool CaseTypedLengthAndAngleDrawTheLine(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    auto& viewport = window.Viewport();
    window.SelectTool(DrawingTool::Line);
    const int before = WireCount(window);
    viewport.ClickAt(QPointF(viewport.width() * 0.5, viewport.height() * 0.5));
    // 1点目を置くと、カーソル横の入力列が出る。主要欄は長さ。
    viewport.HoverAt(QPointF(viewport.width() * 0.6, viewport.height() * 0.45));
    if (!Explain("入力列が出ている", viewport.CursorPanel().active)) {
        return false;
    }
    if (!Explain("長さを打てる", viewport.TypeIntoCursorField(QStringLiteral("12")))) {
        return false;
    }
    if (!Explain("Tab で角度の欄へ移れる", viewport.FocusNextCursorField(false))) {
        return false;
    }
    if (!Explain("角度を打てる", viewport.TypeIntoCursorField(QStringLiteral("90")))) {
        return false;
    }
    if (!Explain("Enter で決まる", viewport.CommitCursorField())) {
        return false;
    }
    viewport.FinishTool();
    window.SelectTool(DrawingTool::Select);
    if (!Explain((std::string("線が1本できる(実際 ")
                     + std::to_string(WireCount(window) - before) + " 本)").c_str(),
            WireCount(window) == before + 1)) {
        return false;
    }
    double angleDeg = 0.0;
    double lengthMm = 0.0;
    if (!Explain("線の向きと長さが読める", LastLineAngleAndLength(window, angleDeg, lengthMm))) {
        return false;
    }
    if (!Explain((std::string("長さは打った 12mm(実際 ") + std::to_string(lengthMm)
                     + ")").c_str(),
            std::abs(lengthMm - 12.0) < 1.0e-6)) {
        return false;
    }
    return Explain((std::string("向きは打った 90 度(実際 ") + std::to_string(angleDeg)
                       + " 度)").c_str(),
        std::abs(angleDeg - 90.0) < 1.0e-6);
}

} // namespace

std::vector<SelfTestCase> DrawingAccuracyCases()
{
    return {
        {"HP-AC-01 ほぼ真下へ引いた線は真下ちょうどになる", CaseNearVerticalLineBecomesExactlyVertical},
        {"HP-AC-02 長さを打ち Tab で角度へ移って Enter で打ったとおりの線ができる",
            CaseTypedLengthAndAngleDrawTheLine},
    };
}

} // namespace kachakacha::v2::selftest
