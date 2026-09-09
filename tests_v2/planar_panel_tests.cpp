// 平らな部品をそのまま型紙にする(fabrication-contract §9)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/PlanarPanel.h"

#include <cmath>
#include <string>

using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::fabrication::BuildPlanarPanel;
using kachakacha::v2::fabrication::BuildPlanarPanels;
using kachakacha::v2::fabrication::CheckPlanar;
using kachakacha::v2::fabrication::PatternPanel;
using kachakacha::v2::fabrication::PatternPlacement;
using kachakacha::v2::fabrication::PlacePanelCurves;
using kachakacha::v2::fabrication::PanelForOpening;
using kachakacha::v2::fabrication::PlanarPanelRequest;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kTolerance = 0.01;

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[nodiscard]] CurveSegment Line(Vector3 start, Vector3 end)
{
    const auto made = CurveSegment::MakeLine(start, end);
    Require(made.HasValue(), "線が作れる");
    return made.Value();
}

//! XY平面の上の 100 x 60 の四角。
[[nodiscard]] std::vector<CurveSegment> Rectangle(double z = 0.0)
{
    return {Line({0, 0, z}, {100, 0, z}), Line({100, 0, z}, {100, 60, z}),
        Line({100, 60, z}, {0, 60, z}), Line({0, 60, z}, {0, 0, z})};
}

//! 好きな位置と大きさの四角。開口の試験に使う。
[[nodiscard]] std::vector<CurveSegment> RectangleAt(double x, double y, double width,
    double height, double z)
{
    return {Line({x, y, z}, {x + width, y, z}),
        Line({x + width, y, z}, {x + width, y + height, z}),
        Line({x + width, y + height, z}, {x, y + height, z}),
        Line({x, y + height, z}, {x, y, z})};
}

//! 型紙の部材の外周から、囲む四角の大きさを測る。
struct Extent {
    double width = 0.0;
    double height = 0.0;
};

[[nodiscard]] Extent MeasureOutline(const PatternPanel& panel)
{
    double minU = 1.0e18;
    double maxU = -1.0e18;
    double minV = 1.0e18;
    double maxV = -1.0e18;
    for (const auto& point : panel.outline) {
        minU = std::min(minU, point.u);
        maxU = std::max(maxU, point.u);
        minV = std::min(minV, point.v);
        maxV = std::max(maxV, point.v);
    }
    return Extent{maxU - minU, maxV - minV};
}

} // namespace

KACHA_V2_TEST(planar_panel, 平らな線は平らと分かる)
{
    const auto check = CheckPlanar(Rectangle(), kTolerance);
    Require(check.planar, "平ら");
    RequireNear(check.maxDeviationMm, 0.0, 1.0e-6, "外れていない");
    // XY平面なので法線はZ向き(符号はどちらでもよい)。
    RequireNear(std::abs(check.normal.z), 1.0, 1.0e-6, "法線はZ");
}

KACHA_V2_TEST(planar_panel, ねじれていれば平らでないと分かる)
{
    std::vector<CurveSegment> twisted = Rectangle();
    twisted.push_back(Line({50, 30, 0}, {50, 30, 5}));
    const auto check = CheckPlanar(twisted, kTolerance);
    Require(!check.planar, "平らでない");
    Require(check.maxDeviationMm > 1.0, "どれだけ外れているかが分かる");
}

KACHA_V2_TEST(planar_panel, 平らな部品はそのまま型紙になる)
{
    PlanarPanelRequest request;
    request.panelId = "側面";
    request.boundary = Rectangle();
    const auto panel = BuildPlanarPanel(request, kTolerance);
    Require(panel.HasValue(), "作れる");
    RequireEqual(panel.Value().panelId, std::string("側面"), "名前");
    // 寸法は変えない。100 x 60 のまま。
    const Extent extent = MeasureOutline(panel.Value());
    RequireNear(extent.width, 100.0, 1.0e-6, "幅");
    RequireNear(extent.height, 60.0, 1.0e-6, "高さ");
}

