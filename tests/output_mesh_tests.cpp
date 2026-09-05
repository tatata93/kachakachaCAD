// 出力モード(オーナー指示)の中核: 出力表 → 三角形メッシュ → STL の試験。

#include "kachakacha/io/OutputMesh.h"
#include "kachakacha/model/Project.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using kachakacha::geometry::Vector3;
using kachakacha::io::BuildOutputMesh;
using kachakacha::io::OutputItem;
using kachakacha::io::OutputMesh;
using kachakacha::io::OutputMeshOptions;
using kachakacha::io::WriteOutputMeshStl;
using kachakacha::model::PlateThicknessDirection;
using kachakacha::model::Project;
using kachakacha::model::ProjectObjectKind;
using kachakacha::model::Wire;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

//! 20x10 の平らな面を1つ持つプロジェクト。
Project MakePanelProject()
{
    Project project;
    project.AddWire("枠", Wire::Polyline({
        {0.0, 0.0, 0.0},
        {20.0, 0.0, 0.0},
        {20.0, 10.0, 0.0},
        {0.0, 10.0, 0.0},
        {0.0, 0.0, 0.0},
    }));
    project.AddPlanarSurface("板面", "枠");
    return project;
}

} // namespace

int main()
{
    // --- 面1枚: 厚みを付けた閉じた薄板になる ---
    {
        const Project project = MakePanelProject();
        OutputMeshOptions options;
        options.surfaceThicknessMillimeters = 0.8;
        options.surfaceSamples = 6;
        const OutputMesh mesh = BuildOutputMesh(
            project, {{ProjectObjectKind::Surface, "板面"}}, options);
        Require(!mesh.Empty(), "surface becomes triangles");
        Require(mesh.Closed(), "thickened surface is watertight");
        double minimumZ = mesh.vertices.front().z;
        double maximumZ = minimumZ;
        for (const Vector3& vertex : mesh.vertices) {
            minimumZ = std::min(minimumZ, vertex.z);
            maximumZ = std::max(maximumZ, vertex.z);
        }
        Require(std::abs((maximumZ - minimumZ) - 0.8) < 1.0e-6,
            "surface is thickened by the requested amount");
    }

    // --- 閉じた線だけ: 面にしてから厚みを付ける ---
    {
        Project project;
        project.AddWire("輪", Wire::Polyline({
            {0.0, 0.0, 5.0},
            {10.0, 0.0, 5.0},
            {10.0, 10.0, 5.0},
            {0.0, 10.0, 5.0},
            {0.0, 0.0, 5.0},
        }));
        OutputMeshOptions options;
        options.surfaceSamples = 6;
        const OutputMesh mesh = BuildOutputMesh(
            project, {{ProjectObjectKind::Wire, "輪"}}, options);
        Require(!mesh.Empty(), "closed wire becomes a solid sheet");
        Require(mesh.Closed(), "wire sheet is watertight");
        bool hasFillTriangle = false;
        for (const OutputMesh::Triangle& triangle : mesh.triangles) {
            hasFillTriangle = hasFillTriangle || triangle.fill;
        }
        Require(hasFillTriangle, "wire-built faces are marked as auto filled");
    }

    // --- 自動でふさぐ: 板材の一部を欠いた形でも閉じるまで埋める ---
    {
        Project project = MakePanelProject();
        project.AddPlate("板", "板面", 1.0, PlateThicknessDirection::Positive, "プラ板");
        OutputMeshOptions options;
        options.surfaceSamples = 5;
        const OutputMesh withPlate = BuildOutputMesh(
            project, {{ProjectObjectKind::Plate, "板"}}, options);
        Require(withPlate.Closed(), "plate alone is watertight");

        // ふさぐ機能を切ると、開いた縁のある入力はそのまま報告される。
        Project openProject;
        openProject.AddWire("線A", Wire::Line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}));
        openProject.AddWire("線B", Wire::Line({0.0, 10.0, 0.0}, {10.0, 10.0, 0.0}));
        openProject.AddRuledSurface("帯", "線A", "線B");
        OutputMeshOptions noFill;
        noFill.fillOpenBoundaries = false;
        noFill.surfaceSamples = 4;
        noFill.surfaceThicknessMillimeters = 0.5;
        const OutputMesh unfilled = BuildOutputMesh(
            openProject, {{ProjectObjectKind::Surface, "帯"}}, noFill);
        Require(unfilled.filledLoopCount == 0, "filling can be turned off");
    }

    // --- STL 書き出し: 三角形数がヘッダと一致する ---
    {
        const Project project = MakePanelProject();
        OutputMeshOptions options;
        options.surfaceSamples = 4;
        const OutputMesh mesh = BuildOutputMesh(
            project, {{ProjectObjectKind::Surface, "板面"}}, options);
        const std::string path = "output_mesh_test.stl";
        WriteOutputMeshStl(path, mesh);
        std::ifstream input(path, std::ios::binary);
        Require(static_cast<bool>(input), "stl file is written");
        input.seekg(80);
        std::uint32_t count = 0;
        input.read(reinterpret_cast<char*>(&count), sizeof(count));
        Require(count == mesh.triangles.size(), "stl header matches triangle count");
        input.close();
        std::remove(path.c_str());
    }

    std::cout << "output mesh tests passed\n";
    return EXIT_SUCCESS;
}
