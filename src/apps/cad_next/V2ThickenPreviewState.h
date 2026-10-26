#pragma once

#include "kachakacha/app/ThickenInputState.h"
#include "kachakacha/modeling/ShapeMesh.h"
#include "kachakacha/geometry/CurveSegment.h"
#include <vector>
#include <map>

//! 厚みツールの欄・下見・実行状態を一緒に持つ。確定までは文書へ入れない。
//! built のハンドルは V2ThickenCommands の取消・入力変更・確定で解放する。
struct V2ThickenPreviewState {
    bool shelfShown = false;
    kachakacha::v2::app::ThickenInputState input;
    kachakacha::v2::app::ThickenPreviewOutcome outcome;
    // 確定は下見に使った形をそのまま入れる。欄の面と同じ並び。
    std::vector<kachakacha::v2::modeling::KernelShapeHandle> built;
    std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>> builtEdges;
    std::vector<double> builtThickness;
    kachakacha::v2::modeling::ShapeMesh builtMesh;
    std::map<std::uint64_t,kachakacha::v2::modeling::ShapeMesh> builtMeshes;
    std::map<std::uint64_t,std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>>> builtWires;
    // 3D選択に映した面の写し。差分を読むための前回値。
    std::vector<kachakacha::v2::base::EntityId> mirror;
    bool mirroring = false;
    bool refreshing = false;
    bool refreshPending = false;
};

