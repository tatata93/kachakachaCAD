// 3D に出す役割の札(app/ToolRoleLabels.h)。
//
// 右の棚に「断面 3本」と出ていても、画面のどの線がその3本なのか分からなかった。
// 正本の 3D 図には TARGET / PROFILE / Section 1..n の札が出ている。
#include "kachakacha/app/ToolRoleLabels.h"
#include "kachakacha/base/TestHarness.h"

#include <array>
#include <cstdint>
#include <string>

using kachakacha::v2::app::ExtrudeInputState;
using kachakacha::v2::app::ExtrudeRoleLabels;
using kachakacha::v2::app::SurfaceInputState;
using kachakacha::v2::app::SurfaceOrdering;
using kachakacha::v2::app::SurfaceRoleLabels;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] EntityId Id(std::uint8_t value)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = value;
    return EntityId(kachakacha::v2::base::Uuid(bytes));
}

} // namespace

KACHA_V2_TEST(tool_role_labels, 押し出しは対象と輪郭に札を出す)
{
    ExtrudeInputState state;
    state.target = Id(1);
    state.profiles = {Id(2)};
    const auto labels = ExtrudeRoleLabels(state);
    Require(labels.size() == 2U, "2枚");
    Require(labels[0].text == std::string("TARGET"), "対象");
    Require(labels[1].text == std::string("PROFILE"), "輪郭は1本なら番号なし");
}

KACHA_V2_TEST(tool_role_labels, 面を押すときは札を重ねない)
{
    // 面を押すとき、輪郭の番号は対象と同じ立体を指す。
    // 同じ場所に2枚出すと読めない。
    ExtrudeInputState state;
    state.target = Id(1);
    state.profiles = {Id(1)};
    state.profileIsFace = true;
    const auto labels = ExtrudeRoleLabels(state);
    Require(labels.size() == 1U, "1枚だけ");
    Require(labels[0].text == std::string("TARGET"), "対象を残す");
}

KACHA_V2_TEST(tool_role_labels, 断面の番号は画面の断面順と同じ)
{
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::LoftSections;
    state.sections = {Id(1), Id(2), Id(3)};
    state.ordering = SurfaceOrdering::ManualLock;
    state.explicitOrder = {Id(3), Id(1), Id(2)};
    const auto labels = SurfaceRoleLabels(state);
    Require(labels.size() == 3U, "3枚");
    Require(labels[0].entityId == Id(3) && labels[0].text == std::string("Section 1"),
        "手で並べた順の1番目");
    Require(labels[2].entityId == Id(2) && labels[2].text == std::string("Section 3"),
        "3番目");
}

KACHA_V2_TEST(tool_role_labels, ガイドは2本なら左右で呼ぶ)
{
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::GuidedLoft;
    state.sections = {Id(1), Id(2)};
    state.guides = {Id(5), Id(6)};
    const auto labels = SurfaceRoleLabels(state);
    Require(labels.size() == 4U, "断面2 + ガイド2");
    Require(labels[2].text == std::string("Guide L"), "左");
    Require(labels[3].text == std::string("Guide R"), "右");
}

KACHA_V2_TEST(tool_role_labels, 曲線網は断面をUガイドをVと呼ぶ)
{
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::GordonNetwork;
    state.sections = {Id(1), Id(2)};
    state.guides = {Id(5), Id(6)};
    const auto labels = SurfaceRoleLabels(state);
    Require(labels.size() == 4U, "U2 + V2");
    Require(labels[0].text == std::string("U 1"), "断面は U");
    Require(labels[2].text == std::string("V 1"), "ガイドは V");
}

KACHA_V2_TEST(tool_role_labels, 使わない欄には札を出さない)
{
    // 入っていても捨てないが、いまの作り方で使わないものに札は出さない。
    // 出すと「これも使われる」と読めてしまう。
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::PlanarBoundary;
    state.sections = {Id(1), Id(2)};
    state.boundaries = {Id(9)};
    const auto labels = SurfaceRoleLabels(state);
    Require(labels.size() == 1U, "境界だけ");
    Require(labels[0].text == std::string("Boundary"), "1本なら番号なし");
    Require(labels[0].entityId == Id(9), "境界の番号");
}

KACHA_V2_TEST_MAIN("tool_role_labels_tests")
