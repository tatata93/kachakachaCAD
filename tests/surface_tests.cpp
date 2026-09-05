#include "kachakacha/io/ProjectScript.h"
#include "kachakacha/model/Project.h"
#include "kachakacha/model/Surface.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

using kachakacha::geometry::AlmostEqual;
using kachakacha::geometry::Vector3;
using kachakacha::model::NamedWire;
using kachakacha::model::PlateThicknessDirection;
using kachakacha::model::Project;
using kachakacha::model::ProjectObjectKind;
using kachakacha::model::Surface;
using kachakacha::model::SurfaceKind;
using kachakacha::model::Wire;
using kachakacha::model::WireKind;
using kachakacha::model::WireMetadata;
using kachakacha::model::WirePlanePolicy;
using kachakacha::model::WorkPlane;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

const NamedWire& RequireWire(const Project& project, const std::string& name)
{
    const auto found = std::find_if(
        project.Wires().begin(), project.Wires().end(),
        [&](const NamedWire& wire) {
            return wire.name == name;
        });
    if (found == project.Wires().end()) {
        throw std::runtime_error("test wire is missing: " + name);
    }
    return *found;
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

    {
        Project project;
        project.AddWire("ruled_move_a", Wire::Line({0.0, 0.0, 0.0}, {0.0, 8.0, 0.0}));
        project.AddWire("ruled_move_b", Wire::Line({10.0, 0.0, 2.0}, {10.0, 8.0, 2.0}));
        project.AddRuledSurface("ruled_move_surface", "ruled_move_a", "ruled_move_b");

        const Surface beforeSurface = *project.FindSurface("ruled_move_surface");
        const Vector3 beforePoint = beforeSurface.Evaluate(0.35, 0.4);
        const Vector3 beforeWireStart = RequireWire(project, "ruled_move_a").wire.Start();
        const Vector3 delta{2.0, -3.0, 5.0};

        const int moved = project.TranslateObjects(
            {{ProjectObjectKind::Surface, "ruled_move_surface"}}, delta);

        const Surface afterSurface = *project.FindSurface("ruled_move_surface");
        Require(moved == 2, "surface translation moves two source wires");
        Require(AlmostEqual(
                afterSurface.Evaluate(0.35, 0.4), beforePoint + delta, 1.0e-8),
            "surface translation rebuilds the ruled surface");
        Require(AlmostEqual(
                RequireWire(project, "ruled_move_a").wire.Start(),
                beforeWireStart + delta,
                1.0e-8),
            "surface translation moves the source wire");
    }

    {
        Project project;
        project.AddWire("plate_move_a", Wire::Line({0.0, 0.0, 0.0}, {0.0, 6.0, 0.0}));
        project.AddWire("plate_move_b", Wire::Line({12.0, 0.0, 1.0}, {12.0, 6.0, 1.0}));
        project.AddRuledSurface("plate_move_surface", "plate_move_a", "plate_move_b");
        project.AddPlate(
            "plate_move_plate",
            "plate_move_surface",
            0.5,
            PlateThicknessDirection::Positive,
            "plastic");

        const Vector3 beforeSurfacePoint =
            project.FindSurface("plate_move_surface")->Evaluate(0.25, 0.75);
        const Vector3 beforePlatePoint =
            project.FindPlate("plate_move_plate")->Evaluate(0.25, 0.75, 0.5);
        const Vector3 beforeWireStart = RequireWire(project, "plate_move_b").wire.Start();
        const Vector3 delta{-1.0, 4.0, 3.0};

        const int moved = project.TranslateObjects(
            {{ProjectObjectKind::Plate, "plate_move_plate"}}, delta);

        Require(moved == 2, "plate translation moves source surface wires");
        Require(AlmostEqual(
                project.FindSurface("plate_move_surface")->Evaluate(0.25, 0.75),
                beforeSurfacePoint + delta,
                1.0e-8),
            "plate translation rebuilds the source surface");
        Require(AlmostEqual(
                project.FindPlate("plate_move_plate")->Evaluate(0.25, 0.75, 0.5),
                beforePlatePoint + delta,
                1.0e-8),
            "plate translation rebuilds the plate");
        Require(AlmostEqual(
                RequireWire(project, "plate_move_b").wire.Start(),
                beforeWireStart + delta,
                1.0e-8),
            "plate translation moves the source wire");
    }

    {
        Project project;
        project.AddWorkPlane(
            "paper",
            WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}));
        WireMetadata metadata;
        metadata.sourcePlaneName = std::string("paper");
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        project.AddWire("paper_line", Wire::Line({1.0, 2.0, 0.0}, {5.0, 2.0, 0.0}), metadata);

        const Vector3 beforeOrigin = project.FindWorkPlane("paper")->Origin();
        const Vector3 beforeWireStart = RequireWire(project, "paper_line").wire.Start();
        const Vector3 delta{3.0, 4.0, 5.0};

        const int moved = project.TranslateObjects({{ProjectObjectKind::WorkPlane, "paper"}}, delta);

        Require(moved == 2, "work-plane translation moves the plane and its source wires");
        Require(AlmostEqual(project.FindWorkPlane("paper")->Origin(), beforeOrigin + delta, 1.0e-8),
            "work-plane translation moves the plane");
        Require(AlmostEqual(
                RequireWire(project, "paper_line").wire.Start(),
                beforeWireStart + delta,
                1.0e-8),
            "work-plane translation moves wires drawn on the plane");
    }

    {
        Project project;
        project.AddWire("projection_boundary", Wire::Polyline({
            {0.0, 0.0, 0.0},
            {10.0, 0.0, 0.0},
            {10.0, 10.0, 0.0},
            {0.0, 10.0, 0.0},
            {0.0, 0.0, 0.0},
        }));
        project.AddPlanarSurface("projection_panel", "projection_boundary");
        project.AddWire("projection_draft",
            Wire::Circle({5.0, 5.0, 8.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1.0));
        project.AddProjectedWire(
            "projection_wire", "projection_draft", "projection_panel", {0.0, 0.0, -1.0});

        bool projectedRejected = false;
        try {
            static_cast<void>(project.TranslateObjects(
                {{ProjectObjectKind::Wire, "projection_wire"}}, {1.0, 0.0, 0.0}));
        } catch (const std::invalid_argument& error) {
            const std::string message = error.what();
            projectedRejected = message.find("projection_wire") != std::string::npos
                && message.find("元(下書き・板材・近似モデル)側") != std::string::npos;
        }
        Require(projectedRejected, "reject direct translation of projected wire");
    }

    {
        Project project;
        project.AddWire("duplicate_a", Wire::Line({0.0, 0.0, 0.0}, {0.0, 4.0, 0.0}));
        project.AddWire("duplicate_b", Wire::Line({8.0, 0.0, 0.0}, {8.0, 4.0, 0.0}));
        project.AddRuledSurface("duplicate_surface", "duplicate_a", "duplicate_b");

        const Vector3 beforeWireStart = RequireWire(project, "duplicate_a").wire.Start();
        const Vector3 beforeSurfacePoint =
            project.FindSurface("duplicate_surface")->Evaluate(0.2, 0.6);
        const Vector3 delta{0.0, 0.0, 7.0};

        const int moved = project.TranslateObjects({
            {ProjectObjectKind::Surface, "duplicate_surface"},
            {ProjectObjectKind::Wire, "duplicate_a"},
        }, delta);

        Require(moved == 2, "duplicate translation targets are counted once");
        Require(AlmostEqual(
                RequireWire(project, "duplicate_a").wire.Start(),
                beforeWireStart + delta,
                1.0e-8),
            "duplicate translation target moves once");
        Require(AlmostEqual(
                project.FindSurface("duplicate_surface")->Evaluate(0.2, 0.6),
                beforeSurfacePoint + delta,
                1.0e-8),
            "duplicate translation rebuilds the surface once");
    }

    {
        // --- 回転(ギズモ用の中核): 面を軸まわりに回すと元ワイヤごと回る ---
        Project project;
        project.AddWire("rotate_a", Wire::Line({10.0, 0.0, 0.0}, {10.0, 6.0, 0.0}));
        project.AddWire("rotate_b", Wire::Line({20.0, 0.0, 3.0}, {20.0, 6.0, 3.0}));
        project.AddRuledSurface("rotate_surface", "rotate_a", "rotate_b");

        const Vector3 axisPoint{0.0, 0.0, 0.0};
        const Vector3 axisDirection{0.0, 0.0, 1.0};
        const double quarterTurn = 3.14159265358979323846 / 2.0;
        const int rotated = project.RotateObjects(
            {{ProjectObjectKind::Surface, "rotate_surface"}},
            axisPoint, axisDirection, quarterTurn);

        Require(rotated == 2, "surface rotation rotates two source wires");
        // (10,0,0) を z 軸まわりに +90° → (0,10,0)。
        Require(AlmostEqual(
                RequireWire(project, "rotate_a").wire.Start(),
                Vector3{0.0, 10.0, 0.0},
                1.0e-8),
            "surface rotation rotates the source wire start");
        Require(AlmostEqual(
                RequireWire(project, "rotate_b").wire.End(),
                Vector3{-6.0, 20.0, 3.0},
                1.0e-8),
            "surface rotation keeps the axis-parallel coordinate");
        // 面自体も作り直されて回った位置にある。
        Require(AlmostEqual(
                project.FindSurface("rotate_surface")->Evaluate(0.0, 0.0),
                Vector3{0.0, 10.0, 0.0},
                1.0e-8),
            "surface rotation rebuilds the ruled surface");
    }

    {
        // 作業平面の回転: 平面と平面上のワイヤが一緒に回る。
        Project project;
        project.AddWorkPlane(
            "rotate_paper",
            WorkPlane::FromPointNormal({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}));
        WireMetadata metadata;
        metadata.sourcePlaneName = std::string("rotate_paper");
        metadata.planePolicy = WirePlanePolicy::ReferenceOnly;
        project.AddWire(
            "rotate_paper_line", Wire::Line({4.0, 0.0, 0.0}, {8.0, 0.0, 0.0}), metadata);

        const int rotated = project.RotateObjects(
            {{ProjectObjectKind::WorkPlane, "rotate_paper"}},
            {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 3.14159265358979323846);

        Require(rotated == 2, "work-plane rotation rotates the plane and its wires");
        Require(AlmostEqual(
                RequireWire(project, "rotate_paper_line").wire.Start(),
                Vector3{-4.0, 0.0, 0.0},
                1.0e-8),
            "work-plane rotation rotates wires drawn on the plane");
    }

    {
        // 派生(投影ワイヤ)は回転も拒否する(移動と同じ収集規則)。
        Project project;
        project.AddWire("rotate_boundary", Wire::Polyline({
            {0.0, 0.0, 0.0},
            {10.0, 0.0, 0.0},
            {10.0, 10.0, 0.0},
            {0.0, 10.0, 0.0},
            {0.0, 0.0, 0.0},
        }));
        project.AddPlanarSurface("rotate_panel", "rotate_boundary");
        project.AddWire("rotate_draft",
            Wire::Circle({5.0, 5.0, 8.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1.0));
        project.AddProjectedWire(
            "rotate_projection", "rotate_draft", "rotate_panel", {0.0, 0.0, -1.0});

        bool rejected = false;
        try {
            static_cast<void>(project.RotateObjects(
                {{ProjectObjectKind::Wire, "rotate_projection"}},
                {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.5));
        } catch (const std::invalid_argument& error) {
            const std::string message = error.what();
            rejected = message.find("rotate_projection") != std::string::npos;
        }
        Require(rejected, "reject direct rotation of projected wire");

        bool zeroAxisRejected = false;
        try {
            static_cast<void>(project.RotateObjects(
                {{ProjectObjectKind::Wire, "rotate_draft"}},
                {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 0.5));
        } catch (const std::invalid_argument&) {
            zeroAxisRejected = true;
        }
        Require(zeroAxisRejected, "reject a zero-length rotation axis");
    }

    {
        // --- おまかせ面(オーナー指示): 選択順・向き不問で面が作れる ---
        Project project;
        // 閉じた長方形(4本、順序も向きもバラバラ) → 平面。
        project.AddWire("自動枠右", Wire::Line({20.0, 0.0, 0.0}, {20.0, 10.0, 0.0}));
        project.AddWire("自動枠下", Wire::Line({0.0, 0.0, 0.0}, {20.0, 0.0, 0.0}));
        project.AddWire("自動枠上", Wire::Line({0.0, 10.0, 0.0}, {20.0, 10.0, 0.0}));
        project.AddWire("自動枠左", Wire::Line({0.0, 10.0, 0.0}, {0.0, 0.0, 0.0}));
        const std::string planarNote = project.AddAutoSurface(
            "自動平面", {"自動枠右", "自動枠上", "自動枠下", "自動枠左"});
        Require(project.Surfaces().back().surface.Kind() == SurfaceKind::Planar,
            "auto surface picks a planar surface for a closed flat loop");
        Require(project.Surfaces().back().autoAssembled,
            "auto surface is marked as auto assembled");
        Require(planarNote.find("平面") != std::string::npos,
            "auto surface describes the planar result");

        // 閉じているが平面ではない4本 → パッチ面(穴埋め)。角が境界上に載る。
        project.AddWire("山下", Wire::Line({0.0, 0.0, 20.0}, {20.0, 0.0, 20.0}));
        project.AddWire("山右", Wire::CircularArcThroughThreePoints(
            {20.0, 0.0, 20.0}, {22.0, 5.0, 26.0}, {20.0, 10.0, 20.0}));
        project.AddWire("山上", Wire::Line({20.0, 10.0, 20.0}, {0.0, 10.0, 20.0}));
        project.AddWire("山左", Wire::CircularArcThroughThreePoints(
            {0.0, 10.0, 20.0}, {-2.0, 5.0, 26.0}, {0.0, 0.0, 20.0}));
        (void)project.AddAutoSurface("自動パッチ", {"山上", "山下", "山左", "山右"});
        const auto& patch = project.Surfaces().back();
        Require(patch.surface.Kind() == SurfaceKind::Patch,
            "auto surface falls back to a patch for a non-planar loop");
        bool cornerOnBoundary = false;
        const Vector3 corner = patch.surface.Evaluate(0.0, 0.0);
        for (int sample = 0; sample <= 256; ++sample) {
            if ((patch.surface.FirstBoundary().Evaluate(sample / 256.0) - corner)
                    .Length() < 0.5) {
                cornerOnBoundary = true;
                break;
            }
        }
        Require(cornerOnBoundary, "patch corner lies on the boundary loop");

        // 3本の断面を順序も向きもバラバラに → 自動整列ロフト。
        project.AddWire("断面中", Wire::CircularArcThroughThreePoints(
            {-24.0, 0.0, 70.0}, {0.0, 20.0, 70.0}, {24.0, 0.0, 70.0}));
        project.AddWire("断面奥", Wire::CircularArcThroughThreePoints(
            {20.0, 0.0, 90.0}, {0.0, 16.0, 90.0}, {-20.0, 0.0, 90.0})); // 逆向き
        project.AddWire("断面手前", Wire::CircularArcThroughThreePoints(
            {-30.0, 0.0, 50.0}, {0.0, 26.0, 50.0}, {30.0, 0.0, 50.0}));
        const std::string loftNote = project.AddAutoSurface(
            "自動ロフト", {"断面中", "断面奥", "断面手前"});
        const auto& loft = project.Surfaces().back();
        Require(loft.surface.Kind() == SurfaceKind::Loft,
            "auto surface lofts three open sections");
        Require(loftNote.find("自動整列") != std::string::npos,
            "auto surface reports the automatic ordering");
        // v=0/1 が手前(z=50)と奥(z=90)の断面になっている(順序が整った)。
        const double lowZ = loft.surface.Evaluate(0.5, 0.0).z;
        const double highZ = loft.surface.Evaluate(0.5, 1.0).z;
        Require(std::abs(std::min(lowZ, highZ) - 50.0) < 1.0
                && std::abs(std::max(lowZ, highZ) - 90.0) < 1.0,
            "auto loft orders sections along their axis");
        // 向きが揃っている(v方向の素線がねじれていない): u=0 の両端が同じ側。
        const Vector3 uZeroLow = loft.surface.Evaluate(0.0, 0.0);
        const Vector3 uZeroHigh = loft.surface.Evaluate(0.0, 1.0);
        Require((uZeroLow - uZeroHigh).Length()
                < (uZeroLow - loft.surface.Evaluate(1.0, 1.0)).Length(),
            "auto loft aligns section directions");

        // 2本(片方は2線に分かれ、0.6mmの隙間あり) → 連結+ルールド。
        project.AddWire("縁A1", Wire::Line({100.0, 0.0, 0.0}, {110.0, 0.0, 0.0}));
        project.AddWire("縁A2", Wire::Line({110.6, 0.0, 0.0}, {120.0, 0.0, 0.0}));
        project.AddWire("縁B", Wire::Line({100.0, 20.0, 4.0}, {120.0, 20.0, 4.0}));
        const std::string ruledNote = project.AddAutoSurface(
            "自動ルールド", {"縁B", "縁A1", "縁A2"});
        Require(project.Surfaces().back().surface.Kind() == SurfaceKind::Ruled,
            "auto surface joins chains with a small gap into a ruled surface");
        Require(ruledNote.find("隙間") != std::string::npos,
            "auto surface reports the bridged gap");

        // 保存 → 読込で同じ手順で作り直される。
        std::ostringstream saved;
        kachakacha::io::WriteProjectScript(saved, project);
        Require(saved.str().find("surface_auto 自動パッチ 4") != std::string::npos,
            "auto surface is saved as surface_auto");
        std::istringstream input(saved.str());
        Project loaded = kachakacha::io::LoadProjectScript(input, "auto-surface");
        Require(loaded.FindSurface("自動パッチ").has_value()
                && loaded.FindSurface("自動パッチ")->Kind() == SurfaceKind::Patch,
            "auto surface survives the script round trip");

        // 元の線を編集すると追従して作り直される(0.8mm持ち上げ→隙間は自動で
        // 閉じたまま、平面ではなくなるのでパッチへ再判定される)。
        const Vector3 beforeEdit = project.FindSurface("自動平面")->Evaluate(0.5, 0.5);
        project.UpdateWire("自動枠上",
            Wire::Line({0.0, 10.0, 0.8}, {20.0, 10.0, 0.8}));
        const Vector3 afterEdit = project.FindSurface("自動平面")->Evaluate(0.5, 0.5);
        Require((afterEdit - beforeEdit).Length() > 0.2,
            "auto surface follows source wire edits");
        Require(project.FindSurface("自動平面")->Kind() == SurfaceKind::Patch,
            "auto surface re-plans its kind after edits");

        // 閉じない選択は理由つきで拒否される。
        bool guarded = false;
        try {
            (void)project.AddAutoSurface("自動失敗", {"縁B"});
        } catch (const std::invalid_argument& error) {
            guarded = std::string(error.what()).find("閉じていません") != std::string::npos;
        }
        Require(guarded, "auto surface explains why it cannot build");
    }

    {
        // --- おまかせ面は選んだ線を必ず通る(オーナー指示) ---
        Project project;
        // ルールド: v=0/1 の縁が入力ワイヤそのもの。
        const Wire lowArc = Wire::CircularArcThroughThreePoints(
            {-20.0, 0.0, 0.0}, {0.0, 14.0, 0.0}, {20.0, 0.0, 0.0});
        const Wire highArc = Wire::CircularArcThroughThreePoints(
            {-20.0, 0.0, 30.0}, {0.0, 14.0, 30.0}, {20.0, 0.0, 30.0});
        project.AddWire("通過下", lowArc);
        project.AddWire("通過上", highArc);
        (void)project.AddAutoSurface("通過ルールド", {"通過下", "通過上"});
        const Surface& ruled = project.Surfaces().back().surface;
        for (int sample = 0; sample <= 8; ++sample) {
            const double u = static_cast<double>(sample) / 8.0;
            Require(AlmostEqual(ruled.Evaluate(u, 0.0), lowArc.Evaluate(u), 1.0e-9),
                "auto ruled surface passes through the first wire exactly");
            Require(AlmostEqual(ruled.Evaluate(u, 1.0), highArc.Evaluate(u), 1.0e-9),
                "auto ruled surface passes through the second wire exactly");
        }

        // ロフト: 中間断面も v=0.5 でそのまま通る(スプラインは節点を補間する)。
        const Wire midArc = Wire::CircularArcThroughThreePoints(
            {-20.0, 0.0, 15.0}, {0.0, 18.0, 15.0}, {20.0, 0.0, 15.0});
        project.AddWire("通過中", midArc);
        (void)project.AddAutoSurface("通過ロフト", {"通過下", "通過中", "通過上"});
        const Surface& loftThrough = project.Surfaces().back().surface;
        for (int sample = 0; sample <= 8; ++sample) {
            const double u = static_cast<double>(sample) / 8.0;
            Require(AlmostEqual(
                    loftThrough.Evaluate(u, 0.5), midArc.Evaluate(u), 1.0e-9),
                "auto loft passes through the middle wire exactly");
        }

        // わずかに平面から外れた輪郭は平面へ丸めず、パッチで正確に通す。
        project.AddWire("浮き下", Wire::Line({50.0, 0.0, 0.0}, {70.0, 0.0, 0.0}));
        project.AddWire("浮き右", Wire::Line({70.0, 0.0, 0.0}, {70.0, 12.0, 0.1}));
        project.AddWire("浮き上", Wire::Line({70.0, 12.0, 0.1}, {50.0, 12.0, 0.0}));
        project.AddWire("浮き左", Wire::Line({50.0, 12.0, 0.0}, {50.0, 0.0, 0.0}));
        (void)project.AddAutoSurface("浮き輪郭",
            {"浮き下", "浮き右", "浮き上", "浮き左"});
        const auto& lifted = project.Surfaces().back();
        Require(lifted.surface.Kind() == SurfaceKind::Patch,
            "slightly non-planar loop is not flattened into a plane");
        const Vector3 liftedCorner{70.0, 12.0, 0.1};
        double nearestBoundary = std::numeric_limits<double>::max();
        for (int sample = 0; sample <= 400; ++sample) {
            const double t = static_cast<double>(sample) / 400.0;
            nearestBoundary = std::min({nearestBoundary,
                (lifted.surface.Evaluate(t, 0.0) - liftedCorner).Length(),
                (lifted.surface.Evaluate(t, 1.0) - liftedCorner).Length(),
                (lifted.surface.Evaluate(0.0, t) - liftedCorner).Length(),
                (lifted.surface.Evaluate(1.0, t) - liftedCorner).Length()});
        }
        // 境界そのものは輪郭ワイヤを正確に通る。走査サンプルが頂点の
        // パラメータへぴったり乗るとは限らないため、判定は走査間隔ぶんだけ緩める
        // (旧実装は平面へ丸めて 0.1mm 浮いた頂点を通らなかった)。
        Require(nearestBoundary < 0.05,
            "patch boundary passes through the lifted corner");
    }

    {
        // --- ねじれ防止(オーナー指示): 巻きもシームも食い違う円2つ → まっすぐな筒 ---
        Project project;
        project.AddWire("筒下", Wire::Circle(
            {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 10.0));
        // 上の円は逆巻き+シームが90度ずれている。
        project.AddWire("筒上", Wire::Circle(
            {0.0, 0.0, 20.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, 10.0));
        (void)project.AddAutoSurface("筒", {"筒上", "筒下"});
        const Surface& tube = project.Surfaces().back().surface;
        for (int sample = 0; sample <= 32; ++sample) {
            const double u = static_cast<double>(sample) / 32.0;
            const Vector3 bottom = tube.Evaluate(u, 0.0);
            const Vector3 top = tube.Evaluate(u, 1.0);
            const Vector3 ruling = top - bottom;
            Require(std::abs(std::abs(ruling.z) - 20.0) < 1.0e-6
                    && std::abs(ruling.x) < 1.0e-6 && std::abs(ruling.y) < 1.0e-6,
                "circle sections build an untwisted straight tube");
        }

        // 円のReversedは巻きだけ反転する(以前は同じ円を返すバグ)。
        const Wire circle = Wire::Circle(
            {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 5.0);
        const Wire reversedCircle = circle.Reversed();
        Require(AlmostEqual(reversedCircle.Start(), circle.Start(), 1.0e-9),
            "reversed circle keeps its seam");
        Require(AlmostEqual(
                reversedCircle.Evaluate(0.25), circle.Evaluate(0.75), 1.0e-9),
            "reversed circle runs the opposite way");
    }

    std::cout << "surface tests passed\n";
    return EXIT_SUCCESS;
}
