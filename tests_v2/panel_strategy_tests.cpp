// 部材の分け方(AT-FAB-005)と、手で付ける役割(AT-FAB-006)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/ManualRole.h"
#include "kachakacha/fabrication/PanelStrategy.h"

#include <string>
#include <vector>

using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::SegmentId;
using kachakacha::v2::fabrication::BrokenReferenceAction;
using kachakacha::v2::fabrication::BuildPanelPartition;
using kachakacha::v2::fabrication::CompareAllStrategies;
using kachakacha::v2::fabrication::FabricationSettings;
using kachakacha::v2::fabrication::FabricationStrategy;
using kachakacha::v2::fabrication::FindManualRoleConflicts;
using kachakacha::v2::fabrication::JointKind;
using kachakacha::v2::fabrication::ManualReferenceState;
using kachakacha::v2::fabrication::ManualRole;
using kachakacha::v2::fabrication::ManualRoleAssignment;
using kachakacha::v2::fabrication::PanelAdjacency;
using kachakacha::v2::fabrication::PanelCandidate;
using kachakacha::v2::fabrication::PanelGeometryClass;
using kachakacha::v2::fabrication::ResolveBrokenReference;
using kachakacha::v2::fabrication::RolesCanCoexist;
using kachakacha::v2::fabrication::ValidateManualRoles;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] PanelCandidate Panel(const std::string& id, PanelGeometryClass kind,
    double deviation = 0.0, double area = 100.0, double ratio = 0.0)
{
    PanelCandidate panel;
    panel.panelId = id;
    panel.classification = kind;
    panel.flattenDeviationMm = deviation;
    panel.areaMm2 = area;
    panel.doubleCurvedRatio = ratio;
    return panel;
}

[[nodiscard]] PanelAdjacency Edge(std::size_t first, std::size_t second,
    double length = 50.0, double angle = 1.5)
{
    PanelAdjacency adjacency;
    adjacency.firstIndex = first;
    adjacency.secondIndex = second;
    adjacency.sharedEdgeLengthMm = length;
    adjacency.dihedralAngleRad = angle;
    return adjacency;
}

//! 箱のような4枚。すべて平面で、順に隣り合う。
[[nodiscard]] std::vector<PanelCandidate> FourPlanar()
{
    return {Panel("a", PanelGeometryClass::Planar),
        Panel("b", PanelGeometryClass::Planar),
        Panel("c", PanelGeometryClass::Planar),
        Panel("d", PanelGeometryClass::Planar)};
}

[[nodiscard]] std::vector<PanelAdjacency> Chain4()
{
    return {Edge(0, 1), Edge(1, 2), Edge(2, 3)};
}

[[nodiscard]] std::string FirstCode(
    const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[nodiscard]] FabricationSettings Settings(FabricationStrategy strategy)
{
    FabricationSettings settings;
    settings.strategy = strategy;
    return settings;
}

} // namespace

// =====================================================================
//  AT-FAB-005 4通りの分け方
// =====================================================================

KACHA_V2_TEST(strategy, 1枚は1つにまとまる)
{
    const auto built = BuildPanelPartition(FourPlanar(), Chain4(),
        Settings(FabricationStrategy::OnePiece), 0.3);
    Require(built.HasValue(), "作れること");
    RequireEqual(std::to_string(built.Value().PieceCount()), "1", "部材の数");
    RequireEqual(std::to_string(built.Value().pieces.front().panelIndices.size()), "4",
        "4枚とも入っている");
}

KACHA_V2_TEST(strategy, 1枚のときの継ぎ目はすべて折り)
{
    const auto built = BuildPanelPartition(FourPlanar(), Chain4(),
        Settings(FabricationStrategy::OnePiece), 0.3);
    Require(built.HasValue(), "作れること");
    for (const auto& joint : built.Value().joints) {
        Require(joint.kind == JointKind::Fold, "折りになっている");
        Require(joint.matePairId.empty(), "貼り合わせは要らない");
    }
}

