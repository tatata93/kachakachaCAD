// V1 の .kcd を V2 の文書へ読む(io/KcdImport.h)。
#include "kachakacha/io/KcdImport.h"
#include "kachakacha/base/TestHarness.h"

#include <fstream>
#include <sstream>
#include <string>

using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::io::ImportKcdScript;
using kachakacha::v2::io::LooksLikeKcdPath;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] int CountKind(const kachakacha::v2::document::DocumentSnapshot& snapshot,
    EntityKind kind)
{
    int count = 0;
    for (const auto& entity : snapshot.entities) {
        count += entity.kind == kind ? 1 : 0;
    }
    return count;
}

//! 受入例(examples/railway-nose-acceptance.kcd)。試験の実行場所から探す。
[[nodiscard]] std::string ReadExample()
{
    for (const char* path : {"examples/railway-nose-acceptance.kcd",
             "../examples/railway-nose-acceptance.kcd", "../../examples/railway-nose-acceptance.kcd",
             KACHACAD_V2_REPO_ROOT "/examples/railway-nose-acceptance.kcd"}) {
        std::ifstream file(path);
        if (file) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }
    return {};
}

} // namespace

KACHA_V2_TEST(kcd_import, 平面と線と点が読める)
{
    DeterministicIdGenerator ids{3};
    const auto read = ImportKcdScript(
        "format_version 1\n"
        "plane_point_normal p1 0 0 5  0 0 1  1 0 0\n"
        "plane_offset p2 p1 10\n"
        "plane_rotate p3 p1 0 0 0  1 0 0  90\n"
        "plane_three p4 0 0 0 10 0 0 0 10 0\n"
        "line3d l1 0 0 0 10 0 0\n"
        "polyline3d pl 0 0 0 10 0 0 10 10 0\n"
        "bezier3d b1 0 0 0 3 5 0 7 5 0 10 0 0\n"
        "bspline3d s1 0 0 0 3 5 0 7 5 0 10 0 0\n"
        "bspline3d_knots s2 5 0 0 0 3 5 0 7 5 0 10 0 0 13 5 0 0 0 0 0 0.5 1 1 1 1\n"
        "circle3d c1 0 0 0  1 0 0  0 1 0  4\n"
        "arc3d a1 0 0 0  1 0 0  0 1 0  4 0 90\n"
        "point3d pt 1 2 3\n"
        "wire_meta l1 p1 reference\n"
        "visibility wire l1 hidden\n",
        ids);
    Require(read.HasValue(), "読める");
    const auto& snapshot = read.Value().snapshot;
    RequireEqual(std::to_string(CountKind(snapshot, EntityKind::WorkPlane)), std::string("4"), "平面4");
    RequireEqual(std::to_string(CountKind(snapshot, EntityKind::Wire)), std::string("7"), "線7");
    RequireEqual(std::to_string(CountKind(snapshot, EntityKind::Point)), std::string("1"), "点1");
    RequireEqual(std::to_string(read.Value().skippedCommands), std::string("0"), "読み飛ばし無し");
    // 名前が残る。平面 p2 は p1 から 10 離れる。
    bool hidden = false;
    for (const auto& entity : snapshot.entities) {
        if (entity.displayName == "l1") {
            hidden = entity.visibility == kachakacha::v2::domain::Visibility::Hidden;
        }
    }
    Require(hidden, "visibility が効く");
    for (const auto& feature : snapshot.features) {
        if (feature.displayName != "p2") {
            continue;
        }
        const auto& plane = std::get<kachakacha::v2::domain::CreateWorkPlaneDefinition>(
            feature.definition);
        RequireNear(plane.origin.z, 15.0, 1e-9, "p2 は z=15");
    }
}

KACHA_V2_TEST(kcd_import, 受入例がロフトと板まで読めて読めないものは名前を挙げる)
{
    const std::string text = ReadExample();
    Require(!text.empty(), "受入例が見つかる");
    DeterministicIdGenerator ids{3};
    const auto read = ImportKcdScript(text, ids);
    Require(read.HasValue(), "読める");
    const auto& snapshot = read.Value().snapshot;
    RequireEqual(std::to_string(CountKind(snapshot, EntityKind::WorkPlane)), std::string("6"), "平面6");
    RequireEqual(std::to_string(CountKind(snapshot, EntityKind::Wire)), std::string("18"), "線18");
    RequireEqual(std::to_string(CountKind(snapshot, EntityKind::GuideSurface)), std::string("1"),
        "ロフト1");
    RequireEqual(std::to_string(CountKind(snapshot, EntityKind::Part)), std::string("2"), "板2");
    // 読み飛ばし: wire_project 3, plate_range 2, plate_opening 3, body_surface_jig 1,
    // visibility(plate nose_panel_rear は読めた板なので効く、body の治具は読み飛ばし)1。
    Require(read.Value().skippedCommands >= 9, "読めないものは読み飛ばしとして数える");
    bool projectNoted = false;
    bool jigNoted = false;
    for (const auto& note : read.Value().notes) {
        RequireEqual(note.code, std::string("KCD1-I002"), "警告の番号");
        projectNoted = projectNoted || note.detailsJa.find("wire_project") != std::string::npos;
        jigNoted = jigNoted || note.detailsJa.find("body_surface_jig") != std::string::npos;
    }
    Require(projectNoted && jigNoted, "何を読み飛ばしたか名前で言う");
    // 線は元の平面に紐づく。
    int bound = 0;
    for (const auto& feature : snapshot.features) {
        if (const auto* wire = std::get_if<kachakacha::v2::domain::CreateWireDefinition>(
                &feature.definition)) {
            bound += wire->sourcePlaneId.has_value() ? 1 : 0;
        }
    }
    RequireEqual(std::to_string(bound), std::string("18"), "wire_meta が効く");
}

KACHA_V2_TEST(kcd_import, 壊れた行と無い名前は断る)
{
    DeterministicIdGenerator ids{3};
    const auto few = ImportKcdScript("line3d l1 0 0 0 10 0\n", ids);
    Require(!few.HasValue(), "数が足りなければ断る");
    RequireEqual(few.Diagnostics().front().code, std::string("KCD1-E001"), "理由の番号");
    const auto missing = ImportKcdScript("plane_offset p2 nothing 10\n", ids);
    Require(!missing.HasValue(), "無い平面は断る");
    const auto knots = ImportKcdScript(
        "bspline3d_knots s2 5 0 0 0 3 5 0 7 5 0 10 0 0 13 5 0 0 0 0 0 0.3 1 1 1 1\n", ids);
    Require(!knots.HasValue(), "一様でない節は断る(黙って近似しない)");
    const auto empty = ImportKcdScript("# comment only\n", ids);
    Require(!empty.HasValue(), "空は断る");
    Require(LooksLikeKcdPath("a/b.KCD") && !LooksLikeKcdPath("a/b.kcd2"), "拡張子の見分け");
}

KACHA_V2_TEST_MAIN("kcd_import")
