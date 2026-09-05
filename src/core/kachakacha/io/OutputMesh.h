#pragma once

#include "kachakacha/model/Project.h"

#include <string>
#include <vector>

namespace kachakacha::io {

//! 出力表(オーナー指示: 出力モードの管理表)の1項目。
struct OutputItem {
    model::ProjectObjectKind kind = model::ProjectObjectKind::Surface;
    std::string name;
};

struct OutputMeshOptions {
    //! 面・ワイヤだけの部分に与える厚み(STL/STEPは中身のある形を要求するため)。
    double surfaceThicknessMillimeters = 0.5;
    //! 開いている縁を自動でふさぐ(オーナー指示)。
    bool fillOpenBoundaries = true;
    //! 面のテセレーション分割数(片側)。
    int surfaceSamples = 24;
};

//! 出力プレビュー用の三角形メッシュ。fill=true の三角形は
//! 「自動でふさいだ場所」(オーナー指示で色を変えて表示する)。
struct OutputMesh {
    struct Triangle {
        int a = 0;
        int b = 0;
        int c = 0;
        bool fill = false;
    };
    std::vector<geometry::Vector3> vertices;
    std::vector<Triangle> triangles;
    //! 自動でふさいだ輪郭の数。
    int filledLoopCount = 0;
    //! 埋めきれずに残った開いた縁の数(0なら閉じた形)。
    int openEdgeCount = 0;
    //! 人が読む説明(何を厚み化した・いくつ埋めた など)。
    std::vector<std::string> notes;

    [[nodiscard]] bool Empty() const noexcept { return triangles.empty(); }
    [[nodiscard]] bool Closed() const noexcept { return openEdgeCount == 0; }
};

//! 出力表の内容から、STL相当の三角形メッシュを組み立てる。
//! 面は板厚を与えた薄板として、ワイヤは閉じた輪郭を面にしてから薄板として扱う。
//! 隣り合う面がつながっていない開いた縁は(指定があれば)自動でふさぐ。
[[nodiscard]] OutputMesh BuildOutputMesh(
    const model::Project& project,
    const std::vector<OutputItem>& items,
    const OutputMeshOptions& options = {});

//! メッシュをバイナリSTLとして書き出す。
void WriteOutputMeshStl(const std::string& path, const OutputMesh& mesh);

} // namespace kachakacha::io
