// 作図ツールの状態機械(v1-drawing-parity.md §1)。
// V1の23種すべてが、同じやり方で取り消せて、同じやり方で中断できること。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/ToolController.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ArcMode;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::modeling::DrawingToolNameJa;
using kachakacha::v2::modeling::ToolSession;
using kachakacha::v2::modeling::ToolSettings;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kPi = 3.14159265358979323846;

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

[[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

//! 点を順に置いて、確定した出力を返す。
[[nodiscard]] std::optional<kachakacha::v2::modeling::ToolOutput> PlaceAll(
    ToolSession& session, const std::vector<Vector3>& points)
{
    std::optional<kachakacha::v2::modeling::ToolOutput> finished;
    for (const Vector3& point : points) {
        const auto result = session.AddPoint(point);
        Require(result.HasValue(), "点を置けること");
        if (result.Value().has_value()) {
            finished = result.Value();
        }
    }
    return finished;
}

//! V1の23種すべて。
[[nodiscard]] std::vector<DrawingTool> AllTools()
{
    return {DrawingTool::Select, DrawingTool::SetGridOrigin, DrawingTool::Point,
        DrawingTool::Line, DrawingTool::Polyline, DrawingTool::Rectangle,
        DrawingTool::Circle, DrawingTool::Arc, DrawingTool::Bezier, DrawingTool::Spline,
        DrawingTool::Move, DrawingTool::Copy, DrawingTool::Mirror, DrawingTool::Rotate,
        DrawingTool::Split, DrawingTool::Trim, DrawingTool::Extend,
        DrawingTool::JoinEndpoints, DrawingTool::TangentJoin, DrawingTool::CurvatureJoin,
        DrawingTool::Measure, DrawingTool::ConnectTwoPoints,
        DrawingTool::ChamferOrFilletPair};
}

} // namespace

KACHA_V2_TEST(tool, V1の23種がすべてある)
{
    RequireCount(AllTools().size(), 23, "ツールの数");
    for (const DrawingTool tool : AllTools()) {
        Require(!DrawingToolNameJa(tool).empty(),
            "名前があること: " + std::to_string(static_cast<int>(tool)));
    }
}

KACHA_V2_TEST(tool, 作図点は1点で確定する)
{
    ToolSession session(DrawingTool::Point, {}, Tolerance());
    RequireCount(static_cast<std::size_t>(session.Prompt().remainingPoints), 1, "あと1点");
    const auto output = PlaceAll(session, {{10.0, 20.0, 0.0}});
    Require(output.has_value(), "確定すること");
    RequireCount(output->points.size(), 1, "点の数");
    RequireNear(output->points.front().x, 10.0, 1e-12, "位置");
}

KACHA_V2_TEST(tool, 直線は2点で確定する)
{
    ToolSession session(DrawingTool::Line, {}, Tolerance());
    Require(!session.AddPoint({0.0, 0.0, 0.0}).Value().has_value(), "1点では確定しない");
    RequireCount(static_cast<std::size_t>(session.Prompt().remainingPoints), 1, "あと1点");
    const auto second = session.AddPoint({10.0, 0.0, 0.0});
    Require(second.Value().has_value(), "2点で確定すること");
    RequireCount(second.Value()->segments.size(), 1, "線の数");
    Require(second.Value()->segments.front().Kind() == CurveKind::Line, "直線であること");
}

KACHA_V2_TEST(tool, 矩形は対角2点で4本の線になる)
{
    ToolSession session(DrawingTool::Rectangle, {}, Tolerance());
    const auto output = PlaceAll(session, {{0.0, 0.0, 0.0}, {40.0, 30.0, 0.0}});
    Require(output.has_value(), "確定すること");
    RequireCount(output->segments.size(), 4, "線の数");
    // 周長が 2*(40+30)。
    double perimeter = 0.0;
    for (const auto& segment : output->segments) {
        perimeter += segment.TotalLength(1e-9);
    }
    RequireNear(perimeter, 2.0 * (40.0 + 30.0), 1e-6, "周長");
}

KACHA_V2_TEST(tool, 矩形と円は作業平面の向きに沿う)
{
    // XY と決め打ちしていたので、前から見る面(ZX)の上では矩形が「つぶれた」と断られ、
    // 円は XY に寝たまま出来ていた。作業平面の向きを渡せば、その面の上に出来る。
    ToolSession rectangle(DrawingTool::Rectangle, {}, Tolerance());
    rectangle.SetPlane({0.0, 1.0, 0.0}, {1.0, 0.0, 0.0});   // ZX 面: 法線 Y、u = X
    const auto output = PlaceAll(rectangle, {{0.0, 0.0, 0.0}, {40.0, 0.0, 30.0}});
    Require(output.has_value(), "ZX 面の上で確定すること");
    RequireCount(output->segments.size(), 4, "線の数");
    double perimeter = 0.0;
    for (const auto& segment : output->segments) {
        perimeter += segment.TotalLength(1e-9);
        RequireNear(segment.StartPoint().y, 0.0, 1e-9, "y = 0 の面の上");
        RequireNear(segment.EndPoint().y, 0.0, 1e-9, "y = 0 の面の上");
    }
    RequireNear(perimeter, 2.0 * (40.0 + 30.0), 1e-6, "周長");

    ToolSession circle(DrawingTool::Circle, {}, Tolerance());
    circle.SetPlane({0.0, 1.0, 0.0}, {1.0, 0.0, 0.0});
    const auto round = PlaceAll(circle, {{0.0, 0.0, 10.0}, {5.0, 0.0, 10.0}});
    Require(round.has_value(), "円も ZX 面で確定すること");
    const auto& arc = round->segments.front();
    for (double t : {0.0, 0.25, 0.5, 0.75}) {
        RequireNear(arc.Evaluate(t).y, 0.0, 1e-9, "円は y = 0 の面の上");
    }
}

KACHA_V2_TEST(tool, つぶれた矩形を断る)
{
    ToolSession session(DrawingTool::Rectangle, {}, Tolerance());
    Require(!session.AddPoint({0.0, 0.0, 0.0}).Value().has_value(), "1点目");
    const auto second = session.AddPoint({0.0, 30.0, 0.0});   // 幅が0
    Require(!second.HasValue(), "断ること");
    // 断ったあとも、1点目は残っていてやり直せること。
    RequireCount(session.Points().size(), 1, "1点目が残ること");
}

KACHA_V2_TEST(tool, 円は中心と半径の点で作る)
{
    ToolSession session(DrawingTool::Circle, {}, Tolerance());
    const auto output = PlaceAll(session, {{10.0, 10.0, 0.0}, {30.0, 10.0, 0.0}});
    Require(output.has_value(), "確定すること");
    Require(output->segments.front().Kind() == CurveKind::Circle, "円であること");
    RequireNear(output->segments.front().Radius(), 20.0, 1e-9, "半径");
}

KACHA_V2_TEST(tool, 円弧の3つの作り方が全部動く)
{
    // 1. 3点を通す。
    {
        ToolSettings settings;
        settings.arcMode = ArcMode::ThreePoints;
        ToolSession session(DrawingTool::Arc, settings, Tolerance());
        RequireCount(static_cast<std::size_t>(session.Prompt().remainingPoints), 3,
            "3点必要");
        const auto output = PlaceAll(session,
            {{0.0, 0.0, 0.0}, {10.0, 10.0, 0.0}, {20.0, 0.0, 0.0}});
        Require(output.has_value(), "確定すること");
        Require(output->segments.front().Kind() == CurveKind::CircularArc, "円弧");
    }
    // 2. 両端と半径。
    {
        ToolSettings settings;
        settings.arcMode = ArcMode::EndpointsAndRadius;
        settings.radiusMm = 15.0;
        ToolSession session(DrawingTool::Arc, settings, Tolerance());
        RequireCount(static_cast<std::size_t>(session.Prompt().remainingPoints), 2,
            "2点必要");
        const auto output = PlaceAll(session, {{0.0, 0.0, 0.0}, {20.0, 0.0, 0.0}});
        Require(output.has_value(), "確定すること");
        RequireNear(output->segments.front().Radius(), 15.0, 1e-9, "半径");
    }
    // 3. 始点と接線方向。
    {
        ToolSettings settings;
        settings.arcMode = ArcMode::StartTangent;
        settings.radiusMm = 12.0;
        settings.sweepAngleRad = kPi / 3.0;
        ToolSession session(DrawingTool::Arc, settings, Tolerance());
        const auto output = PlaceAll(session, {{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}});
        Require(output.has_value(), "確定すること");
        RequireNear(output->segments.front().Radius(), 12.0, 1e-9, "半径");
        RequireNear(std::abs(output->segments.front().SweepAngleRad()), kPi / 3.0, 1e-9,
            "掃引角");
    }
}

KACHA_V2_TEST(tool, 半径が小さすぎる円弧を断る)
{
    ToolSettings settings;
    settings.arcMode = ArcMode::EndpointsAndRadius;
    settings.radiusMm = 1.0;   // 弦の半分(10mm)より小さい
    ToolSession session(DrawingTool::Arc, settings, Tolerance());
    Require(!session.AddPoint({0.0, 0.0, 0.0}).Value().has_value(), "1点目");
    Require(!session.AddPoint({20.0, 0.0, 0.0}).HasValue(), "断ること");
}

KACHA_V2_TEST(tool, ベジェは4点で確定する)
{
    ToolSession session(DrawingTool::Bezier, {}, Tolerance());
    RequireCount(static_cast<std::size_t>(session.Prompt().remainingPoints), 4, "4点必要");
    const auto output = PlaceAll(session, {{0.0, 0.0, 0.0}, {5.0, 10.0, 0.0},
        {15.0, 10.0, 0.0}, {20.0, 0.0, 0.0}});
    Require(output.has_value(), "確定すること");
    Require(output->segments.front().Kind() == CurveKind::CubicBezier, "ベジェ");
}

KACHA_V2_TEST(tool, ポリラインは好きなだけ点を置ける)
{
    ToolSession session(DrawingTool::Polyline, {}, Tolerance());
    Require(session.Prompt().acceptsMorePoints, "いくつでも受け付けること");
    for (int index = 0; index < 6; ++index) {
        const auto result = session.AddPoint({static_cast<double>(index) * 10.0,
            static_cast<double>(index % 2) * 5.0, 0.0});
        Require(result.HasValue(), "点を置けること");
        Require(!result.Value().has_value(), "途中では確定しないこと");
    }
    Require(session.Prompt().canFinish, "確定できること");
    const auto output = session.Finish();
    Require(output.HasValue(), "確定すること");
    RequireCount(output.Value().segments.size(), 5, "線の数");
}

KACHA_V2_TEST(tool, ポリラインは2点未満では確定できない)
{
    ToolSession session(DrawingTool::Polyline, {}, Tolerance());
    Require(!session.Prompt().canFinish, "点が無ければ確定できない");
    Require(!session.Finish().HasValue(), "断ること");
    Require(session.AddPoint({0.0, 0.0, 0.0}).HasValue(), "1点目");
    Require(!session.Prompt().canFinish, "1点では確定できない");
    Require(!session.Finish().HasValue(), "断ること");
}

KACHA_V2_TEST(tool, スプラインは4点以上で確定する)
{
    ToolSession session(DrawingTool::Spline, {}, Tolerance());
    for (int index = 0; index < 3; ++index) {
        Require(session.AddPoint({static_cast<double>(index) * 10.0, 0.0, 0.0}).HasValue(),
            "点を置けること");
    }
    Require(!session.Prompt().canFinish, "3点では確定できない");
    Require(session.AddPoint({30.0, 10.0, 0.0}).HasValue(), "4点目");
    Require(session.Prompt().canFinish, "4点で確定できる");
    const auto output = session.Finish();
    Require(output.HasValue(), "確定すること");
    Require(output.Value().segments.front().Kind() == CurveKind::CubicBSpline,
        "スプライン");
}

// ---------------------------------------------------------------- 共通の操作

KACHA_V2_TEST(tool, どのツールでも1点戻せる)
{
    // V1は「このツールだけ取り消しが効かない」があった。全ツールで同じにする。
    const DrawingTool tools[]{DrawingTool::Line, DrawingTool::Rectangle,
        DrawingTool::Circle, DrawingTool::Arc, DrawingTool::Bezier, DrawingTool::Spline,
        DrawingTool::Polyline, DrawingTool::Rotate, DrawingTool::Mirror};
    for (const DrawingTool tool : tools) {
        ToolSession session(tool, {}, Tolerance());
        Require(!session.UndoLastPoint(), "点が無ければ戻せないこと");
        Require(session.AddPoint({0.0, 0.0, 0.0}).HasValue(), "1点目");
        RequireCount(session.Points().size(), 1, "1点");
        Require(session.UndoLastPoint(), "戻せること");
        RequireCount(session.Points().size(), 0, "0点");
    }
}

KACHA_V2_TEST(tool, どのツールでもやめられる)
{
    for (const DrawingTool tool : AllTools()) {
        ToolSession session(tool, {}, Tolerance());
        const auto result = session.AddPoint({1.0, 2.0, 0.0});
        // 形を作らないツールは点を受け付けない。それも決まった振る舞い。
        (void)result;
        session.Cancel();
        RequireCount(session.Points().size(), 0,
            std::string(DrawingToolNameJa(tool)) + " をやめられること");
    }
}

KACHA_V2_TEST(tool, どのツールでも案内文が出る)
{
    for (const DrawingTool tool : AllTools()) {
        ToolSession session(tool, {}, Tolerance());
        Require(!session.Prompt().messageJa.empty(),
            std::string(DrawingToolNameJa(tool)) + " の案内文");
    }
}

KACHA_V2_TEST(tool, 形を作らないツールは点を受け付けない)
{
    const DrawingTool tools[]{DrawingTool::Select, DrawingTool::Split, DrawingTool::Trim,
        DrawingTool::Extend, DrawingTool::JoinEndpoints, DrawingTool::TangentJoin,
        DrawingTool::CurvatureJoin, DrawingTool::Measure,
        DrawingTool::ChamferOrFilletPair};
    for (const DrawingTool tool : tools) {
        ToolSession session(tool, {}, Tolerance());
        const auto result = session.AddPoint({0.0, 0.0, 0.0});
        Require(!result.HasValue(),
            std::string(DrawingToolNameJa(tool)) + " は点を受け付けないこと");
        RequireEqual(result.Diagnostics().front().code, std::string("UI-T003"),
            "診断コード");
    }
}

KACHA_V2_TEST(tool, 有限でない座標を断る)
{
    ToolSession session(DrawingTool::Line, {}, Tolerance());
    const auto result = session.AddPoint({std::nan(""), 0.0, 0.0});
    Require(!result.HasValue(), "断ること");
    RequireCount(session.Points().size(), 0, "点が増えないこと");
}

KACHA_V2_TEST(tool, 途中経過が見える)
{
    ToolSession session(DrawingTool::Line, {}, Tolerance());
    Require(session.Preview({10.0, 0.0, 0.0}).empty(), "1点も置いていなければ何も出ない");
    Require(session.AddPoint({0.0, 0.0, 0.0}).HasValue(), "1点目");
    const auto preview = session.Preview({10.0, 5.0, 0.0});
    RequireCount(preview.size(), 1, "途中経過の線");
    RequireNear(preview.front().EndPoint().y, 5.0, 1e-12, "ポインタまで引かれること");
}

KACHA_V2_TEST(tool, 矩形の途中経過も見える)
{
    ToolSession session(DrawingTool::Rectangle, {}, Tolerance());
    Require(session.AddPoint({0.0, 0.0, 0.0}).HasValue(), "1点目");
    const auto preview = session.Preview({40.0, 30.0, 0.0});
    RequireCount(preview.size(), 4, "4本の線として見えること");
}

KACHA_V2_TEST(tool, 円弧の途中経過も見える)
{
    ToolSettings settings;
    settings.arcMode = ArcMode::ThreePoints;
    ToolSession session(DrawingTool::Arc, settings, Tolerance());
    Require(session.AddPoint({0.0, 0.0, 0.0}).HasValue(), "1点目");
    // 2点目まで置いたら、3点目のプレビューで円弧が出る。
    Require(session.AddPoint({10.0, 10.0, 0.0}).HasValue(), "2点目");
    const auto preview = session.Preview({20.0, 0.0, 0.0});
    RequireCount(preview.size(), 1, "1本");
    Require(preview.front().Kind() == CurveKind::CircularArc, "円弧として見えること");
}

KACHA_V2_TEST(tool, 作れない途中経過でも落ちない)
{
    ToolSettings settings;
    settings.arcMode = ArcMode::ThreePoints;
    ToolSession session(DrawingTool::Arc, settings, Tolerance());
    Require(session.AddPoint({0.0, 0.0, 0.0}).HasValue(), "1点目");
    Require(session.AddPoint({10.0, 0.0, 0.0}).HasValue(), "2点目");
    // 3点が一直線。円弧にならない。
    const auto preview = session.Preview({20.0, 0.0, 0.0});
    Require(!preview.empty(), "何かは見えること(線として)");
    for (const auto& segment : preview) {
        Require(segment.Kind() == CurveKind::Line, "線として見せること");
    }
}

KACHA_V2_TEST(tool, 補助線として作れる)
{
    ToolSettings settings;
    settings.construction = true;
    ToolSession session(DrawingTool::Line, settings, Tolerance());
    const auto output = PlaceAll(session, {{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}});
    Require(output.has_value(), "確定すること");
    Require(output->construction, "補助線であること");
}

KACHA_V2_TEST(tool, 変換ツールは基準点を集める)
{
    // 移動・コピーは2点、回転は3点、ミラーは2点。
    struct Case {
        DrawingTool tool;
        std::size_t points;
    };
    const Case cases[]{{DrawingTool::Move, 2}, {DrawingTool::Copy, 2},
        {DrawingTool::Mirror, 2}, {DrawingTool::Rotate, 3}};
    for (const Case& item : cases) {
        ToolSession session(item.tool, {}, Tolerance());
        RequireCount(static_cast<std::size_t>(session.Prompt().remainingPoints),
            item.points, std::string(DrawingToolNameJa(item.tool)) + " の点の数");
        std::vector<Vector3> points;
        for (std::size_t index = 0; index < item.points; ++index) {
            points.push_back({static_cast<double>(index) * 10.0, 0.0, 0.0});
        }
        const auto output = PlaceAll(session, points);
        Require(output.has_value(),
            std::string(DrawingToolNameJa(item.tool)) + " が確定すること");
    }
}

KACHA_V2_TEST(tool, 確定すると点がリセットされる)
{
    ToolSession session(DrawingTool::Line, {}, Tolerance());
    Require(PlaceAll(session, {{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}}).has_value(), "1本目");
    RequireCount(session.Points().size(), 0, "点が空になること");
    // 続けて2本目が引けること。
    Require(PlaceAll(session, {{10.0, 0.0, 0.0}, {10.0, 10.0, 0.0}}).has_value(), "2本目");
}

KACHA_V2_TEST(tool, 点の数が決まったツールは途中で確定できない)
{
    ToolSession session(DrawingTool::Line, {}, Tolerance());
    Require(session.AddPoint({0.0, 0.0, 0.0}).HasValue(), "1点目");
    const auto finish = session.Finish();
    Require(!finish.HasValue(), "断ること");
    RequireEqual(finish.Diagnostics().front().code, std::string("UI-T003"), "診断コード");
}

KACHA_V2_TEST(tool, 指定した点を作図点として残せる)
{
    // V1 の「指定した点を作図点として残す」。線を作る道具のときだけ点が付く。
    ToolSettings settings;
    settings.keepPoints = true;
    ToolSession session(DrawingTool::Line, settings, Tolerance());
    Require(!session.AddPoint({0.0, 0.0, 0.0}).Value().has_value(), "1点目");
    const auto done = session.AddPoint({10.0, 0.0, 0.0});
    Require(done.HasValue() && done.Value().has_value(), "2点目で確定");
    RequireEqual(std::to_string(done.Value()->keptPoints.size()), std::string("2"),
        "指した2点が残る");
    // 作図点の道具では二重に残さない。
    ToolSession point(DrawingTool::Point, settings, Tolerance());
    const auto made = point.AddPoint({1.0, 2.0, 0.0});
    Require(made.HasValue() && made.Value().has_value(), "作図点");
    Require(made.Value()->keptPoints.empty(), "作図点は二重にしない");
    // 既定では残さない。
    ToolSession plain(DrawingTool::Line, ToolSettings{}, Tolerance());
    Require(!plain.AddPoint({0.0, 0.0, 0.0}).Value().has_value(), "1点目");
    Require(plain.AddPoint({10.0, 0.0, 0.0}).Value()->keptPoints.empty(), "既定は残さない");
}

KACHA_V2_TEST_MAIN("tool_tests")
