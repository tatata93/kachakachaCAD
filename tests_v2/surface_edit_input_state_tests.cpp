// 「面の編集」の入力(app/SurfaceEditInputState.h)。
//
// 道具から始め、3D で面や線を押すと欄へ入り、押し直すと外れる。縁は面の縁の近くを
// 押して決め、同じ面を押し直せば縁を替える。欄は固定の「縁1・縁2」ではなく、
// 作り方ごとの本数の決まり(SurfaceEditSlotsFor)で出す。
#include "kachakacha/app/SurfaceEditInputState.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::MirrorPlaneChoice;
using kachakacha::v2::app::MirrorPlaneFor;
using kachakacha::v2::app::SurfaceEditFooterLine;
using kachakacha::v2::app::SurfaceEditInputState;
using kachakacha::v2::app::SurfaceEditMissingJa;
using kachakacha::v2::app::SurfaceEditOperation;
using kachakacha::v2::app::SurfaceEditOperationForCommand;
using kachakacha::v2::app::SurfaceEditOutcome;
using kachakacha::v2::app::SurfaceEditReadyToBuild;
using kachakacha::v2::app::SurfaceEditSlotsFor;
using kachakacha::v2::app::WithoutSurfaceEditEntry;
using kachakacha::v2::app::WithSurfaceEditOperation;
using kachakacha::v2::app::WithSurfaceEditSurfacePick;
using kachakacha::v2::app::WithSurfaceEditWirePick;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::modeling::SurfaceContinuity;
using kachakacha::v2::test::Require;

namespace {

struct Ids {
    DeterministicIdGenerator generator{41};
    EntityId Next() { return generator.NextTyped<IdKind::Entity>(); }
};

} // namespace

KACHA_V2_TEST(surface_edit_input, 合わせるは面の縁を2本押して決め同じ面を押すと縁を替える)
{
    Ids ids;
    const EntityId roof = ids.Next();
    const EntityId side = ids.Next();
    SurfaceEditInputState state;
    state.operation = SurfaceEditOperation::Match;
    Require(!SurfaceEditReadyToBuild(state), "空では作れない");
    Require(SurfaceEditMissingJa(state).find("直す面の縁") != std::string::npos,
        "最初に何を選ぶかを言う: " + SurfaceEditMissingJa(state));
    state = WithSurfaceEditSurfacePick(state, side, {1, 2, 3}, 2);
    Require(SurfaceEditMissingJa(state).find("合わせ先の縁") != std::string::npos,
        "次は合わせ先: " + SurfaceEditMissingJa(state));
    state = WithSurfaceEditSurfacePick(state, roof, {0, 0, 0}, 0);
    Require(SurfaceEditReadyToBuild(state), "2 本そろえば作れる");
    Require(state.edges[0].surface == side && state.edges[1].surface == roof, "押した順");
    state = WithSurfaceEditSurfacePick(state, side, {5, 5, 5}, 3);
    Require(state.edges.size() == 2 && state.edges[0].edgeIndex == 3, "同じ面は縁を替える");
    state = WithSurfaceEditSurfacePick(state, side, {5, 5, 5}, 3);
    Require(state.edges.size() == 1 && state.edges[0].surface == roof, "同じ縁は外れる");
}

KACHA_V2_TEST(surface_edit_input, 同じ縁を2つの欄に入れたら作らない)
{
    Ids ids;
    const EntityId face = ids.Next();
    SurfaceEditInputState state;
    state.operation = SurfaceEditOperation::Bridge;
    state.edges.push_back({face, 1, {}});
    state.edges.push_back({face, 1, {}});
    Require(!SurfaceEditReadyToBuild(state), "同じ縁では作らない");
    Require(SurfaceEditMissingJa(state).find("同じ縁") != std::string::npos, "理由を言う");
}

