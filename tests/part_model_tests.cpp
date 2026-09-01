#include "kachakacha/io/PartPatterns.h"
#include "kachakacha/io/ProjectScript.h"
#include "kachakacha/model/PartModel.h"
#include "kachakacha/model/Project.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

using kachakacha::geometry::Vector3;
using kachakacha::model::ObjectSetState;
using kachakacha::model::PartApproximationOptions;
using kachakacha::model::PartSplitAxis;
using kachakacha::model::PlateThicknessDirection;
using kachakacha::model::Project;
using kachakacha::model::ProjectObjectKind;
using kachakacha::model::Wire;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// ペットボトルの肩のような「上がすぼまり中央が膨らむ」ロフト面の断面を作る。
// 断面はz=一定の半円弧(前面側)で、半径が高さで変わる。
Wire SectionArc(double radiusMillimeters, double heightMillimeters)
{
    return Wire::CircularArcThroughThreePoints(
        {-radiusMillimeters, 0.0, heightMillimeters},
        {0.0, radiusMillimeters, heightMillimeters},
        {radiusMillimeters, 0.0, heightMillimeters});
}

Project MakeBottleLikeProject()
{
    Project project;
    project.AddWire("断面1", SectionArc(20.0, 0.0));
    project.AddWire("断面2", SectionArc(24.0, 12.0));
    project.AddWire("断面3", SectionArc(24.0, 24.0));
    project.AddWire("断面4", SectionArc(12.0, 40.0));
    project.AddLoftSurface(
        "胴", std::vector<std::string>{"断面1", "断面2", "断面3", "断面4"});
    project.AddPlate("胴板", "胴", 0.5, PlateThicknessDirection::Centered, "プラ板");
    return project;
}

} // namespace