KACHA_V2_TEST(strategy, 別部材はすべて分かれる)
{
    const auto built = BuildPanelPartition(FourPlanar(), Chain4(),
        Settings(FabricationStrategy::SeparatePanels), 0.3);
    Require(built.HasValue(), "作れること");
    RequireEqual(std::to_string(built.Value().PieceCount()), "4", "部材の数");
    for (const auto& joint : built.Value().joints) {
        Require(joint.kind == JointKind::Separate, "別部材になっている");
        Require(!joint.matePairId.empty(), "貼り合わせの番号が付く");
    }
}

KACHA_V2_TEST(strategy, 別部材の貼り合わせ番号が重ならない)
{
    const auto built = BuildPanelPartition(FourPlanar(), Chain4(),
        Settings(FabricationStrategy::SeparatePanels), 0.3);
    Require(built.HasValue(), "作れること");
    std::vector<std::string> ids;
    for (const auto& joint : built.Value().joints) {
        for (const std::string& seen : ids) {
            Require(seen != joint.matePairId, "番号が重なっていない");
        }
        ids.push_back(joint.matePairId);
    }
    RequireEqual(std::to_string(ids.size()), "3", "継ぎ目の数");
}

KACHA_V2_TEST(strategy, 少数分割は目標の内側でまとめる)
{
    const auto built = BuildPanelPartition(FourPlanar(), Chain4(),
        Settings(FabricationStrategy::FewPieces), 0.3);
    Require(built.HasValue(), "作れること");
    RequireEqual(std::to_string(built.Value().PieceCount()), "1", "平面だけなら1枚");
}

KACHA_V2_TEST(strategy, 混合は強く曲がった面だけ切り離す)
{
    std::vector<PanelCandidate> panels = FourPlanar();
    // 3枚目だけを、切れ目でも逃がせない強い二重曲率にする。
    panels[2] = Panel("c", PanelGeometryClass::DoubleCurved, 5.0, 100.0, 0.9);
    const auto built =
        BuildPanelPartition(panels, Chain4(), Settings(FabricationStrategy::Hybrid), 0.3);
    Require(built.HasValue(), "作れること");
    RequireEqual(std::to_string(built.Value().PieceCount()), "3", "3つに分かれる");
    bool told = false;
    for (const auto& note : built.Value().notes) {
        if (note.code == "FAB-P013") {
            told = true;
        }
    }
    Require(told, "なぜ分けたかを言う");
}

KACHA_V2_TEST(strategy, 混合でも曲がっていなければ1枚になる)
{
    const auto built = BuildPanelPartition(FourPlanar(), Chain4(),
        Settings(FabricationStrategy::Hybrid), 0.3);
    Require(built.HasValue(), "作れること");
    RequireEqual(std::to_string(built.Value().PieceCount()), "1", "1枚");
}

KACHA_V2_TEST(strategy, つながっていなければ1枚にできないと断る)
{
    std::vector<PanelAdjacency> broken{Edge(0, 1)};   // 2,3 が孤立
    const auto built = BuildPanelPartition(FourPlanar(), broken,
        Settings(FabricationStrategy::OnePiece), 0.3);
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "FAB-P010", "診断コード");
}

KACHA_V2_TEST(strategy, 1枚では展開できない面があれば断る)
{
    std::vector<PanelCandidate> panels = FourPlanar();
    panels[1] = Panel("b", PanelGeometryClass::DoubleCurved, 9.0, 100.0, 0.95);
    const auto built = BuildPanelPartition(panels, Chain4(),
        Settings(FabricationStrategy::OnePiece), 0.3);
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "FAB-P011", "診断コード");
}

KACHA_V2_TEST(strategy, 必ず分ける指定があれば1枚にできない)
{
    std::vector<PanelAdjacency> edges = Chain4();
    edges[1].forcedBoundary = true;
    const auto built = BuildPanelPartition(FourPlanar(), edges,
        Settings(FabricationStrategy::OnePiece), 0.3);
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "FAB-P011", "診断コード");
}