KACHA_V2_TEST(planar_panel, 高さが違っても寸法は変わらない)
{
    // Z=50 の平面に置いても、型紙の上の大きさは同じでなければならない。
    PlanarPanelRequest request;
    request.panelId = "上面";
    request.boundary = Rectangle(50.0);
    const auto panel = BuildPlanarPanel(request, kTolerance);
    Require(panel.HasValue(), "作れる");
    const Extent extent = MeasureOutline(panel.Value());
    RequireNear(extent.width, 100.0, 1.0e-6, "幅");
    RequireNear(extent.height, 60.0, 1.0e-6, "高さ");
}

KACHA_V2_TEST(planar_panel, 開口も型紙に載る)
{
    PlanarPanelRequest request;
    request.panelId = "窓つき";
    request.boundary = Rectangle();
    request.openings.push_back({Line({20, 20, 0}, {40, 20, 0}), Line({40, 20, 0}, {40, 40, 0}),
        Line({40, 40, 0}, {20, 40, 0}), Line({20, 40, 0}, {20, 20, 0})});
    const auto panel = BuildPlanarPanel(request, kTolerance);
    Require(panel.HasValue(), "作れる");
    RequireEqual(std::to_string(panel.Value().openings.size()), std::string("1"), "開口1つ");
    Require(!panel.Value().openings.front().empty(), "点がある");
}

KACHA_V2_TEST(planar_panel, 平らでない部品は断る)
{
    // 近似して作ると、切ってから合わないことに気づく。だから断る。
    PlanarPanelRequest request;
    request.panelId = "曲がった面";
    request.boundary = Rectangle();
    request.boundary.push_back(Line({50, 30, 0}, {50, 30, 8}));
    const auto panel = BuildPlanarPanel(request, kTolerance);
    Require(!panel.HasValue(), "断る");
    RequireEqual(FirstCode(panel.Diagnostics()), std::string("FAB-P004"), "コード");
    // 何mm外れているかを言う。言わないと、どう直せばよいか分からない。
    Require(panel.Diagnostics().front().detailsJa.find("mm") != std::string::npos,
        "外れ量を言う");
}

KACHA_V2_TEST(planar_panel, 外周が無ければ断る)
{
    const auto panel = BuildPlanarPanel(PlanarPanelRequest{}, kTolerance);
    Require(!panel.HasValue(), "断る");
    RequireEqual(FirstCode(panel.Diagnostics()), std::string("FAB-P003"), "コード");
}

KACHA_V2_TEST(planar_panel, 1枚でも作れなければそこで止める)
{
    // 途中まで作って渡すと、足りないことに気づかないまま切ることになる。
    PlanarPanelRequest good;
    good.panelId = "よい";
    good.boundary = Rectangle();
    PlanarPanelRequest bad;
    bad.panelId = "わるい";
    bad.boundary = Rectangle();
    bad.boundary.push_back(Line({50, 30, 0}, {50, 30, 8}));
    const auto panels = BuildPlanarPanels({good, bad}, kTolerance);
    Require(!panels.HasValue(), "断る");
    RequireEqual(FirstCode(panels.Diagnostics()), std::string("FAB-P004"), "コード");
}

KACHA_V2_TEST(planar_panel, 置いても寸法は変わらない)
{
    // 置き場所は回転と平行移動だけ。倍率も鏡像も無い。
    PlanarPanelRequest request;
    request.panelId = "側面";
    request.boundary = Rectangle();
    const auto panel = BuildPlanarPanel(request, kTolerance);
    Require(panel.HasValue(), "作れる");
    PatternPlacement placement;
    placement.panelId = "側面";
    placement.translationMm = kachakacha::v2::geometry::Point2{10.0, 20.0};
    placement.rotationRad = 0.7853981633974483;
    const auto placed = PlacePanelCurves(panel.Value(), placement);
    Require(placed.HasValue(), "置ける");
    const auto atOrigin = PlacePanelCurves(panel.Value(), PatternPlacement{});
    Require(atOrigin.HasValue(), "原点にも置ける");
    // 外周を1周した長さは、置き方によらず同じでなければならない。
    // 変わるなら倍率か鏡像が入っている。
    const auto perimeter = [](const auto& curves) {
        double total = 0.0;
        for (const auto& curve : curves) {
            total += curve.segment.TotalLength(kTolerance);
        }
        return total;
    };
    RequireNear(perimeter(placed.Value()), perimeter(atOrigin.Value()), 1.0e-6,
        "1周の長さは置き方で変わらない");
    RequireNear(perimeter(atOrigin.Value()), 320.0, 1.0e-6, "100+60+100+60");
}

