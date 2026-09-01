#include "kachakacha/model/Surface.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using kachakacha::geometry::AlmostEqual;
using kachakacha::model::Surface;
using kachakacha::model::SurfaceKind;
using kachakacha::model::Wire;
using kachakacha::model::WireKind;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main()
{
    const Wire rectangle = Wire::Polyline({
        {0.0, 0.0, 0.0},
        {20.0, 0.0, 0.0},
        {20.0, 10.0, 0.0},
        {0.0, 10.0, 0.0},
        {0.0, 0.0, 0.0},
    });
    const Surface planar = Surface::Planar(rectangle);
    Require(planar.Kind() == SurfaceKind::Planar, "create planar surface");
    const auto planarCurvature = planar.Curvature(0.5, 0.5);
    Require(std::abs(planarCurvature.gaussian) <= 1.0e-10
            && std::abs(planarCurvature.mean) <= 1.0e-10,
        "planar surface has zero curvature");
    const auto planarProjection = planar.ProjectPointAlongDirection({7.0, 4.0, 8.0}, {0.0, 0.0, -1.0});
    Require(AlmostEqual(planarProjection.point, {7.0, 4.0, 0.0}, 1.0e-8), "project point to bounded plane");
    bool outsideRejected = false;
    try {
        static_cast<void>(planar.ProjectPointAlongDirection({25.0, 4.0, 8.0}, {0.0, 0.0, -1.0}));
    } catch (const std::invalid_argument&) {
        outsideRejected = true;
    }
    Require(outsideRejected, "reject projection outside planar boundary");

    bool crossingBoundaryRejected = false;
    try {
        (void)Surface::Planar(Wire::Polyline({
            {0.0, 0.0, 0.0},
            {10.0, 10.0, 0.0},
            {0.0, 10.0, 0.0},
            {10.0, 0.0, 0.0},
            {0.0, 0.0, 0.0},
        }));
    } catch (const std::invalid_argument&) {
        crossingBoundaryRejected = true;
    }
    Require(crossingBoundaryRejected,
        "self-crossing planar boundary is rejected explicitly");

    const Wire sectionA = Wire::CubicBezier(
        {0.0, -6.0, 0.0}, {0.0, -2.0, 3.0}, {0.0, 2.0, 3.0}, {0.0, 6.0, 0.0});
    const Wire sectionB = Wire::CubicBezier(
        {12.0, -6.0, 0.0}, {12.0, -2.0, 5.0}, {12.0, 2.0, 5.0}, {12.0, 6.0, 0.0});
    const Surface ruled = Surface::Ruled(sectionA, sectionB.Reversed());
    Require(ruled.Kind() == SurfaceKind::Ruled, "create ruled section surface");
    Require(AlmostEqual(ruled.Evaluate(0.0, 1.0), sectionB.Start(), 1.0e-8), "automatically orient second section");

    const Wire sectionMiddle = Wire::CubicBezier(
        {6.0, -6.0, 0.0}, {6.0, -2.0, 6.0}, {6.0, 2.0, 6.0}, {6.0, 6.0, 0.0});
    const Surface loft = Surface::Loft({sectionA, sectionMiddle.Reversed(), sectionB});
    Require(loft.Kind() == SurfaceKind::Loft, "create multi-section loft surface");
    Require(AlmostEqual(loft.Evaluate(0.0, 0.5), sectionMiddle.Start(), 1.0e-8), "loft keeps selected section order");
    Require(AlmostEqual(loft.Evaluate(1.0, 1.0), sectionB.End(), 1.0e-8), "loft reaches last section");
    const double loftStep = 1.0e-4;
    const auto middlePoint = loft.Evaluate(0.5, 0.5);
    const auto beforeDirection = (middlePoint - loft.Evaluate(0.5, 0.5 - loftStep)).Normalized();
    const auto afterDirection = (loft.Evaluate(0.5, 0.5 + loftStep) - middlePoint).Normalized();
    Require(kachakacha::geometry::Dot(beforeDirection, afterDirection) > 0.99999,
        "loft keeps a continuous direction through section planes");
    const auto secondBefore = (
        loft.Evaluate(0.5, 0.5)
        - loft.Evaluate(0.5, 0.5 - loftStep) * 2.0
        + loft.Evaluate(0.5, 0.5 - loftStep * 2.0))
        / (loftStep * loftStep);
    const auto secondAfter = (
        loft.Evaluate(0.5, 0.5 + loftStep * 2.0)
        - loft.Evaluate(0.5, 0.5 + loftStep) * 2.0
        + loft.Evaluate(0.5, 0.5))
        / (loftStep * loftStep);
    Require((secondBefore - secondAfter).Length() < 0.1,
        "loft keeps C2 curvature continuity through section planes");
    const auto loftCurvature = loft.Curvature(0.5, 0.5);
    Require(std::isfinite(loftCurvature.gaussian)
            && std::isfinite(loftCurvature.mean)
            && loftCurvature.principalMinimum <= loftCurvature.principalMaximum,
        "loft reports finite ordered principal curvatures");

    const Wire firstGuide = Wire::CubicBezier(
        {0.0, -6.0, 0.0}, {4.0, -6.0, 1.0},
        {8.0, -6.0, 2.0}, {12.0, -6.0, 0.0});
    const Wire secondGuide = Wire::CubicBezier(
        {0.0, 6.0, 0.0}, {4.0, 6.0, 1.5},
        {8.0, 6.0, 2.5}, {12.0, 6.0, 0.0});
    const Wire guidedSection = Wire::CubicBezier(
        firstGuide.Evaluate(0.5), {6.0, -2.0, 7.0},
        {6.0, 2.0, 7.0}, secondGuide.Evaluate(0.5));
    const Surface guided = Surface::GuidedLoft(
        firstGuide, secondGuide.Reversed(), {guidedSection.Reversed()});
    Require(guided.Kind() == SurfaceKind::GuidedLoft,
        "create guided loft from two profiles and one cross section");
    Require(AlmostEqual(guided.Evaluate(0.0, 0.5), firstGuide.Evaluate(0.5), 1.0e-6),
        "guided loft follows first outer contour");
    Require(AlmostEqual(guided.Evaluate(1.0, 0.5), secondGuide.Evaluate(0.5), 1.0e-6),
        "guided loft follows second outer contour");
    Require(AlmostEqual(guided.Evaluate(0.5, 0.5), guidedSection.Evaluate(0.5), 1.0e-6),
        "guided loft passes through selected cross section");

    const Wire joinedEndFirstGuide = Wire::CubicBezier(
        {0.0, 0.0, 0.0}, {10.0 / 3.0, 5.0, 0.0},
        {20.0 / 3.0, 5.0, 0.0}, {10.0, 0.0, 0.0});
    const Wire joinedEndSecondGuide = Wire::Line(
        {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    const Wire joinedEndSectionA = Wire::Line(
        joinedEndFirstGuide.Evaluate(0.3),
        joinedEndSecondGuide.Evaluate(0.3));
    const Wire joinedEndSectionB = Wire::Line(
        joinedEndFirstGuide.Evaluate(0.7),
        joinedEndSecondGuide.Evaluate(0.7));
    const Surface joinedEndGuided = Surface::GuidedLoft(
        joinedEndFirstGuide, joinedEndSecondGuide,
        {joinedEndSectionA, joinedEndSectionB});
    Require(AlmostEqual(
                joinedEndGuided.Evaluate(0.0, 0.0),
                joinedEndGuided.Evaluate(1.0, 0.0), 1.0e-8)
            && AlmostEqual(
                joinedEndGuided.Evaluate(0.0, 1.0),
                joinedEndGuided.Evaluate(1.0, 1.0), 1.0e-8),
        "guided loft permits two guide rows that meet at both ends");
    Require(AlmostEqual(
                joinedEndGuided.Evaluate(0.5, 0.3),
                joinedEndSectionA.Evaluate(0.5), 1.0e-6)
            && AlmostEqual(
                joinedEndGuided.Evaluate(0.5, 0.7),
                joinedEndSectionB.Evaluate(0.5), 1.0e-6),
        "guided loft sections may touch intermediate points of joined guides");

    const Wire lightDrawing = Wire::Circle({6.0, 0.0, 12.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1.25);
    const Wire projectedLight = ruled.ProjectWireAlongDirection(lightDrawing, {0.0, 0.0, -1.0});
    Require(projectedLight.Kind() == WireKind::Polyline && projectedLight.IsClosed(), "project closed light drawing to surface");
    for (const auto& point : projectedLight.ControlPoints()) {
        const auto check = ruled.ProjectPointAlongDirection({point.x, point.y, 12.0}, {0.0, 0.0, -1.0});
        Require(std::abs(check.point.z - point.z) <= 1.0e-5, "projected light points remain on surface");
    }

    const Wire loftProjectedLight = loft.ProjectWireAlongDirection(lightDrawing, {0.0, 0.0, -1.0});
    Require(loftProjectedLight.IsClosed(), "project light drawing to loft surface");

    // --- Gordon面: 断面とガイド(外形)を厳密に通る ---
    const auto pointA = sectionA.Evaluate(0.5);
    const auto pointM = sectionMiddle.Evaluate(0.5);
    const auto pointB = sectionB.Evaluate(0.5);
    const kachakacha::geometry::Vector3 bulgeAM{3.0, 0.0, 6.0};
    const kachakacha::geometry::Vector3 bulgeMB{9.0, 0.0, 6.0};
    const Wire ridgeGuide = Wire::Polyline({pointA, bulgeAM, pointM, bulgeMB, pointB});

    const Surface gordon = Surface::Gordon({sectionA, sectionMiddle, sectionB}, {ridgeGuide});
    Require(gordon.Kind() == SurfaceKind::Gordon, "create gordon surface");
    Require(gordon.MaximumGuideGap() <= 1.0e-5, "gordon guide touches all sections");

    // 断面は厳密に通る: 各断面パラメータ v_i で端点が一致し、中間uでも断面曲線上に乗る。
    Require(AlmostEqual(gordon.Evaluate(0.0, 0.0), sectionA.Start(), 1.0e-9), "gordon keeps first section start");
    Require(AlmostEqual(gordon.Evaluate(1.0, 0.0), sectionA.End(), 1.0e-9), "gordon keeps first section end");
    Require(AlmostEqual(gordon.Evaluate(0.0, 1.0), sectionB.Start(), 1.0e-9), "gordon keeps last section start");
    Require(AlmostEqual(gordon.Evaluate(1.0, 1.0), sectionB.End(), 1.0e-9), "gordon keeps last section end");
    for (int sample = 0; sample <= 16; ++sample) {
        const double u = static_cast<double>(sample) / 16.0;
        const auto onMiddle = gordon.Evaluate(u, 0.5);
        double closest = 1.0e9;
        for (int scan = 0; scan <= 4096; ++scan) {
            closest = std::min(closest,
                (onMiddle - sectionMiddle.Evaluate(static_cast<double>(scan) / 4096.0)).Length());
        }
        Require(closest <= 1.0e-4, "gordon stays exactly on the middle section");
    }

    // ガイドも厳密に通る: ガイドは各断面中央(u=0.5)を通るので、u=0.5 の等パラメータ線がガイドに一致する。
    for (int sample = 0; sample <= 8; ++sample) {
        const double v = static_cast<double>(sample) / 8.0;
        const auto onGuide = gordon.Evaluate(0.5, v);
        const auto guidePoint = ridgeGuide.Evaluate(v);
        Require((onGuide - guidePoint).Length() <= 1.0e-3, "gordon follows the guide between sections");
    }
    // 普通のロフトはこの外形を通らない(機能差の確認)。
    Require((loft.Evaluate(0.5, 0.25) - bulgeAM).Length() > 0.5,
        "plain loft does not follow the ridge guide");
    Require((gordon.Evaluate(0.5, 0.25) - bulgeAM).Length() <= 1.0e-3,
        "gordon passes through the drawn ridge point");

    // 2断面+ガイドでも作成できる(ロフトは3断面必要だった)。
    const Wire straightGuide = Wire::Polyline({pointA, pointB});
    const Surface twoSectionGordon = Surface::Gordon({sectionA, sectionB}, {straightGuide});
    const kachakacha::geometry::Vector3 straightMiddle{6.0, 0.0, 3.0};
    Require((twoSectionGordon.Evaluate(0.5, 0.5) - straightMiddle).Length() <= 1.0e-3,
        "two-section gordon follows a straight guide");

    // 断面に触れないガイドは明確に拒否する。
    bool detachedGuideRejected = false;
    try {
        const Wire floatingGuide = Wire::Polyline({{0.0, 0.0, 20.0}, {12.0, 0.0, 20.0}});
        static_cast<void>(Surface::Gordon({sectionA, sectionMiddle, sectionB}, {floatingGuide}));
    } catch (const std::invalid_argument&) {
        detachedGuideRejected = true;
    }
    Require(detachedGuideRejected, "reject a guide that does not touch the sections");

    std::vector<kachakacha::geometry::Vector3> lightSamples;
    for (int sample = 0; sample <= 64; ++sample) {
        lightSamples.push_back(lightDrawing.Evaluate(
            static_cast<double>(sample) / 64.0));
    }
    const auto trackedProjections = loft.ProjectPointsAlongDirection(
        lightSamples, {0.0, 0.0, -1.0});
    Require(trackedProjections.size() == lightSamples.size(),
        "track all neighboring projection samples");
    Require(AlmostEqual(
            trackedProjections.front().point,
            trackedProjections.back().point,
            1.0e-5),
        "tracked projection preserves a closed curve");

    std::cout << "surface tests passed\n";
    return EXIT_SUCCESS;
}