KACHA_V2_TEST(strategy, 必ず分ける指定は少数分割でも守られる)
{
    std::vector<PanelAdjacency> edges = Chain4();
    edges[1].forcedBoundary = true;
    const auto built = BuildPanelPartition(FourPlanar(), edges,
        Settings(FabricationStrategy::FewPieces), 0.3);
    Require(built.HasValue(), "作れること");
    RequireEqual(std::to_string(built.Value().PieceCount()), "2", "2つに分かれる");
}

KACHA_V2_TEST(strategy, つないだまま指定は曲がっていても守られる)
{
    std::vector<PanelCandidate> panels = FourPlanar();
    panels[2] = Panel("c", PanelGeometryClass::DoubleCurved, 5.0, 100.0, 0.9);
    std::vector<PanelAdjacency> edges = Chain4();
    edges[1].keepTogether = true;
    edges[2].keepTogether = true;
    const auto built =
        BuildPanelPartition(panels, edges, Settings(FabricationStrategy::Hybrid), 0.3);
    Require(built.HasValue(), "作れること");
    RequireEqual(std::to_string(built.Value().PieceCount()), "1", "1枚のまま");
}

KACHA_V2_TEST(strategy, 4通りを並べて見比べられる)
{
    const auto comparison =
        CompareAllStrategies(FourPlanar(), Chain4(), FabricationSettings{}, 0.3);
    RequireEqual(std::to_string(comparison.entries.size()), "4", "4通り");
    for (const auto& entry : comparison.entries) {
        Require(entry.available, "平面だけならどれも作れる");
    }
    RequireEqual(std::to_string(comparison.entries[0].partition.PieceCount()), "1",
        "1枚");
    RequireEqual(std::to_string(comparison.entries[2].partition.PieceCount()), "4",
        "別部材");
}

KACHA_V2_TEST(strategy, 作れない戦略は理由つきで残る)
{
    std::vector<PanelAdjacency> broken{Edge(0, 1)};
    const auto comparison =
        CompareAllStrategies(FourPlanar(), broken, FabricationSettings{}, 0.3);
    bool sawRefusal = false;
    for (const auto& entry : comparison.entries) {
        if (entry.strategy == FabricationStrategy::OnePiece) {
            Require(!entry.available, "1枚は作れない");
            Require(!entry.refusal.empty(), "理由がある");
            sawRefusal = true;
        }
    }
    Require(sawRefusal, "1枚の欄がある");
}

KACHA_V2_TEST(strategy, 同じ入力からは毎回同じ分け方になる)
{
    std::string reference;
    for (int attempt = 0; attempt < 5; ++attempt) {
        const auto built = BuildPanelPartition(FourPlanar(), Chain4(),
            Settings(FabricationStrategy::FewPieces), 0.3);
        Require(built.HasValue(), "作れること");
        std::string text;
        for (const auto& piece : built.Value().pieces) {
            text += piece.pieceId + ":";
            for (const std::size_t index : piece.panelIndices) {
                text += std::to_string(index) + ",";
            }
            text += ";";
        }
        if (attempt == 0) {
            reference = text;
        } else {
            RequireEqual(text, reference, "毎回同じ");
        }
    }
}

KACHA_V2_TEST(strategy, 面が無ければ断る)
{
    const auto built = BuildPanelPartition({}, {}, FabricationSettings{}, 0.3);
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "FAB-P012", "診断コード");
}

KACHA_V2_TEST(strategy, 矛盾した指定は断る)
{
    std::vector<PanelAdjacency> edges = Chain4();
    edges[0].forcedBoundary = true;
    edges[0].keepTogether = true;
    const auto built = BuildPanelPartition(FourPlanar(), edges, FabricationSettings{}, 0.3);
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "FAB-P012", "診断コード");
}