KACHA_V2_TEST(planar_panel, 外周は閉じる)
{
    // 閉じないと切り抜けない。
    PlanarPanelRequest request;
    request.panelId = "側面";
    request.boundary = Rectangle();
    const auto panel = BuildPlanarPanel(request, kTolerance);
    Require(panel.HasValue(), "作れる");
    const auto curves = PlacePanelCurves(panel.Value(), PatternPlacement{});
    Require(curves.HasValue(), "置ける");
    // 端から端までたどると元へ戻る。
    double gap = 0.0;
    for (std::size_t index = 0; index < curves.Value().size(); ++index) {
        const auto& current = curves.Value()[index];
        const auto& next = curves.Value()[(index + 1) % curves.Value().size()];
        const Vector3 a = current.segment.EndPoint();
        const Vector3 b = next.segment.StartPoint();
        gap = std::max(gap, std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)));
    }
    RequireNear(gap, 0.0, 1.0e-6, "つながって閉じている");
}

KACHA_V2_TEST(planar_panel, 開口は別のレイヤーに出る)
{
    // 外周と開口を同じレイヤーに出すと、どちらを先に切ればよいか分からない。
    PlanarPanelRequest request;
    request.panelId = "窓つき";
    request.boundary = Rectangle();
    request.openings.push_back({Line({20, 20, 0}, {40, 20, 0}), Line({40, 20, 0}, {40, 40, 0}),
        Line({40, 40, 0}, {20, 40, 0}), Line({20, 40, 0}, {20, 20, 0})});
    const auto panel = BuildPlanarPanel(request, kTolerance);
    Require(panel.HasValue(), "作れる");
    const auto curves = PlacePanelCurves(panel.Value(), PatternPlacement{});
    Require(curves.HasValue(), "置ける");
    int outline = 0;
    int opening = 0;
    for (const auto& curve : curves.Value()) {
        if (curve.layer == kachakacha::v2::exporters::PatternLine::Outline) {
            ++outline;
        }
        if (curve.layer == kachakacha::v2::exporters::PatternLine::Opening) {
            ++opening;
        }
    }
    Require(outline > 0, "外周がある");
    Require(opening > 0, "開口がある");
}

KACHA_V2_TEST(planar_panel, 開口はそれが載っている壁のものになる)
{
    // 窓は、それが描かれている壁のものである。人に選ばせる必要はない。
    std::vector<PlanarPanelRequest> requests;
    PlanarPanelRequest floor;
    floor.panelId = "床";
    floor.boundary = RectangleAt(0.0, 0.0, 40.0, 20.0, 0.0);
    requests.push_back(floor);
    PlanarPanelRequest wall;
    wall.panelId = "壁";
    // z = 10 の高さにある、もう1枚。
    wall.boundary = RectangleAt(0.0, 0.0, 40.0, 20.0, 10.0);
    requests.push_back(wall);

    const auto onFloor = PanelForOpening(requests, RectangleAt(5.0, 5.0, 6.0, 4.0, 0.0),
        0.001);
    Require(onFloor.has_value() && *onFloor == 0, "床の上の穴は床のもの");
    const auto onWall = PanelForOpening(requests, RectangleAt(5.0, 5.0, 6.0, 4.0, 10.0),
        0.001);
    Require(onWall.has_value() && *onWall == 1, "壁の上の穴は壁のもの");
}

