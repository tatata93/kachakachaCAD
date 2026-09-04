#include "kachakacha/io/PartFoldState.h"
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

        // --- 開口(窓・ライト)の近似モデルへの反映 ---
        {
            Project openingProject = MakeBottleLikeProject();
            // 前面(y+側)の上寄りに四角い窓の下書きを置き、-y方向へ投影して胴板の開口にする。
            openingProject.AddWire("窓下書き", Wire::Polyline({
                {-4.0, 40.0, 26.0}, {4.0, 40.0, 26.0}, {4.0, 40.0, 33.0},
                {-4.0, 40.0, 33.0}, {-4.0, 40.0, 26.0}}));
            openingProject.AddProjectedWire("窓", "窓下書き", "胴", {0.0, -1.0, 0.0});
            openingProject.AddPlateOpening("胴板", "窓");

            PartApproximationOptions twoParts;
            twoParts.splitAxis = PartSplitAxis::V;
            twoParts.automaticBoundaries = false;
            twoParts.manualBoundaryParameters = {0.5};
            openingProject.AddPartModel("近似穴", "胴板", twoParts);
            const std::string derivedOpening = "近似穴_部材2_穴1";
            {
                const auto& openingModel = openingProject.PartModels().back();
                Require(openingModel.openingWireNames.size() == 1,
                    "window maps to one derived opening on the approximation");
                Require(openingModel.openingWireNames.front() == derivedOpening,
                    "derived opening belongs to the part that contains it");
            }
            bool derivedFound = false;
            for (const auto& wire : openingProject.Wires()) {
                if (wire.name == derivedOpening) {
                    derivedFound = true;
                    Require(wire.partModelSourceName.has_value(),
                        "derived opening is marked as derived");
                    Require(wire.projection.has_value()
                            && wire.projection->targetSurfaceName == "近似穴_部材2",
                        "derived opening projects onto its part surface");
                    Require(wire.wire.IsClosed(1.0e-6), "derived opening stays closed");
                }
            }
            Require(derivedFound, "derived opening wire exists");

            // 部材面から作った板材に、その開口を穴として付けられる。
            openingProject.AddPlate(
                "窓板", "近似穴_部材2", 0.5, PlateThicknessDirection::Centered, "プラ板");
            openingProject.AddPlateOpening("窓板", derivedOpening);
            Require(openingProject.FindPlate("窓板").has_value(), "part plate with opening");

            // 型紙にも開口が写る。
            const auto patterned = kachakacha::io::BuildPartPatternWithPreview(
                openingProject, openingProject.PartModels().back(), {2});
            Require(patterned.pattern.openings.size() == 1,
                "pattern of the owning part shows the opening");

            // 保存/読込で開口付きの部材板が復元される。
            std::ostringstream openingSaved;
            kachakacha::io::WriteProjectScript(openingSaved, openingProject);
            const std::string openingText = openingSaved.str();
            Require(openingText.find("plate_opening 窓板 " + derivedOpening)
                    != std::string::npos,
                "part-plate opening saved by derived name");
            std::istringstream openingInput(openingText);
            Project openingLoaded = kachakacha::io::LoadProjectScript(
                openingInput, "part-model-opening-test");
            Require(openingLoaded.PartModels().back().openingWireNames.size() == 1,
                "derived opening regenerated on load");
            {
                bool restored = false;
                for (const auto& plate : openingLoaded.Plates()) {
                    if (plate.name == "窓板") {
                        restored = plate.openingWireNames.size() == 1
                            && plate.openingWireNames.front() == derivedOpening;
                    }
                }
                Require(restored, "part-plate opening restored");
            }

            // 境界を動かすと開口の所属部材が変わり、古い派生穴は残らない。
            PartApproximationOptions movedBoundary = twoParts;
            movedBoundary.manualBoundaryParameters = {0.9};
            openingProject.UpdatePartModelOptions("近似穴", movedBoundary);
            {
                const auto& openingModel = openingProject.PartModels().back();
                Require(openingModel.openingWireNames.size() == 1,
                    "moved boundary keeps one derived opening");
                Require(openingModel.openingWireNames.front() == "近似穴_部材1_穴1",
                    "opening moved to the first part");
            }
            for (const auto& wire : openingProject.Wires()) {
                Require(wire.name != derivedOpening, "stale derived opening removed");
            }
            {
                bool cleared = false;
                for (const auto& plate : openingProject.Plates()) {
                    if (plate.name == "窓板") {
                        cleared = plate.openingWireNames.empty();
                    }
                }
                Require(cleared, "part plate no longer references the removed opening");
            }

            // 片付け: 板材→モデルの順で消すと派生穴も消える。
            Require(openingProject.RemovePlate("窓板"), "part plate removed");
            Require(openingProject.RemovePartModel("近似穴"), "opening part model removed");
            for (const auto& wire : openingProject.Wires()) {
                Require(!wire.partModelSourceName.has_value(),
                    "derived opening wires removed with the model");
            }
        }

        // --- 曲げ状態モデル(スライダーで選んだ曲げ具合の実体化) ---
        {
            Project foldProject = MakeBottleLikeProject();
            foldProject.AddWire("窓下書き", Wire::Polyline({
                {-4.0, 40.0, 26.0}, {4.0, 40.0, 26.0}, {4.0, 40.0, 33.0},
                {-4.0, 40.0, 33.0}, {-4.0, 40.0, 26.0}}));
            foldProject.AddProjectedWire("窓", "窓下書き", "胴", {0.0, -1.0, 0.0});
            foldProject.AddPlateOpening("胴板", "窓");
            PartApproximationOptions foldOptions;
            foldOptions.splitAxis = PartSplitAxis::V;
            foldOptions.automaticBoundaries = false;
            foldOptions.manualBoundaryParameters = {0.5};
            foldProject.AddPartModel("近似F", "胴板", foldOptions);
            const auto foldModel = foldProject.PartModels().back();

            const auto polylineLength = [](const Wire& wire) {
                const auto& points = wire.ControlPoints();
                double length = 0.0;
                for (std::size_t i = 1; i < points.size(); ++i) {
                    length += (points[i] - points[i - 1]).Length();
                }
                return length;
            };
            const auto findWire = [](const Project& project,
                                      const std::string& name) -> const Wire& {
                for (const auto& wire : project.Wires()) {
                    if (wire.name == name) {
                        return wire.wire;
                    }
                }
                throw std::runtime_error("fold-state wire missing: " + name);
            };

            // 完成形(progress=1): 板材2枚、窓は部材2の板の実際の穴になる。
            kachakacha::io::PartFoldStateOptions foldedState;
            foldedState.progress = 1.0;
            const auto foldedResult = kachakacha::io::AddPartFoldStateModel(
                foldProject, foldProject, foldModel, foldedState, "曲げ100");
            Require(foldedResult.plateNames.size() == 2,
                "folded state creates one plate per part");
            Require(foldedResult.openingWireNames.size() == 1,
                "window becomes a real hole in the folded state");
            {
                bool holeAttached = false;
                for (const auto& plate : foldProject.Plates()) {
                    if (plate.name == "曲げ100_部材2板") {
                        holeAttached = plate.openingWireNames.size() == 1;
                    }
                }
                Require(holeAttached, "hole is attached to the second part plate");
            }

            // 平面(progress=0): 全レールが同一平面に載り、レール長は完成形と一致する。
            Project flatTarget;
            kachakacha::io::PartFoldStateOptions flatState;
            flatState.progress = 0.0;
            const auto flatResult = kachakacha::io::AddPartFoldStateModel(
                flatTarget, foldProject, foldModel, flatState, "展開");
            Require(flatResult.plateNames.size() == 2,
                "flat state creates one plate per part");
            Require(flatResult.openingWireNames.size() == 1,
                "window is a real hole in the flat state too");
            {
                // 同一平面チェック。
                const auto& first = findWire(flatTarget, "展開_レール1").ControlPoints();
                const Vector3 origin = first.front();
                const Vector3 axisA = first.back() - origin;
                const auto& last = findWire(flatTarget, "展開_レール3").ControlPoints();
                Vector3 axisB{0.0, 0.0, 0.0};
                for (const auto& candidate : last) {
                    axisB = candidate - origin;
                    if (Cross(axisA, axisB).Length() > 1.0e-6) {
                        break;
                    }
                }
                const Vector3 normal = Cross(axisA, axisB);
                Require(normal.Length() > 1.0e-9, "flat rails are not degenerate");
                const Vector3 unit = normal * (1.0 / normal.Length());
                for (const char* railName : {"展開_レール1", "展開_レール2", "展開_レール3"}) {
                    for (const auto& point : findWire(flatTarget, railName).ControlPoints()) {
                        Require(std::abs(Dot(point - origin, unit)) < 1.0e-6,
                            "flat state lies in a single plane");
                    }
                }
                // レール長の保存(展開は等長)。
                for (int rail = 1; rail <= 3; ++rail) {
                    const double flatLength = polylineLength(findWire(
                        flatTarget, "展開_レール" + std::to_string(rail)));
                    const double foldedLength = polylineLength(findWire(
                        foldProject, "曲げ100_レール" + std::to_string(rail)));
                    Require(std::abs(flatLength - foldedLength) < 1.0e-6,
                        "rail lengths are preserved between flat and folded");
                }
            }

            // 平面状態は別プロジェクト(別kcd相当)として保存・復元できる。
            std::ostringstream flatSaved;
            kachakacha::io::WriteProjectScript(flatSaved, flatTarget);
            std::istringstream flatInput(flatSaved.str());
            Project flatLoaded = kachakacha::io::LoadProjectScript(
                flatInput, "part-fold-flat");
            Require(flatLoaded.Plates().size() == 2, "flat kcd restores both plates");

            // 中間(50%)も部材選択つきで実体化できる。
            kachakacha::io::PartFoldStateOptions halfState;
            halfState.progress = 0.5;
            halfState.partNumbers = {2};
            const auto halfResult = kachakacha::io::AddPartFoldStateModel(
                foldProject, foldProject, foldModel, halfState, "曲げ50");
            Require(halfResult.plateNames.size() == 1,
                "half-folded state respects the part selection");
        }

        // --- 面からの直接近似(合意8: 厚みは板材化の時点で指定) ---
        {
            Project surfaceProject = MakeBottleLikeProject();
            PartApproximationOptions surfaceOptions;
            surfaceOptions.splitAxis = PartSplitAxis::V;
            surfaceOptions.automaticBoundaries = false;
            surfaceOptions.manualBoundaryParameters = {0.5};
            surfaceProject.AddPartModelFromSurface("面近似", "胴", surfaceOptions);
            const auto& surfaceModel = surfaceProject.PartModels().back();
            Require(surfaceModel.sourceSurfaceName == "胴", "surface source recorded");
            Require(surfaceModel.sourcePlateName.empty(), "no plate source for surface input");
            Require(surfaceModel.result.parts.size() == 2, "surface input splits into two parts");
            Require(surfaceModel.boundaryWireNames.size() == 3, "surface input creates rails");
            Require(surfaceModel.partSurfaceNames.size() == 2, "surface input creates part surfaces");
            Require(surfaceModel.openingWireNames.empty(),
                "surface input has no plate openings to project");

            // 派生面を再近似することは禁止。
            bool derivedGuarded = false;
            try {
                surfaceProject.AddPartModelFromSurface(
                    "二重近似", surfaceModel.partSurfaceNames.front(), surfaceOptions);
            } catch (const std::exception&) {
                derivedGuarded = true;
            }
            Require(derivedGuarded, "derived part surfaces cannot be approximated again");

            // 元面の削除は近似モデルが使っている間は拒否される。
            bool sourceGuarded = false;
            try {
                (void)surfaceProject.RemoveSurface("胴");
            } catch (const std::exception&) {
                sourceGuarded = true;
            }
            Require(sourceGuarded, "source surface protected while a part model uses it");

            // スクリプト保存・復元(part_model_surface)。
            std::ostringstream saved;
            kachakacha::io::WriteProjectScript(saved, surfaceProject);
            const std::string text = saved.str();
            Require(text.find("part_model_surface 面近似 胴 v 0") != std::string::npos,
                "surface-source part model saved as part_model_surface");
            std::istringstream input(text);
            Project loadedSurface = kachakacha::io::LoadProjectScript(input, "surface-source");
            Require(loadedSurface.PartModels().size() == 1, "surface-source model loaded");
            Require(loadedSurface.PartModels().front().sourceSurfaceName == "胴",
                "surface source survives the round trip");
            Require(loadedSurface.PartModels().front().result.parts.size() == 2,
                "surface-source result regenerated on load");

            // 板材化: 面入力では options の板厚を使う。
            kachakacha::io::PartFoldStateOptions foldOptions;
            foldOptions.progress = 1.0;
            foldOptions.surfaceThicknessMillimeters = 0.3;
            const auto realized = kachakacha::io::AddPartFoldStateModel(
                surfaceProject, surfaceProject,
                surfaceProject.PartModels().front(), foldOptions, "面曲げ");
            Require(realized.plateNames.size() == 2, "surface-source fold state creates plates");
            const auto realizedPlate = surfaceProject.FindPlate(realized.plateNames.front());
            Require(realizedPlate.has_value(), "realized plate exists");
            Require(std::abs(realizedPlate->Thickness() - 0.3) < 1.0e-9,
                "surface thickness option is used for realized plates");

            // 板厚未指定(0)は拒否。
            bool thicknessGuarded = false;
            try {
                kachakacha::io::PartFoldStateOptions zeroThickness;
                zeroThickness.surfaceThicknessMillimeters = 0.0;
                (void)kachakacha::io::AddPartFoldStateModel(
                    surfaceProject, surfaceProject,
                    surfaceProject.PartModels().front(), zeroThickness, "面曲げ2");
            } catch (const std::exception&) {
                thicknessGuarded = true;
            }
            Require(thicknessGuarded, "surface-source realization requires a positive thickness");

            // 型紙も面入力から直接作れる。
            const auto patterns = kachakacha::io::BuildAllPartPatterns(
                surfaceProject, surfaceProject.PartModels().front());
            Require(patterns.size() == 2, "surface-source patterns built for each part");
            Require(!patterns.front().outerBoundary.points.empty(),
                "surface-source pattern has an outer boundary");
        }

        // --- 可動折り線(合意10: 折り線ごとの進行度、.kcd保存) ---
        {
            Project foldProject = MakeBottleLikeProject();
            PartApproximationOptions threeParts;
            threeParts.splitAxis = PartSplitAxis::V;
            threeParts.automaticBoundaries = false;
            threeParts.manualBoundaryParameters = {0.34, 0.67};
            foldProject.AddPartModel("可動", "胴板", threeParts);
            const auto& foldModel = foldProject.PartModels().back();
            Require(foldModel.result.parts.size() == 3, "three parts for two creases");

            std::vector<double> rails{0.0, 0.34, 0.67, 1.0};
            const auto mesh = kachakacha::model::DevelopPartMesh(
                *foldProject.FindPlate("胴板"), PartSplitAxis::V, rails, 48);
            const auto angles = kachakacha::model::MeasureCreaseAngles(mesh);
            Require(angles.size() == 2, "one angle per internal rail");
            Require(std::abs(angles[0]) > 1.0e-3, "curved loft has a real crease angle");

            // 全て1なら world と一致する。
            const auto identity = kachakacha::model::BuildPerCreaseFoldState(mesh, {1.0, 1.0});
            double worstIdentity = 0.0;
            for (int row = 0; row < mesh.rows; ++row) {
                for (int column = 0; column < mesh.columns; ++column) {
                    worstIdentity = std::max(worstIdentity,
                        (identity[row][column] - mesh.world[row][column]).Length());
                }
            }
            Require(worstIdentity < 1.0e-9, "all-1 crease progress reproduces the world mesh");

            // 折り線1だけ0にすると、その折り角がほぼ0になり、折り線2の角は保たれる。
            const auto partial = kachakacha::model::BuildPerCreaseFoldState(mesh, {0.0, 1.0});
            const auto measureAngle = [&](const std::vector<std::vector<Vector3>>& state,
                                          int rail) {
                // MeasureCreaseAngles と同じ定義で state 上の折り角を測る。
                const Vector3 span = state[rail][mesh.columns - 1] - state[rail][0];
                const Vector3 axis = span * (1.0 / span.Length());
                double sinSum = 0.0;
                double cosSum = 0.0;
                for (int column = 0; column < mesh.columns; column += 3) {
                    Vector3 toPrevious = state[rail - 1][column] - state[rail][column];
                    Vector3 toNext = state[rail + 1][column] - state[rail][column];
                    toPrevious = toPrevious - axis * Dot(toPrevious, axis);
                    toNext = toNext - axis * Dot(toNext, axis);
                    if (toPrevious.Length() <= 1.0e-9 || toNext.Length() <= 1.0e-9) {
                        continue;
                    }
                    const Vector3 straight = toPrevious * (-1.0 / toPrevious.Length());
                    const Vector3 next = toNext * (1.0 / toNext.Length());
                    sinSum += Dot(Cross(straight, next), axis);
                    cosSum += Dot(straight, next);
                }
                return std::atan2(sinSum, cosSum);
            };
            Require(std::abs(measureAngle(partial, 1)) < std::abs(angles[0]) * 0.2 + 1.0e-3,
                "zeroed crease becomes nearly flat");
            Require(std::abs(measureAngle(partial, 2) - angles[1]) < std::abs(angles[1]) * 0.2 + 1.0e-3,
                "other crease keeps its angle");
            // 剛体回転なのでレール長は変わらない。
            for (int row = 0; row < mesh.rows; ++row) {
                double worldLength = 0.0;
                double stateLength = 0.0;
                for (int column = 1; column < mesh.columns; ++column) {
                    worldLength += (mesh.world[row][column] - mesh.world[row][column - 1]).Length();
                    stateLength += (partial[row][column] - partial[row][column - 1]).Length();
                }
                Require(std::abs(worldLength - stateLength) < 1.0e-6,
                    "per-crease state preserves rail lengths");
            }

            // 進行度の保存とスクリプト往復。
            foldProject.SetPartModelRailFoldProgress("可動", {0.5, 1.0});
            std::ostringstream saved;
            kachakacha::io::WriteProjectScript(saved, foldProject);
            Require(saved.str().find("part_model_fold 可動 2 0.5 1") != std::string::npos,
                "custom fold progress saved to the script");
            std::istringstream input(saved.str());
            Project loadedFold = kachakacha::io::LoadProjectScript(input, "fold-progress");
            Require(loadedFold.PartModels().back().railFoldProgress
                    == std::vector<double>({0.5, 1.0}),
                "fold progress survives the round trip");

            // 部材数が変わる再計算でリセットされる。
            PartApproximationOptions twoParts;
            twoParts.splitAxis = PartSplitAxis::V;
            twoParts.automaticBoundaries = false;
            twoParts.manualBoundaryParameters = {0.5};
            loadedFold.UpdatePartModelOptions("可動", twoParts);
            Require(loadedFold.PartModels().back().railFoldProgress.empty(),
                "fold progress resets when the crease count changes");

            // サイズ不一致・範囲外は拒否。
            bool sizeGuarded = false;
            try {
                foldProject.SetPartModelRailFoldProgress("可動", {0.5});
            } catch (const std::exception&) {
                sizeGuarded = true;
            }
            Require(sizeGuarded, "fold progress size must match the crease count");
            bool rangeGuarded = false;
            try {
                foldProject.SetPartModelRailFoldProgress("可動", {9.0, 1.0});
            } catch (const std::exception&) {
                rangeGuarded = true;
            }
            Require(rangeGuarded, "fold progress range is validated");

            // 実体化は折り線ごとの状態を反映する(折り線1=平らでレールが遠ざかる)。
            kachakacha::io::PartFoldStateOptions foldOptions;
            foldOptions.progress = 1.0;
            foldProject.SetPartModelRailFoldProgress("可動", {0.0, 1.0});
            const auto realized = kachakacha::io::AddPartFoldStateModel(
                foldProject, foldProject, foldProject.PartModels().back(),
                foldOptions, "個別曲げ");
            Require(realized.railWireNames.size() == 4, "per-crease realization creates rails");
            const auto findWireLocal = [&](const std::string& wireName) {
                for (const auto& wire : foldProject.Wires()) {
                    if (wire.name == wireName) {
                        return wire.wire;
                    }
                }
                throw std::runtime_error("realized rail is missing: " + wireName);
            };
            // 同じモデルを完成形(折り指定なし)でも実体化し、後半レールが動いたことを確かめる。
            foldProject.SetPartModelRailFoldProgress("可動", {});
            (void)kachakacha::io::AddPartFoldStateModel(
                foldProject, foldProject, foldProject.PartModels().back(),
                foldOptions, "通常曲げ");
            const Vector3 customRail
                = findWireLocal("個別曲げ_レール4").ControlPoints().front();
            const Vector3 defaultRail
                = findWireLocal("通常曲げ_レール4").ControlPoints().front();
            Require((customRail - defaultRail).Length() > 1.0,
                "realized state reflects the per-crease fold");
            const Vector3 customFirst
                = findWireLocal("個別曲げ_レール1").ControlPoints().front();
            const Vector3 defaultFirst
                = findWireLocal("通常曲げ_レール1").ControlPoints().front();
            Require((customFirst - defaultFirst).Length() < 1.0e-6,
                "rails before the flattened crease stay in place");
        }
    } catch (const std::exception& error) {
        std::cerr << "part_model_tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "part_model_tests passed\n";
    return EXIT_SUCCESS;
}