KACHA_V2_TEST(strategy, 部材が上限を超えたら知らせる)
{
    std::vector<PanelCandidate> panels;
    for (int index = 0; index < 30; ++index) {
        panels.push_back(Panel("p" + std::to_string(index), PanelGeometryClass::Planar));
    }
    FabricationSettings settings = Settings(FabricationStrategy::SeparatePanels);
    settings.panelCountLimit = 24;
    const auto built = BuildPanelPartition(panels, {}, settings, 0.3);
    Require(built.HasValue(), "作れること");
    bool warned = false;
    for (const auto& note : built.Value().notes) {
        if (note.code == "FAB-P100") {
            warned = true;
        }
    }
    Require(warned, "上限超えを知らせる");
}

// =====================================================================
//  AT-FAB-006 手で付ける役割
// =====================================================================

namespace {

[[nodiscard]] ManualRoleAssignment Role(const std::string& id, ManualRole role,
    const EntityId& wire, double from = 0.0, double to = 1.0)
{
    ManualRoleAssignment assignment;
    assignment.assignmentId = id;
    assignment.role = role;
    assignment.wireEntityId = wire;
    assignment.fromParameter = from;
    assignment.toParameter = to;
    if (role == ManualRole::BendDirection) {
        assignment.bendAngleRad = 0.5;
    }
    return assignment;
}

[[nodiscard]] EntityId MakeWireId(std::uint64_t seed)
{
    DeterministicIdGenerator generator(seed);
    return EntityId(generator.Next());
}

} // namespace

KACHA_V2_TEST(manual_role, 7つの役割すべてに名前がある)
{
    const std::vector<ManualRole> roles{ManualRole::PanelBoundary, ManualRole::FoldLine,
        ManualRole::ReliefCut, ManualRole::Opening, ManualRole::KeepTogether,
        ManualRole::NoCutZone, ManualRole::BendDirection};
    RequireEqual(std::to_string(roles.size()), "7", "7つ");
    for (const ManualRole role : roles) {
        Require(kachakacha::v2::fabrication::ManualRoleNameJa(role) != "不明",
            "日本語名がある");
        Require(kachakacha::v2::fabrication::ManualRoleName(role) != "unknown",
            "英語名がある");
    }
}

KACHA_V2_TEST(manual_role, 7つの役割を1本ずつ付けられる)
{
    std::vector<ManualRoleAssignment> assignments;
    int number = 1;
    for (const ManualRole role : {ManualRole::PanelBoundary, ManualRole::FoldLine,
             ManualRole::ReliefCut, ManualRole::Opening, ManualRole::KeepTogether,
             ManualRole::NoCutZone, ManualRole::BendDirection}) {
        assignments.push_back(
            Role("r" + std::to_string(number), role, MakeWireId(number)));
        ++number;
    }
    const auto validated = ValidateManualRoles(assignments);
    Require(validated.HasValue(), "通ること");
    RequireEqual(std::to_string(validated.Value().assignments.size()), "7", "7件");
}

KACHA_V2_TEST(manual_role, 分けるとつないだままは同居できない)
{
    Require(!RolesCanCoexist(ManualRole::PanelBoundary, ManualRole::KeepTogether),
        "同居できない");
    const EntityId wire = MakeWireId(7);
    const auto validated = ValidateManualRoles(
        {Role("a", ManualRole::PanelBoundary, wire),
            Role("b", ManualRole::KeepTogether, wire)});
    Require(!validated.HasValue(), "commit しない");
    RequireEqual(FirstCode(validated.Diagnostics()), "FAB-R001", "診断コード");
}

KACHA_V2_TEST(manual_role, 折ると切れ目は同居できない)
{
    const EntityId wire = MakeWireId(8);
    const auto validated = ValidateManualRoles(
        {Role("a", ManualRole::FoldLine, wire), Role("b", ManualRole::ReliefCut, wire)});
    Require(!validated.HasValue(), "commit しない");
}