KACHA_V2_TEST(planar_panel, どの壁にも載っていない開口は断る)
{
    // 近いほうへ寄せない。寄せると、頼んでいない壁に穴が開く。
    std::vector<PlanarPanelRequest> requests;
    PlanarPanelRequest floor;
    floor.panelId = "床";
    floor.boundary = RectangleAt(0.0, 0.0, 40.0, 20.0, 0.0);
    requests.push_back(floor);
    const auto nowhere = PanelForOpening(requests, RectangleAt(5.0, 5.0, 6.0, 4.0, 7.5),
        0.001);
    Require(!nowhere.has_value(), "どこにも載っていないので決めない");
    Require(!PanelForOpening(requests, {}, 0.001).has_value(), "空は決めない");
}

KACHA_V2_TEST(planar_panel, 折り線は切る線と別の層に出る)
{
    // 折り線を切る線と同じ層に出すと、折るところが切り抜かれる。
    PlanarPanelRequest request;
    request.panelId = "側面";
    request.boundary = Rectangle();
    request.folds.push_back({Line({0, 30, 0}, {100, 30, 0})});
    request.foldIsMountain.push_back(true);
    const auto built = BuildPlanarPanel(request, kTolerance);
    Require(built.HasValue(), "作れる");
    Require(built.Value().folds.size() == 1, "折り線が1本ある");
    Require(built.Value().folds.front().sense
            == kachakacha::v2::fabrication::FoldSense::Mountain,
        "山折り");

    PatternPlacement placement;
    const auto placed = PlacePanelCurves(built.Value(), placement);
    Require(placed.HasValue(), "紙の上へ置ける");
    int foldCurves = 0;
    int outlineCurves = 0;
    for (const auto& curve : placed.Value()) {
        if (curve.layer == kachakacha::v2::exporters::PatternLine::Fold) {
            ++foldCurves;
            Require(curve.mountainFold, "山折りとして出る");
        }
        if (curve.layer == kachakacha::v2::exporters::PatternLine::Outline) {
            ++outlineCurves;
        }
    }
    // 外周は四角なので、少なくとも4本は出る
    // (標本の点が角で重なるぶん、長さ0の辺は捨てられる)。
    Require(outlineCurves >= 4, "外周が出る");
    Require(foldCurves >= 1, "折り線が出る");
}

KACHA_V2_TEST(planar_panel, 折り線は閉じない)
{
    // 閉じると、折るところが切り抜かれてしまう。
    PlanarPanelRequest request;
    request.panelId = "側面";
    request.boundary = Rectangle();
    request.folds.push_back({Line({10, 30, 0}, {90, 30, 0})});
    request.foldIsMountain.push_back(false);
    const auto built = BuildPlanarPanel(request, kTolerance);
    Require(built.HasValue(), "作れる");
    PatternPlacement placement;
    const auto placed = PlacePanelCurves(built.Value(), placement);
    Require(placed.HasValue(), "紙の上へ置ける");
    int foldCurves = 0;
    for (const auto& curve : placed.Value()) {
        if (curve.layer == kachakacha::v2::exporters::PatternLine::Fold) {
            ++foldCurves;
            Require(!curve.mountainFold, "谷折りとして出る");
        }
    }
    // 折り線は閉じないので、点の数より1本少ない。
    // 閉じていれば、点の数だけ線が出る。
    const std::size_t points = built.Value().folds.front().path.size();
    Require(foldCurves == static_cast<int>(points) - 1,
        "点の数より1本少ない(閉じていない)");
}

KACHA_V2_TEST(planar_panel, 平面から外れた折り線は断る)
{
    // 折り線も、その壁の上に無ければならない。
    PlanarPanelRequest request;
    request.panelId = "側面";
    request.boundary = Rectangle();
    request.folds.push_back({Line({0, 30, 5}, {100, 30, 5})});
    request.foldIsMountain.push_back(true);
    const auto refused = BuildPlanarPanel(request, kTolerance);
    Require(!refused.HasValue(), "断る");
}

KACHA_V2_TEST_MAIN("planar_panel_tests")
