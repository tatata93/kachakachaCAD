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
        // 境界ワイヤが部材数-1本、派生としてぶら下がる。
        Require(model.boundaryWireNames.size() == model.result.parts.size() - 1,
            "boundary wires match part count");
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

        // --- 幾何学的な正しさ: 上がすぼまる帯の展開は上辺が短い扇環になる ---
        const auto patterns = kachakacha::io::BuildAllPartPatterns(project, model);
        Require(patterns.size() == model.result.parts.size(), "one pattern per part");
        for (const auto& pattern : patterns) {
            Require(pattern.outerBoundary.points.size() >= 4, "pattern boundary exists");
            Require(pattern.analysis.maximumEdgeDistortionMillimeters < 0.5,
                "pattern preserves edge lengths");
        }

        // --- 結合展開: 隣接部材はひとつの型紙になり、非隣接は拒否される ---
        if (model.result.parts.size() >= 2) {
            const auto joined =
                kachakacha::io::BuildPartPattern(project, model, {1, 2});
            Require(!joined.outerBoundary.points.empty(), "joined pattern exists");
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
        Require(text.find("近似A_境界1 ") == std::string::npos
                && text.find("polyline3d 近似A_境界") == std::string::npos,
            "derived boundary wires are not saved");

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

        // --- 片付けと保護 ---
        bool guarded = false;
        try {
            (void)loaded.RemoveWire(loaded.PartModels().front().boundaryWireNames.front());
        } catch (const std::exception&) {
            guarded = true;
        }
        Require(guarded, "derived boundary wires cannot be removed directly");
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
        Require(loaded.RemovePlate("胴板"), "plate removable after part models are gone");
    } catch (const std::exception& error) {
        std::cerr << "part_model_tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "part_model_tests passed\n";
    return EXIT_SUCCESS;
}