KACHA_V2_TEST(manual_role, 切れ目を入れない範囲に切れ目は置けない)
{
    const EntityId wire = MakeWireId(9);
    const auto validated = ValidateManualRoles(
        {Role("a", ManualRole::NoCutZone, wire, 0.0, 0.6),
            Role("b", ManualRole::ReliefCut, wire, 0.4, 1.0)});
    Require(!validated.HasValue(), "commit しない");
    const auto conflicts = FindManualRoleConflicts(
        {Role("a", ManualRole::NoCutZone, wire, 0.0, 0.6),
            Role("b", ManualRole::ReliefCut, wire, 0.4, 1.0)});
    RequireEqual(std::to_string(conflicts.size()), "1", "矛盾は1件");
    RequireNear(conflicts.front().overlapFrom, 0.4, 1.0e-12, "重なりの始まり");
    RequireNear(conflicts.front().overlapTo, 0.6, 1.0e-12, "重なりの終わり");
}

KACHA_V2_TEST(manual_role, 範囲が離れていれば同居できる)
{
    const EntityId wire = MakeWireId(10);
    const auto validated = ValidateManualRoles(
        {Role("a", ManualRole::NoCutZone, wire, 0.0, 0.4),
            Role("b", ManualRole::ReliefCut, wire, 0.6, 1.0)});
    Require(validated.HasValue(), "通ること");
}

KACHA_V2_TEST(manual_role, 別の線なら同じ役割の組でも通る)
{
    const auto validated = ValidateManualRoles(
        {Role("a", ManualRole::PanelBoundary, MakeWireId(11)),
            Role("b", ManualRole::KeepTogether, MakeWireId(12))});
    Require(validated.HasValue(), "通ること");
}

KACHA_V2_TEST(manual_role, 曲げの向きは向きが要る)
{
    ManualRoleAssignment assignment =
        Role("a", ManualRole::BendDirection, MakeWireId(13));
    assignment.bendAngleRad.reset();
    const auto validated = ValidateManualRoles({assignment});
    Require(!validated.HasValue(), "断る");
    RequireEqual(FirstCode(validated.Diagnostics()), "FAB-R002", "診断コード");
}

KACHA_V2_TEST(manual_role, 曲げの向きは他の役割へ付けられない)
{
    ManualRoleAssignment assignment = Role("a", ManualRole::FoldLine, MakeWireId(14));
    assignment.bendAngleRad = 0.3;
    const auto validated = ValidateManualRoles({assignment});
    Require(!validated.HasValue(), "断る");
}

KACHA_V2_TEST(manual_role, 範囲が0から1の外なら断る)
{
    const auto validated =
        ValidateManualRoles({Role("a", ManualRole::FoldLine, MakeWireId(15), 0.5, 0.5)});
    Require(!validated.HasValue(), "断る");
}

KACHA_V2_TEST(manual_role, 壊れた参照は知らせるが黙って動かさない)
{
    ManualRoleAssignment assignment = Role("a", ManualRole::FoldLine, MakeWireId(16));
    assignment.state = ManualReferenceState::Unprojectable;
    const auto validated = ValidateManualRoles({assignment});
    Require(validated.HasValue(), "他が正しければ通る");
    RequireEqual(std::to_string(validated.Value().brokenAssignmentIds.size()), "1",
        "壊れた参照が1件");
    bool warned = false;
    for (const auto& note : validated.Diagnostics()) {
        if (note.code == "FAB-R003") {
            warned = true;
        }
    }
    Require(warned, "知らせる");
    // 位置は変わっていないこと。
    RequireNear(validated.Value().assignments.front().fromParameter, 0.0, 0.0, "始まり");
    RequireNear(validated.Value().assignments.front().toParameter, 1.0, 0.0, "終わり");
}

