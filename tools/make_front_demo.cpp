// 実機診断用のデモモデルを作る(オーナー報告「ユニットを近似が効かない」の再現)。
// 電車の前面に似た構成: 平らな前面 + 曲がった上帯 + 側面/角の面。
// 面の名前は報告時のスクリーンショットに合わせて surface_2..surface_7 とする。

#include "kachakacha/io/ProjectScript.h"
#include "kachakacha/model/Project.h"

#include <fstream>
#include <iostream>

using kachakacha::model::Project;
using kachakacha::model::Wire;

int main(int argc, char* argv[])
{
    const std::string path = argc > 1 ? argv[1] : "front-demo.kcd";
    Project project;
    project.AddWorkPlane("front_XZ",
        kachakacha::model::WorkPlane::FromPointNormal(
            {0.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}));

    // 前面の平らな板(surface_2)。
    project.AddWire("line_1", Wire::Polyline({
        {-40.0, 0.0, 0.0}, {40.0, 0.0, 0.0}, {40.0, 60.0, 0.0},
        {-40.0, 60.0, 0.0}, {-40.0, 0.0, 0.0},
    }));
    project.AddPlanarSurface("surface_2", "line_1");

    // 上の曲がった帯(surface_3)= 近似したい曲面。前後に膨らむ円弧2本のルールド。
    project.AddWire("arc_1", Wire::CircularArcThroughThreePoints(
        {-40.0, 60.0, 0.0}, {0.0, 66.0, -9.0}, {40.0, 60.0, 0.0}));
    project.AddWire("arc_2", Wire::CircularArcThroughThreePoints(
        {-40.0, 60.0, -24.0}, {0.0, 64.0, -32.0}, {40.0, 60.0, -24.0}));
    project.AddRuledSurface("surface_3", "arc_1", "arc_2");

    // 右の角(surface_5)。前面の右端から奥へ回り込む曲面。
    project.AddWire("arc_3", Wire::CircularArcThroughThreePoints(
        {40.0, 0.0, 0.0}, {46.0, 0.0, -12.0}, {40.0, 0.0, -24.0}));
    project.AddWire("arc_4", Wire::CircularArcThroughThreePoints(
        {40.0, 60.0, 0.0}, {46.0, 60.0, -12.0}, {40.0, 60.0, -24.0}));
    project.AddRuledSurface("surface_5", "arc_3", "arc_4");

    // 左の角(surface_6)。
    project.AddWire("arc_5", Wire::CircularArcThroughThreePoints(
        {-40.0, 0.0, 0.0}, {-46.0, 0.0, -12.0}, {-40.0, 0.0, -24.0}));
    project.AddWire("arc_6", Wire::CircularArcThroughThreePoints(
        {-40.0, 60.0, 0.0}, {-46.0, 60.0, -12.0}, {-40.0, 60.0, -24.0}));
    project.AddRuledSurface("surface_6", "arc_5", "arc_6");

    // 下の平らな帯(surface_7)。
    project.AddWire("line_2", Wire::Polyline({
        {-40.0, 0.0, 0.0}, {40.0, 0.0, 0.0}, {40.0, 0.0, -24.0},
        {-40.0, 0.0, -24.0}, {-40.0, 0.0, 0.0},
    }));
    project.AddPlanarSurface("surface_7", "line_2");

    // 部材グループ「前面」へまとめる(作業中グループの確認も兼ねる)。
    project.CreateObjectSet("前面");
    using kachakacha::model::ProjectObjectKind;
    for (const auto& wire : project.Wires()) {
        project.AssignObjectToSet(ProjectObjectKind::Wire, wire.name, "前面");
    }
    for (const auto& surface : project.Surfaces()) {
        project.AssignObjectToSet(ProjectObjectKind::Surface, surface.name, "前面");
    }
    project.SetDefaultObjectSet("前面");

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        std::cerr << "cannot write " << path << '\n';
        return 1;
    }
    kachakacha::io::WriteProjectScript(output, project);
    std::cout << "wrote " << path << '\n';
    return 0;
}