int main()
{
    try {
        // --- 自動分割 ---
        Project project = MakeBottleLikeProject();
        PartApproximationOptions options;
        options.splitAxis = PartSplitAxis::V;
        options.automaticBoundaries = true;
        options.maximumDeviationMillimeters = 0.4;
        options.maximumPartCount = 8;
        options.minimumPartWidthMillimeters = 3.0;
        project.AddPartModel("近似A", "胴板", options);

        Require(project.PartModels().size() == 1, "part model stored");
        const auto& model = project.PartModels().front();
        Require(model.result.parts.size() >= 2, "curved loft splits into multiple parts");
        Require(model.result.parts.size() <= 8, "part count stays within the limit");
        Require(model.result.reachedRequestedTolerance,
            "automatic split reaches the requested tolerance");
        for (const auto& part : model.result.parts) {
            Require(part.estimatedDeviationMillimeters <= 0.4 + 1.0e-9,
                "each part stays within tolerance");
            Require(part.widthMillimeters >= 3.0 - 1.0e-6, "minimum width respected");
        }
        // レール(縁+内部境界)が部材数+1本、部材面が部材数本ぶら下がる。
        Require(model.boundaryWireNames.size() == model.result.parts.size() + 1,
            "rail wires match part count");
        Require(model.partSurfaceNames.size() == model.result.parts.size(),
            "one derived surface per part");
        for (const auto& surfaceName : model.partSurfaceNames) {
            bool found = false;
            for (const auto& surface : project.Surfaces()) {
                if (surface.name == surfaceName) {
                    Require(surface.partModelSourceName.has_value(), "part surface marked derived");
                    Require(surface.surface.Kind() == kachakacha::model::SurfaceKind::Ruled,
                        "part surface is ruled (angular approximation)");
                    found = true;
                }
            }
            Require(found, "part surface exists");
        }
        std::size_t derivedCount = 0;
        for (const auto& wire : project.Wires()) {
            if (wire.partModelSourceName.has_value()) {
                Require(*wire.partModelSourceName == "近似A", "derived wire owner");
                ++derivedCount;
            }
        }
        Require(derivedCount == model.boundaryWireNames.size(), "derived wires present");
        // 自動セット。
        Require(project.ObjectSets().size() == 1, "automatic set created");
        Require(project.ObjectSets().front().name == "近似:近似A", "automatic set name");
        Require(project.ObjectSets().front().automatic, "set flagged automatic");
        Require(project.ObjectStateInSets(ProjectObjectKind::Wire, model.boundaryWireNames.front())
                == ObjectSetState::Visible,
            "set state readable through membership");

        // --- 展開の厳密さ: レールの2D長は近似形状(角ばったメッシュ)の3D長と一致する ---
        const auto results = kachakacha::io::BuildAllPartPatternsWithPreview(project, model);
        Require(results.size() == model.result.parts.size(), "one pattern per part");
        for (const auto& patternResult : results) {
            const auto& mesh = patternResult.mesh;
            Require(mesh.rows == 2, "single part has two rails");
            for (int row = 0; row < mesh.rows; ++row) {
                double length3d = 0.0;
                double length2d = 0.0;
                for (int column = 1; column < mesh.columns; ++column) {
                    length3d += (mesh.world[row][column] - mesh.world[row][column - 1]).Length();
                    const double dx = mesh.developed[row][column].x - mesh.developed[row][column - 1].x;
                    const double dy = mesh.developed[row][column].y - mesh.developed[row][column - 1].y;
                    length2d += std::sqrt(dx * dx + dy * dy);
                }
                Require(std::abs(length3d - length2d) < 1.0e-6 * std::max(1.0, length3d),
                    "development preserves rail lengths exactly");
            }
            // 横断方向(素線)の実長も保存される。
            for (int column = 0; column < mesh.columns; column += 16) {
                const double rail3d = (mesh.world[1][column] - mesh.world[0][column]).Length();
                const double dx = mesh.developed[1][column].x - mesh.developed[0][column].x;
                const double dy = mesh.developed[1][column].y - mesh.developed[0][column].y;
                const double rail2d = std::sqrt(dx * dx + dy * dy);
                Require(std::abs(rail3d - rail2d) < 1.0e-6 * std::max(1.0, rail3d),
                    "development preserves ruling lengths exactly");
            }
            Require(patternResult.pattern.analysis.maximumEdgeDistortionMillimeters == 0.0,
                "exact development reports zero edge distortion");
            Require(patternResult.pattern.foldLines.empty(),
                "single part has no crease lines");
            // 折り畳みプレビュー: 0で平面、1で近似形状に一致。
            const auto flat = kachakacha::model::BuildFoldPreview(mesh, 0.0);
            const auto folded = kachakacha::model::BuildFoldPreview(mesh, 1.0);
            Require((folded[0][0] - mesh.world[0][0]).Length() < 1.0e-9,
                "fold preview at 1 matches the approximation shape");
            double planarity = 0.0;
            const kachakacha::geometry::Vector3 origin = flat[0][0];
            const kachakacha::geometry::Vector3 e1 = flat[0][mesh.columns - 1] - origin;
            const kachakacha::geometry::Vector3 e2 = flat[1][0] - origin;
            const kachakacha::geometry::Vector3 normal = kachakacha::geometry::Cross(e1, e2);
            const double normalLength = normal.Length();
            if (normalLength > 1.0e-12) {
                for (int row = 0; row < mesh.rows; ++row) {
                    for (int column = 0; column < mesh.columns; ++column) {
                        planarity = std::max(planarity,
                            std::abs(kachakacha::geometry::Dot(flat[row][column] - origin, normal) / normalLength));
                    }
                }
                Require(planarity < 1.0e-6, "fold preview at 0 is planar");
            }
        }

        // --- 結合展開: 隣接部材は一枚になり、部材境界が折り線(山/谷)として入る ---
        if (model.result.parts.size() >= 2) {
            const auto joined =
                kachakacha::io::BuildPartPattern(project, model, {1, 2});
            Require(!joined.outerBoundary.points.empty(), "joined pattern exists");
            Require(joined.foldLines.size() == 1, "joined pattern has one crease");
            Require(joined.foldLines.front().foldDirection != 0,
                "crease has a mountain/valley direction");
        }
        if (model.result.parts.size() >= 3) {
            bool rejected = false;
            try {
                (void)kachakacha::io::BuildPartPattern(project, model, {1, 3});
            } catch (const std::exception&) {
                rejected = true;
            }
            Require(rejected, "non-adjacent parts are rejected");
        }

        // --- 派生: 元断面の編集で境界ワイヤが追従する ---
        const Vector3 before =
            project.Wires()[0].wire.Evaluate(0.5);
        std::vector<Vector3> boundaryBefore;
        for (const auto& wire : project.Wires()) {
            if (wire.partModelSourceName.has_value()) {
                boundaryBefore.push_back(wire.wire.Evaluate(0.5));
            }
        }
        project.UpdateWire("断面4", SectionArc(16.0, 44.0));
        std::vector<Vector3> boundaryAfter;
        for (const auto& wire : project.Wires()) {
            if (wire.partModelSourceName.has_value()) {
                boundaryAfter.push_back(wire.wire.Evaluate(0.5));
            }
        }
        Require(!boundaryAfter.empty(), "boundaries survive source edit");
        bool moved = boundaryBefore.size() != boundaryAfter.size();
        for (std::size_t i = 0; !moved && i < boundaryAfter.size(); ++i) {
            moved = (boundaryAfter[i] - boundaryBefore[i]).Length() > 1.0e-6;
        }
        Require(moved, "boundaries follow the source edit");
        (void)before;

        // --- 手動境界 ---
        PartApproximationOptions manual;
        manual.splitAxis = PartSplitAxis::V;
        manual.automaticBoundaries = false;
        manual.manualBoundaryParameters = {0.3, 0.7};
        project.AddPartModel("近似M", "胴板", manual);
        const auto& manualModel = project.PartModels().back();
        Require(manualModel.result.parts.size() == 3, "manual boundaries make three parts");
        Require(std::abs(manualModel.result.parts[1].minimumParameter - 0.3) < 1.0e-12,
            "manual boundary parameter kept");

        // --- 抽出(独立コピー) ---
        const auto extracted = project.ExtractPartModelBoundaries("近似A");
        Require(extracted.size() == project.PartModels().front().boundaryWireNames.size(),
            "extraction copies every boundary");
        Require(project.ObjectStateInSets(ProjectObjectKind::Wire, extracted.front())
                == ObjectSetState::Visible,
            "extracted wires live in a set");
        for (const auto& name : extracted) {
            bool found = false;
            for (const auto& wire : project.Wires()) {
                if (wire.name == name) {
                    Require(!wire.partModelSourceName.has_value(), "extracted copy is independent");
                    found = true;
                }
            }
            Require(found, "extracted wire exists");
        }

        // --- 部材面から板材を作れる(板材化) ---
        project.AddPlate(
            "部材板", project.PartModels().front().partSurfaceNames.front(),
            0.5, PlateThicknessDirection::Centered, "プラ板");
        Require(project.FindPlate("部材板").has_value(), "plate created from part surface");

        // --- セット状態と保存/読込 ---
        project.SetObjectSetState("近似:近似A", ObjectSetState::ReferenceOnly);
        std::ostringstream saved;
        kachakacha::io::WriteProjectScript(saved, project);
        const std::string text = saved.str();
        Require(text.find("part_model 近似A 胴板 v 1") != std::string::npos,
            "part model saved");
        Require(text.find("part_model 近似M 胴板 v 0") != std::string::npos,
            "manual part model saved");
        Require(text.find("object_set_state 近似:近似A reference") != std::string::npos,
            "automatic set state saved");
        Require(text.find("object_set 抽出:近似A visible") != std::string::npos,
            "manual set saved");
        Require(text.find("polyline3d 近似A_境界") == std::string::npos,
            "derived rail wires are not saved");
        Require(text.find("surface_ruled 近似A_部材") == std::string::npos,
            "derived part surfaces are not saved");
        Require(text.find("plate 部材板 近似A_部材1") != std::string::npos,
            "plate on a part surface is saved after part_model");
        Require(text.find("part_model 近似A") < text.find("plate 部材板"),
            "part-surface plates are written after part_model");

        std::istringstream input(text);
        Project loaded = kachakacha::io::LoadProjectScript(input, "part-model-test");
        Require(loaded.PartModels().size() == 2, "part models loaded");
        Require(loaded.PartModels().front().result.parts.size()
                == project.PartModels().front().result.parts.size(),
            "loaded part model recomputes the same parts");
        Require(loaded.ObjectStateInSets(
                    ProjectObjectKind::Wire,
                    loaded.PartModels().front().boundaryWireNames.front())
                == ObjectSetState::ReferenceOnly,
            "set state restored");
        Require(loaded.FindPlate("部材板").has_value(), "part-surface plate restored");
        Require(loaded.PartModels().front().partSurfaceNames.size()
                == loaded.PartModels().front().result.parts.size(),
            "part surfaces regenerated on load");

        // --- 片付けと保護 ---
        bool guarded = false;
        try {
            (void)loaded.RemoveWire(loaded.PartModels().front().boundaryWireNames.front());
        } catch (const std::exception&) {
            guarded = true;
        }
        Require(guarded, "derived boundary wires cannot be removed directly");
        bool surfaceGuarded = false;
        try {
            (void)loaded.RemoveSurface(loaded.PartModels().front().partSurfaceNames.front());
        } catch (const std::exception&) {
            surfaceGuarded = true;
        }
        Require(surfaceGuarded, "derived part surfaces cannot be removed directly");
        bool partModelGuarded = false;
        try {
            (void)loaded.RemovePartModel("近似A");
        } catch (const std::exception&) {
            partModelGuarded = true;
        }
        Require(partModelGuarded, "part model with dependent plate is protected");
        Require(loaded.RemovePlate("部材板"), "dependent plate removed first");
        bool plateGuarded = false;
        try {
            (void)loaded.RemovePlate("胴板");
        } catch (const std::exception&) {
            plateGuarded = true;
        }
        Require(plateGuarded, "source plate is protected while a part model uses it");
        Require(loaded.RemovePartModel("近似A"), "part model removed");
        Require(loaded.RemovePartModel("近似M"), "second part model removed");
        for (const auto& wire : loaded.Wires()) {
            Require(!wire.partModelSourceName.has_value(), "derived wires removed with model");
        }
        for (const auto& surface : loaded.Surfaces()) {
            Require(!surface.partModelSourceName.has_value(), "derived surfaces removed with model");
        }
        Require(loaded.RemovePlate("胴板"), "plate removable after part models are gone");
    } catch (const std::exception& error) {
        std::cerr << "part_model_tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "part_model_tests passed\n";
    return EXIT_SUCCESS;
}