KACHA_V2_TEST(manual_role, 壊れた参照を付け直せる)
{
    ManualRoleAssignment assignment = Role("a", ManualRole::FoldLine, MakeWireId(17));
    assignment.state = ManualReferenceState::Broken;
    const auto current = ValidateManualRoles({assignment});
    Require(current.HasValue(), "下ごしらえ");
    const EntityId replacement = MakeWireId(18);
    const auto fixed = ResolveBrokenReference(current.Value(), "a",
        BrokenReferenceAction::Reassign, replacement);
    Require(fixed.HasValue(), "付け直せる");
    Require(fixed.Value().brokenAssignmentIds.empty(), "壊れた参照が無くなる");
    Require(fixed.Value().assignments.front().wireEntityId == replacement, "指し先");
}

KACHA_V2_TEST(manual_role, 壊れた参照を無効にできる)
{
    ManualRoleAssignment assignment = Role("a", ManualRole::FoldLine, MakeWireId(19));
    assignment.state = ManualReferenceState::Broken;
    const auto current = ValidateManualRoles({assignment});
    Require(current.HasValue(), "下ごしらえ");
    const auto fixed = ResolveBrokenReference(current.Value(), "a",
        BrokenReferenceAction::Disable, std::nullopt);
    Require(fixed.HasValue(), "無効にできる");
    Require(fixed.Value().assignments.front().state == ManualReferenceState::Disabled,
        "外れている");
}

KACHA_V2_TEST(manual_role, 壊れた参照を消せる)
{
    ManualRoleAssignment assignment = Role("a", ManualRole::FoldLine, MakeWireId(20));
    assignment.state = ManualReferenceState::Broken;
    const auto current = ValidateManualRoles({assignment});
    Require(current.HasValue(), "下ごしらえ");
    const auto fixed = ResolveBrokenReference(current.Value(), "a",
        BrokenReferenceAction::Remove, std::nullopt);
    Require(fixed.HasValue(), "消せる");
    Require(fixed.Value().assignments.empty(), "無くなる");
}

KACHA_V2_TEST(manual_role, 付け直す先を言わなければ断る)
{
    ManualRoleAssignment assignment = Role("a", ManualRole::FoldLine, MakeWireId(21));
    assignment.state = ManualReferenceState::Broken;
    const auto current = ValidateManualRoles({assignment});
    Require(current.HasValue(), "下ごしらえ");
    const auto fixed = ResolveBrokenReference(current.Value(), "a",
        BrokenReferenceAction::Reassign, std::nullopt);
    Require(!fixed.HasValue(), "断る");
}

KACHA_V2_TEST(manual_role, 知らない割り当ては断る)
{
    const auto current = ValidateManualRoles({});
    Require(current.HasValue(), "空でも通る");
    const auto fixed = ResolveBrokenReference(current.Value(), "zzz",
        BrokenReferenceAction::Disable, std::nullopt);
    Require(!fixed.HasValue(), "断る");
    RequireEqual(FirstCode(fixed.Diagnostics()), "FAB-R004", "診断コード");
}

KACHA_V2_TEST(manual_role, 文書にない線を指しているものを見つけられる)
{
    const EntityId alive = MakeWireId(22);
    const EntityId gone = MakeWireId(23);
    const auto broken = kachakacha::v2::fabrication::FindBrokenReferences(
        {Role("a", ManualRole::FoldLine, alive), Role("b", ManualRole::FoldLine, gone)},
        {alive});
    RequireEqual(std::to_string(broken.size()), "1", "1件");
    RequireEqual(broken.front(), std::string("b"), "どれか");
}

KACHA_V2_TEST(manual_role, 同じ名前の割り当ては断る)
{
    const auto validated = ValidateManualRoles(
        {Role("a", ManualRole::FoldLine, MakeWireId(24)),
            Role("a", ManualRole::Opening, MakeWireId(25))});
    Require(!validated.HasValue(), "断る");
}

KACHA_V2_TEST_MAIN("panel_strategy_tests")
