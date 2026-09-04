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

            // 全て1なら全帯が恒等変換=world と一致する。
            const auto identityTransforms
                = kachakacha::model::BuildRigidBandTransforms(mesh, {1.0, 1.0});
            Require(identityTransforms.size() == 3, "one transform per band");
            double worstIdentity = 0.0;
            for (int row = 0; row < mesh.rows; ++row) {
                for (int column = 0; column < mesh.columns; ++column) {
                    for (std::size_t band = 0; band < identityTransforms.size(); ++band) {
                        worstIdentity = std::max(worstIdentity,
                            (identityTransforms[band].Apply(mesh.world[row][column])
                                - mesh.world[row][column]).Length());
                    }
                }
            }
            Require(worstIdentity < 1.0e-9, "all-1 crease progress reproduces the world mesh");

            // 剛体変換なので、どんな進行度でも各帯の形(全点間距離)は完全に保たれる。
            const auto partialTransforms
                = kachakacha::model::BuildRigidBandTransforms(mesh, {0.0, 0.35});
            for (std::size_t band = 0; band < partialTransforms.size(); ++band) {
                const auto& transform = partialTransforms[band];
                const int bottomRow = static_cast<int>(band);
                const Vector3 a = transform.Apply(mesh.world[bottomRow][0]);
                const Vector3 b = transform.Apply(mesh.world[bottomRow + 1][mesh.columns - 1]);
                const double folded = (a - b).Length();
                const double original = (mesh.world[bottomRow][0]
                    - mesh.world[bottomRow + 1][mesh.columns - 1]).Length();
                Require(std::abs(folded - original) < 1.0e-9,
                    "rigid band transforms preserve every in-band distance");
            }

            // 折り線ごとの状態(行ごとの点列)を変換から作る補助。
            const auto stateFromTransforms = [&](const std::vector<double>& progressValues) {
                const auto transforms
                    = kachakacha::model::BuildRigidBandTransforms(mesh, progressValues);
                // 行 r は「その行を下縁に持つ帯」の変換で写す(行0は帯0、最終行は最終帯)。
                std::vector<std::vector<Vector3>> mapped(mesh.rows);
                for (int row = 0; row < mesh.rows; ++row) {
                    const std::size_t band = static_cast<std::size_t>(
                        std::min(row, static_cast<int>(transforms.size()) - 1));
                    mapped[row].reserve(mesh.columns);
                    for (int column = 0; column < mesh.columns; ++column) {
                        mapped[row].push_back(
                            transforms[band].Apply(mesh.world[row][column]));
                    }
                }
                return mapped;
            };
            // 折り線1だけ0にすると、帯1以降が折り線1の弦まわりへ -θ1 だけ回転し、
            // 折り線2は追加回転なし(帯2の変換=帯1の変換)。
            const auto zeroFirst
                = kachakacha::model::BuildRigidBandTransforms(mesh, {0.0, 1.0});
            {
                const Vector3 origin = mesh.world[1][0];
                Vector3 axis = mesh.world[1][mesh.columns - 1] - origin;
                axis = axis * (1.0 / axis.Length());
                const Vector3 sample = mesh.world[2][mesh.columns / 2];
                const Vector3 moved = zeroFirst[1].Apply(sample);
                const auto reject = [&](const Vector3& value) {
                    const Vector3 relative = value - origin;
                    return relative - axis * Dot(relative, axis);
                };
                const Vector3 before = reject(sample);
                const Vector3 after = reject(moved);
                Require(std::abs(before.Length() - after.Length()) < 1.0e-9,
                    "rotation keeps the distance to the crease chord");
                const double rotated = std::atan2(
                    Dot(Cross(before, after), axis), Dot(before, after));
                Require(std::abs(rotated + angles[0]) < 1.0e-6,
                    "zeroed crease rotates the tail by minus the full angle");
                const Vector3 viaBand2 = zeroFirst[2].Apply(sample);
                Require((viaBand2 - moved).Length() < 1.0e-9,
                    "untouched second crease adds no extra rotation");
            }
            // 曲げ確認アニメーション: 100%で剛体折り状態(全1ならworld)に一致し、
            // 0%では各帯の展開形が持ち上がった位置に平面として置かれる。
            {
                const auto atTarget = kachakacha::model::BuildBandFoldAnimationRails(
                    mesh, {1.0, 1.0}, 1.0, 30.0);
                Require(atTarget.size() == 6, "animation rails come in band pairs");
                double worstTarget = 0.0;
                for (int band = 0; band < 3; ++band) {
                    for (int column = 0; column < mesh.columns; ++column) {
                        worstTarget = std::max(worstTarget,
                            (atTarget[band * 2][column] - mesh.world[band][column]).Length());
                        worstTarget = std::max(worstTarget,
                            (atTarget[band * 2 + 1][column]
                                - mesh.world[band + 1][column]).Length());
                    }
                }
                Require(worstTarget < 1.0e-9,
                    "assembly=100% matches the folded model exactly");

                const auto atFlat = kachakacha::model::BuildBandFoldAnimationRails(
                    mesh, {1.0, 1.0}, 0.0, 30.0);
                for (int band = 0; band < 3; ++band) {
                    const auto& bottom = atFlat[band * 2];
                    const auto& top = atFlat[band * 2 + 1];
                    // 平面性: 帯の4隅の張る平面から全点が外れない。
                    const Vector3 origin = bottom.front();
                    const Vector3 e1 = bottom.back() - origin;
                    const Vector3 e2 = top.front() - origin;
                    const Vector3 planeNormal = Cross(e1, e2);
                    Require(planeNormal.Length() > 1.0e-9, "flat band is not degenerate");
                    const Vector3 unit = planeNormal * (1.0 / planeNormal.Length());
                    double planarity = 0.0;
                    for (int column = 0; column < mesh.columns; ++column) {
                        planarity = std::max(planarity,
                            std::abs(Dot(bottom[column] - origin, unit)));
                        planarity = std::max(planarity,
                            std::abs(Dot(top[column] - origin, unit)));
                    }
                    Require(planarity < 1.0e-6, "0% shows each part as a flat sheet");
                    // 等長: 展開の下レール長と一致する(型紙と同じ形)。
                    double flatLength = 0.0;
                    double developedLength = 0.0;
                    for (int column = 1; column < mesh.columns; ++column) {
                        flatLength += (bottom[column] - bottom[column - 1]).Length();
                        const double dx = mesh.developed[band][column].x
                            - mesh.developed[band][column - 1].x;
                        const double dy = mesh.developed[band][column].y
                            - mesh.developed[band][column - 1].y;
                        developedLength += std::sqrt(dx * dx + dy * dy);
                    }
                    Require(std::abs(flatLength - developedLength) < 1.0e-6,
                        "0% keeps the exact pattern lengths");
                    // 離れた位置: 帯の中心が元の位置からおおむね持ち上がっている。
                    const Vector3 flatCenter = bottom[mesh.columns / 2];
                    const Vector3 worldCenter = mesh.world[band][mesh.columns / 2];
                    Require((flatCenter - worldCenter).Length() > 15.0,
                        "0% places the flat sheet away from the model");
                    // 鏡像になっていないこと: 展開片の面の向きが元の帯の接平面の
                    // 向き(Cross(接線, 帯の上方向))と同じ側を向く。
                    const int anchorColumn = mesh.columns / 2;
                    const int nextColumn = std::min(anchorColumn + 1, mesh.columns - 1);
                    const Vector3 worldTangent = mesh.world[band][nextColumn]
                        - mesh.world[band][anchorColumn];
                    const Vector3 worldUpDirection = mesh.world[band + 1][anchorColumn]
                        - mesh.world[band][anchorColumn];
                    const Vector3 originalNormal = Cross(worldTangent, worldUpDirection);
                    const Vector3 flatTangent = bottom[nextColumn] - bottom[anchorColumn];
                    const Vector3 flatUpDirection = top[anchorColumn] - bottom[anchorColumn];
                    const Vector3 flatNormal = Cross(flatTangent, flatUpDirection);
                    Require(Dot(flatNormal, originalNormal) > 0.0,
                        "0% flat sheet is not mirrored");
                }

                // 中間状態も完全に等長: レールの辺長・素線長・対角線長が
                // どの進行度でも world と厳密に一致する(オーナー要求の核)。
                for (const double t : {0.25, 0.6, 0.9}) {
                    const auto rails = kachakacha::model::BuildBandFoldAnimationRails(
                        mesh, {1.0, 1.0}, t, 30.0);
                    for (int band = 0; band < 3; ++band) {
                        const auto& bottom = rails[band * 2];
                        const auto& top = rails[band * 2 + 1];
                        for (int column = 0; column < mesh.columns; ++column) {
                            if (column > 0) {
                                const double bottomEdge
                                    = (bottom[column] - bottom[column - 1]).Length();
                                const double worldBottomEdge = (mesh.world[band][column]
                                    - mesh.world[band][column - 1]).Length();
                                Require(std::abs(bottomEdge - worldBottomEdge) < 1.0e-6,
                                    "intermediate keeps rail edge lengths");
                                const double diagonal
                                    = (bottom[column] - top[column - 1]).Length();
                                const double worldDiagonal = (mesh.world[band][column]
                                    - mesh.world[band + 1][column - 1]).Length();
                                Require(std::abs(diagonal - worldDiagonal) < 1.0e-6,
                                    "intermediate keeps diagonal lengths");
                            }
                            const double ruling = (top[column] - bottom[column]).Length();
                            const double worldRuling = (mesh.world[band + 1][column]
                                - mesh.world[band][column]).Length();
                            Require(std::abs(ruling - worldRuling) < 1.0e-6,
                                "intermediate keeps ruling lengths (band width)");
                        }
                    }
                }
            }

            // 剛体変換なのでレール長も変わらない。
            const auto partial = stateFromTransforms({0.0, 1.0});
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
            Require(realized.railWireNames.size() == 6,
                "per-crease realization creates per-band rail edges");
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
                = findWireLocal("個別曲げ_部材3縁2").ControlPoints().front();
            const Vector3 defaultRail
                = findWireLocal("通常曲げ_レール4").ControlPoints().front();
            Require((customRail - defaultRail).Length() > 1.0,
                "realized state reflects the per-crease fold");
            const Vector3 customFirst
                = findWireLocal("個別曲げ_部材1縁1").ControlPoints().front();
            const Vector3 defaultFirst
                = findWireLocal("通常曲げ_レール1").ControlPoints().front();
            Require((customFirst - defaultFirst).Length() < 1.0e-6,
                "rails before the flattened crease stay in place");
        }

        // --- 積層(重ね板、合意9) ---
        {
            Project laminateProject = MakeBottleLikeProject();
            laminateProject.AddPlate(
                "外板", "胴", 0.3, PlateThicknessDirection::Positive, "プラ板");

            // 同じ元面の積層: 下の板の外側へ厚みぶんずれる。
            laminateProject.AddLaminatedPlate("外板_L2", "外板", 0.3, {});
            laminateProject.AddLaminatedPlate("外板_L3", "外板_L2", 0.2, {});
            const auto layer2 = laminateProject.FindPlate("外板_L2");
            const auto layer3 = laminateProject.FindPlate("外板_L3");
            Require(layer2.has_value() && layer3.has_value(), "laminated plates created");
            Require(std::abs(layer2->BaseOffset() - 0.3) < 1.0e-9,
                "second layer sits on top of the base thickness");
            Require(std::abs(layer3->BaseOffset() - 0.6) < 1.0e-9,
                "third layer stacks on the second");
            const auto basePlate = laminateProject.FindPlate("外板");
            const Vector3 baseOuter = basePlate->Evaluate(0.5, 0.5, 1.0);
            const Vector3 layerInner = layer2->Evaluate(0.5, 0.5, 0.0);
            Require((baseOuter - layerInner).Length() < 1.0e-9,
                "layer bottom touches the base top");
            Require(laminateProject.Plates().back().laminateBaseName == "外板_L2",
                "laminate relation stored");
            // 材質は下の板から引き継がれる。
            Require(laminateProject.Plates().back().material == "プラ板",
                "laminate inherits the base material");

            // 循環と削除の保護。
            bool cycleGuarded = false;
            try {
                laminateProject.SetPlateLaminate("外板", "外板_L3");
            } catch (const std::exception&) {
                cycleGuarded = true;
            }
            Require(cycleGuarded, "laminate cycles are rejected");
            bool removeGuarded = false;
            try {
                (void)laminateProject.RemovePlate("外板");
            } catch (const std::exception&) {
                removeGuarded = true;
            }
            Require(removeGuarded, "laminate base cannot be removed while layers exist");

            // 中央合わせの板を土台にした同面積層は拒否。
            laminateProject.AddPlate(
                "中央板", "胴", 0.3, PlateThicknessDirection::Centered, "プラ板");
            bool centeredGuarded = false;
            try {
                laminateProject.AddLaminatedPlate("中央板_L2", "中央板", 0.3, {});
            } catch (const std::exception&) {
                centeredGuarded = true;
            }
            Require(centeredGuarded, "centered bases are rejected for same-surface laminates");

            // 別の面に描いた板同士は「関係の記録のみ」(幾何は変えない)。
            laminateProject.AddWire("帯線1", Wire::Line({50.0, 0.0, 0.0}, {70.0, 0.0, 0.0}));
            laminateProject.AddWire("帯線2", Wire::Line({50.0, 0.0, 8.0}, {70.0, 0.0, 8.0}));
            laminateProject.AddRuledSurface("帯", "帯線1", "帯線2");
            laminateProject.AddPlate(
                "帯板", "帯", 0.3, PlateThicknessDirection::Positive, "プラ板");
            laminateProject.SetPlateLaminate("帯板", "外板");
            Require(std::abs(laminateProject.FindPlate("帯板")->BaseOffset()) < 1.0e-12,
                "cross-surface laminate keeps the plate geometry unchanged");

            // スクリプト往復で関係と下駄が復元される。
            std::ostringstream saved;
            kachakacha::io::WriteProjectScript(saved, laminateProject);
            Require(saved.str().find("plate_laminate 外板_L2 外板") != std::string::npos,
                "laminate relation saved to the script");
            std::istringstream input(saved.str());
            Project loadedLaminate
                = kachakacha::io::LoadProjectScript(input, "laminate");
            Require(std::abs(loadedLaminate.FindPlate("外板_L3")->BaseOffset() - 0.6) < 1.0e-9,
                "laminate offsets recomputed on load");
            bool laminateFound = false;
            for (const auto& plate : loadedLaminate.Plates()) {
                if (plate.name == "帯板") {
                    laminateFound = plate.laminateBaseName == "外板";
                }
            }
            Require(laminateFound, "cross-surface laminate relation survives the round trip");

            // 関係の解除で下駄も戻る。
            loadedLaminate.SetPlateLaminate("外板_L3", {});
            Require(std::abs(loadedLaminate.FindPlate("外板_L3")->BaseOffset()) < 1.0e-12,
                "clearing the laminate removes the offset");
        }

        // --- 面への開口(窓・ライト)と近似モデル・板材への反映 ---
        {
            Project openingProject = MakeBottleLikeProject();
            openingProject.AddWire("窓下書きS", Wire::Polyline({
                {-4.0, 40.0, 26.0}, {4.0, 40.0, 26.0}, {4.0, 40.0, 33.0},
                {-4.0, 40.0, 33.0}, {-4.0, 40.0, 26.0}}));
            openingProject.AddProjectedWire("窓S", "窓下書きS", "胴", {0.0, -1.0, 0.0});
            openingProject.AddSurfaceOpening("胴", "窓S");
            bool duplicateGuarded = false;
            try {
                openingProject.AddSurfaceOpening("胴", "窓S");
            } catch (const std::exception&) {
                duplicateGuarded = true;
            }
            Require(duplicateGuarded, "duplicate surface openings are rejected");

            // 開口に使われているワイヤは削除できない。
            bool wireGuarded = false;
            try {
                (void)openingProject.RemoveWire("窓S");
            } catch (const std::exception&) {
                wireGuarded = true;
            }
            Require(wireGuarded, "surface-opening wires cannot be removed");

            // 面入力の近似モデルへ開口が写る(部材内に収まる開口は派生穴ワイヤになる)。
            PartApproximationOptions twoParts;
            twoParts.splitAxis = PartSplitAxis::V;
            twoParts.automaticBoundaries = false;
            twoParts.manualBoundaryParameters = {0.5};
            openingProject.AddPartModelFromSurface("面近似穴", "胴", twoParts);
            Require(openingProject.PartModels().back().openingWireNames.size() == 1,
                "surface opening is projected into the surface-source part model");

            // この面から作る板材へ自動で引き継がれる。
            openingProject.AddPlate(
                "胴板2", "胴", 0.3, PlateThicknessDirection::Positive, "プラ板");
            bool inherited = false;
            for (const auto& plate : openingProject.Plates()) {
                if (plate.name == "胴板2") {
                    inherited = plate.openingWireNames
                        == std::vector<std::string>{"窓S"};
                }
            }
            Require(inherited, "new plates inherit surface openings");

            // スクリプト往復。
            std::ostringstream saved;
            kachakacha::io::WriteProjectScript(saved, openingProject);
            Require(saved.str().find("surface_opening 胴 窓S") != std::string::npos,
                "surface opening saved to the script");
            std::istringstream input(saved.str());
            Project loadedOpening = kachakacha::io::LoadProjectScript(input, "surface-opening");
            bool restored = false;
            for (const auto& surface : loadedOpening.Surfaces()) {
                if (surface.name == "胴") {
                    restored = surface.openingWireNames
                        == std::vector<std::string>{"窓S"};
                }
            }
            Require(restored, "surface opening survives the round trip");
            Require(loadedOpening.PartModels().back().openingWireNames.size() == 1,
                "part-model opening regenerated on load");

            // 実体化(曲げ状態)にも開口が実穴として写る。
            kachakacha::io::PartFoldStateOptions foldOptions;
            foldOptions.progress = 1.0;
            foldOptions.surfaceThicknessMillimeters = 0.3;
            const auto realized = kachakacha::io::AddPartFoldStateModel(
                openingProject, openingProject,
                openingProject.PartModels().back(), foldOptions, "面穴曲げ");
            Require(realized.openingWireNames.size() + realized.outlineWireNames.size() >= 1,
                "surface opening reaches the realized fold state");

            // 解除も往復も対称に動く。
            openingProject.RemoveSurfaceOpening("胴", "窓S");
            for (const auto& surface : openingProject.Surfaces()) {
                if (surface.name == "胴") {
                    Require(surface.openingWireNames.empty(), "surface opening removed");
                }
            }
        }

        // --- 接続スコープ(合意13): 周辺のワイヤ・面を近似の実形状へ自動変形 ---
        {
            Project scopeProject = MakeBottleLikeProject();
            // 胴の上に載っている縦の線(近似で角ばった実形状からずれるはずの線)。
            {
                std::vector<Vector3> onSurface;
                const auto& body = scopeProject.Surfaces().front().surface;
                for (int sample = 0; sample <= 24; ++sample) {
                    onSurface.push_back(body.Evaluate(0.25, sample / 24.0));
                }
                scopeProject.AddWire("接続線", Wire::Polyline(std::move(onSurface)));
            }
            // 面から遠い線(変形されないはずの線)。
            scopeProject.AddWire("遠い線", Wire::Line({100.0, 0.0, 0.0}, {120.0, 0.0, 0.0}));
            // 平面(境界は面から遠い): 平面再構築の経路を通す。
            scopeProject.AddWire("平面枠", Wire::Polyline({
                {100.0, 0.0, 10.0}, {120.0, 0.0, 10.0}, {120.0, 0.0, 30.0},
                {100.0, 0.0, 30.0}, {100.0, 0.0, 10.0}}));
            scopeProject.AddPlanarSurface("側平面", "平面枠");

            PartApproximationOptions twoParts;
            twoParts.splitAxis = PartSplitAxis::V;
            twoParts.automaticBoundaries = false;
            twoParts.manualBoundaryParameters = {0.5};
            scopeProject.AddPartModelFromSurface("接続近似", "胴", twoParts);
            scopeProject.SetPartModelConnectionScope(
                "接続近似", {"接続線", "遠い線"}, {"側平面"});

            const auto& scopeModel = scopeProject.PartModels().back();
            Require(scopeModel.adaptedWireNames.size() == 3,
                "adapted wires created (two wires + planar boundary)");
            Require(scopeModel.adaptedSurfaceNames
                    == std::vector<std::string>{"側平面_接続"},
                "adapted planar surface created");

            const auto findWireByName = [&](const Project& project, const std::string& name)
                -> const kachakacha::model::NamedWire& {
                for (const auto& wire : project.Wires()) {
                    if (wire.name == name) {
                        return wire;
                    }
                }
                throw std::runtime_error("wire missing: " + name);
            };
            // 面上の線: スナップされて元と変わり(近似の弦誤差ぶん)、変わり幅は許容内。
            const auto& adaptedOn = findWireByName(scopeProject, "接続線_接続");
            Require(adaptedOn.partModelSourceName.has_value()
                    && *adaptedOn.partModelSourceName == "接続近似",
                "adapted wire is derived from the model");
            double moved = 0.0;
            for (int sample = 0; sample <= 24; ++sample) {
                const double t = sample / 24.0;
                moved = std::max(moved,
                    (adaptedOn.wire.Evaluate(t)
                        - findWireByName(scopeProject, "接続線").wire.Evaluate(t)).Length());
            }
            Require(moved > 1.0e-4, "on-surface wire is snapped onto the angular mesh");
            Require(moved < scopeModel.result.maximumDeviationMillimeters + 0.5,
                "snap displacement stays within the approximation deviation");
            // 遠い線: 変形されない(端点一致)。
            const auto& adaptedFar = findWireByName(scopeProject, "遠い線_接続");
            Require((adaptedFar.wire.Evaluate(0.0)
                        - Vector3{100.0, 0.0, 0.0}).Length() < 1.0e-9
                    && (adaptedFar.wire.Evaluate(1.0)
                        - Vector3{120.0, 0.0, 0.0}).Length() < 1.0e-9,
                "far wire keeps its shape");

            // スコープの元は削除から保護される。
            bool scopeGuarded = false;
            try {
                (void)scopeProject.RemoveWire("接続線");
            } catch (const std::exception&) {
                scopeGuarded = true;
            }
            Require(scopeGuarded, "scope wires are protected while the model uses them");

            // スクリプト往復で再生成される。
            std::ostringstream saved;
            kachakacha::io::WriteProjectScript(saved, scopeProject);
            Require(saved.str().find("part_model_scope 接続近似 2 接続線 遠い線 1 側平面")
                    != std::string::npos,
                "connection scope saved to the script");
            std::istringstream input(saved.str());
            Project loadedScope = kachakacha::io::LoadProjectScript(input, "scope");
            Require(loadedScope.PartModels().back().adaptedWireNames.size() == 3,
                "adapted objects regenerated on load");

            // モデル削除で派生「_接続」も片付く。
            Require(loadedScope.RemovePartModel("接続近似"), "scope model removed");
            for (const auto& wire : loadedScope.Wires()) {
                Require(!wire.partModelSourceName.has_value(),
                    "adapted wires removed with the model");
            }
            for (const auto& surface : loadedScope.Surfaces()) {
                Require(!surface.partModelSourceName.has_value(),
                    "adapted surfaces removed with the model");
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "part_model_tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "part_model_tests passed\n";
    return EXIT_SUCCESS;
}