KACHA_V2_TEST(surface_edit_input, 整えるは面を何枚でも入れられ押し直すと外れる)
{
    Ids ids;
    SurfaceEditInputState state;
    state.operation = SurfaceEditOperation::Refit;
    const auto slots = SurfaceEditSlotsFor(state.operation);
    Require(slots.surfacesMinimum == 1 && slots.surfacesMaximum > 100 && slots.batchPerSurface,
        "1 枚から何枚でも、1 枚ずつ作る");
    std::vector<EntityId> faces;
    for (int k = 0; k < 7; ++k) {
        faces.push_back(ids.Next());
        state = WithSurfaceEditSurfacePick(state, faces.back(), {}, -1);
    }
    Require(state.surfaces.size() == 7, "7 枚入る(1 枚に限らない)");
    state = WithSurfaceEditSurfacePick(state, faces[3], {}, -1);
    Require(state.surfaces.size() == 6, "押し直すと外れる");
    state.toleranceMm = 0.0;
    Require(!SurfaceEditReadyToBuild(state), "許容 0 は断る");
}

KACHA_V2_TEST(surface_edit_input, 面へ投影は面1枚と線を何本でも受ける)
{
    Ids ids;
    SurfaceEditInputState state;
    state.operation = SurfaceEditOperation::CurveOnSurface;
    const EntityId first = ids.Next();
    const EntityId second = ids.Next();
    state = WithSurfaceEditSurfacePick(state, first, {}, -1);
    state = WithSurfaceEditSurfacePick(state, second, {}, -1);
    Require(state.surfaces.size() == 1 && state.surfaces.front() == second, "面は 1 枚(替わる)");
    Require(SurfaceEditMissingJa(state).find("線") != std::string::npos, "線を待つ");
    for (int k = 0; k < 4; ++k) {
        state = WithSurfaceEditWirePick(state, ids.Next());
    }
    Require(state.wires.size() == 4 && SurfaceEditReadyToBuild(state), "線 4 本で作れる");
    state = WithoutSurfaceEditEntry(state, state.wires[1]);
    Require(state.wires.size() == 3, "一覧の × で外れる");
}

KACHA_V2_TEST(surface_edit_input, 作り方を替えても入れたものは使えるところへ残る)
{
    Ids ids;
    const EntityId a = ids.Next();
    const EntityId b = ids.Next();
    SurfaceEditInputState state;
    state.operation = SurfaceEditOperation::Match;
    state = WithSurfaceEditSurfacePick(state, a, {}, 0);
    state = WithSurfaceEditSurfacePick(state, b, {}, 2);
    state = WithSurfaceEditOperation(state, SurfaceEditOperation::Mirror);
    Require(state.edges.empty() && state.surfaces.size() == 2, "縁の面が面の欄へ移る");
    state = WithSurfaceEditOperation(state, SurfaceEditOperation::Bridge);
    Require(state.edges.size() == 2 && state.edges[0].edgeIndex == -1, "面が縁の欄へ(縁は未定)");
    Require(SurfaceEditMissingJa(state).find("縁が決まっていません") != std::string::npos,
        "縁を押すよう言う: " + SurfaceEditMissingJa(state));
}

KACHA_V2_TEST(surface_edit_input, 命令と一番下の一行)
{
    SurfaceEditOperation operation = SurfaceEditOperation::Match;
    Require(SurfaceEditOperationForCommand("surface.bridge", operation)
            && operation == SurfaceEditOperation::Bridge, "surface.bridge");
    Require(SurfaceEditOperationForCommand("wire.project_surface", operation)
            && operation == SurfaceEditOperation::CurveOnSurface,
        "既存の「面へ投影」の命令も同じ道具に来る");
    Require(!SurfaceEditOperationForCommand("surface.create", operation), "面を作るは別");
    SurfaceEditInputState state;
    state.operation = SurfaceEditOperation::Bridge;
    state.continuityB = SurfaceContinuity::G2;
    const std::string footer = SurfaceEditFooterLine(state, SurfaceEditOutcome{}, false);
    Require(footer.find("EDGES=0/2") != std::string::npos && footer.find("G1-G2") != std::string::npos,
        "縁の数と両端の滑らかさ: " + footer);
    const auto plane = MirrorPlaneFor(MirrorPlaneChoice::CenterXZ, {9, 9, 9}, {0, 0, 1});
    Require(plane.normal.y == 1.0 && plane.point.x == 0.0, "車体の中心は Y = 0");
    const auto work = MirrorPlaneFor(MirrorPlaneChoice::WorkPlane, {9, 9, 9}, {0, 0, 1});
    Require(work.point.x == 9.0 && work.normal.z == 1.0, "作業平面はそのまま");
}

KACHA_V2_TEST_MAIN("surface_edit_input_state_tests")
