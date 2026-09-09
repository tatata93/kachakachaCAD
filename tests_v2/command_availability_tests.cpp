// コマンドが押せるかどうかの判断(command-catalog.md §1)。
//
// ここを画面側に書いていたころ、選択に依る条件がすべて「押せない」のままで、
// 押し出しも分割も作業平面の切り替えも動かなかった。
// それに気づけたのは PC のビルドまで来たときである。
// 判断を core へ移したので、いまは雲の側で数秒で分かる。
#include "kachakacha/app/CommandAvailability.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/domain/Entity.h"

#include <set>
#include <string>
#include <vector>

using kachakacha::v2::app::BuildSelectionFacts;
using kachakacha::v2::app::CommandCatalog;
using kachakacha::v2::app::ExternalCounts;
using kachakacha::v2::app::SelectionFacts;
using kachakacha::v2::app::SelectionPredicate;
using kachakacha::v2::app::SelectionSatisfies;
using kachakacha::v2::app::SelectionSet;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::document::DocumentSnapshot;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::SnapCurve;
using kachakacha::v2::modeling::SnapScene;
using kachakacha::v2::test::Require;

namespace {

//! 場面と文書をまとめて組み立てる、試験用の小さな机。
struct Bench {
    DeterministicIdGenerator ids{7};
    DocumentSnapshot snapshot;
    SnapScene scene;
    SelectionSet selection;
    GeometryTolerance tolerance;

    [[nodiscard]] EntityId AddEntity(EntityKind kind)
    {
        Entity entity;
        entity.id = ids.NextTyped<IdKind::Entity>();
        entity.kind = kind;
        entity.displayName = "もの";
        snapshot.entities.push_back(entity);
        return entity.id;
    }

    void AddCurve(const EntityId& owner, Vector3 start, Vector3 end)
    {
        const auto made = CurveSegment::MakeLine(start, end);
        Require(made.HasValue(), "直線が作れる");
        scene.curves.push_back(SnapCurve{owner, ids.NextTyped<IdKind::Segment>(),
            made.Value(), false});
    }

    //! 閉じた四角を1つのワイヤーとして足す。
    [[nodiscard]] EntityId AddRectangle(double width, double height)
    {
        const EntityId id = AddEntity(EntityKind::Wire);
        AddCurve(id, {0.0, 0.0, 0.0}, {width, 0.0, 0.0});
        AddCurve(id, {width, 0.0, 0.0}, {width, height, 0.0});
        AddCurve(id, {width, height, 0.0}, {0.0, height, 0.0});
        AddCurve(id, {0.0, height, 0.0}, {0.0, 0.0, 0.0});
        return id;
    }

    [[nodiscard]] EntityId AddOpenLine(double y)
    {
        const EntityId id = AddEntity(EntityKind::Wire);
        AddCurve(id, {0.0, y, 0.0}, {10.0, y, 0.0});
        return id;
    }

    [[nodiscard]] SelectionFacts Facts(const ExternalCounts& external = {}) const
    {
        return BuildSelectionFacts(selection, snapshot, scene, tolerance, external,
            false, false);
    }
};

} // namespace

KACHA_V2_TEST(availability, 閉じた四角は閉じた輪郭として数える)
{
    // これが数えられていなかったので、押し出しがずっと押せなかった。
    Bench bench;
    bench.selection.entityIds.push_back(bench.AddRectangle(20.0, 10.0));
    const auto facts = bench.Facts();
    Require(facts.wires == 1, "ワイヤーは1つ");
    Require(facts.curves == 4, "線は4本");
    Require(facts.wireChains == 1, "鎖は1つ");
    Require(facts.closedProfiles == 1, "閉じた輪郭が1つ");
    Require(SelectionSatisfies(SelectionPredicate::OneOrMoreClosedProfiles, facts),
        "押し出しが押せる");
    Require(SelectionSatisfies(SelectionPredicate::OneClosedProfile, facts),
        "1つだけの条件も満たす");
}

KACHA_V2_TEST(availability, 開いた線は閉じた輪郭ではない)
{
    // 開いた線を押し出せると言ってしまうと、押してから断ることになる。
    Bench bench;
    bench.selection.entityIds.push_back(bench.AddOpenLine(0.0));
    const auto facts = bench.Facts();
    Require(facts.closedProfiles == 0, "閉じた輪郭は0");
    Require(!SelectionSatisfies(SelectionPredicate::OneOrMoreClosedProfiles, facts),
        "押し出しは押せない");
    Require(SelectionSatisfies(SelectionPredicate::OneOrMoreWires, facts),
        "ワイヤーとしては選べている");
}

KACHA_V2_TEST(availability, 離れた2本は鎖2つになる)
{
    // 分割や接線接続は鎖2つを要る。1つのワイヤーは1つの鎖として数える。
    Bench bench;
    bench.selection.entityIds.push_back(bench.AddOpenLine(0.0));
    bench.selection.entityIds.push_back(bench.AddOpenLine(5.0));
    const auto facts = bench.Facts();
    Require(facts.wireChains == 2, "鎖は2つ");
    Require(SelectionSatisfies(SelectionPredicate::TwoWireChains, facts),
        "接線接続が押せる");
}

KACHA_V2_TEST(availability, 1本だけでは鎖2つの条件を満たさない)
{
    Bench bench;
    bench.selection.entityIds.push_back(bench.AddOpenLine(0.0));
    Require(!SelectionSatisfies(SelectionPredicate::TwoWireChains, bench.Facts()),
        "1本では押せない");
}

KACHA_V2_TEST(availability, 作業平面を1つ選べば作業中にできる)
{
    Bench bench;
    bench.selection.entityIds.push_back(bench.AddEntity(EntityKind::WorkPlane));
    const auto facts = bench.Facts();
    Require(facts.workPlanes == 1, "作業平面が1つ");
    Require(SelectionSatisfies(SelectionPredicate::OneWorkPlane, facts), "押せる");
    Require(SelectionSatisfies(SelectionPredicate::OnePlanarFaceOrWorkPlane, facts),
        "平らな面の条件も満たす");
}

KACHA_V2_TEST(availability, 作業平面を2つ選ぶとどちらか決まらない)
{
    Bench bench;
    bench.selection.entityIds.push_back(bench.AddEntity(EntityKind::WorkPlane));
    bench.selection.entityIds.push_back(bench.AddEntity(EntityKind::WorkPlane));
    Require(!SelectionSatisfies(SelectionPredicate::OneWorkPlane, bench.Facts()),
        "2つでは押せない");
}

KACHA_V2_TEST(availability, 部品の数で分かれる)
{
    Bench bench;
    bench.selection.entityIds.push_back(bench.AddEntity(EntityKind::Part));
    Require(SelectionSatisfies(SelectionPredicate::OnePart, bench.Facts()),
        "1つで足りる条件は通る");
    Require(!SelectionSatisfies(SelectionPredicate::TwoParts, bench.Facts()),
        "2つ要る条件は通らない");
    bench.selection.entityIds.push_back(bench.AddEntity(EntityKind::Part));
    Require(SelectionSatisfies(SelectionPredicate::TwoParts, bench.Facts()),
        "2つで通る");
    Require(!SelectionSatisfies(SelectionPredicate::OnePart, bench.Facts()),
        "1つだけの条件は通らなくなる");
}

KACHA_V2_TEST(availability, 製作は部品からでも形状ガイドからでも始められる)
{
    // 平らな部品からも、曲がった面からも型紙は作れる。
    // 片方しか通さないと、曲がった車体が作れない。
    Bench parts;
    parts.selection.entityIds.push_back(parts.AddEntity(EntityKind::Part));
    Require(SelectionSatisfies(SelectionPredicate::OnePartOrSurface, parts.Facts()),
        "部品1つで始められる");
    Bench surface;
    surface.selection.entityIds.push_back(surface.AddEntity(EntityKind::GuideSurface));
    const auto facts = surface.Facts();
    Require(facts.guideSurfaces == 1, "形状ガイドを数える");
    Require(SelectionSatisfies(SelectionPredicate::OnePartOrSurface, facts),
        "形状ガイド1つでも始められる");
    // 両方選んだら、どちらから作るのか決まらない。
    Bench both;
    both.selection.entityIds.push_back(both.AddEntity(EntityKind::Part));
    both.selection.entityIds.push_back(both.AddEntity(EntityKind::GuideSurface));
    Require(!SelectionSatisfies(SelectionPredicate::OnePartOrSurface, both.Facts()),
        "両方だと決まらない");
}

KACHA_V2_TEST(availability, 製作モデルと型紙は画面から数をもらう)
{
    // 部材も型紙も文書には入らない。画面が覚えているので、そこから渡す。
    Bench bench;
    ExternalCounts external;
    Require(!SelectionSatisfies(SelectionPredicate::OneFabricationModel,
                bench.Facts(external)),
        "作る前は押せない");
    external.fabricationModels = 1;
    external.patterns = 2;
    const auto facts = bench.Facts(external);
    Require(SelectionSatisfies(SelectionPredicate::OneFabricationModel, facts),
        "作れば押せる");
    Require(SelectionSatisfies(SelectionPredicate::OneOrMorePatterns, facts),
        "型紙も押せる");
}

KACHA_V2_TEST(availability, 何も選んでいなければ選択に依る条件はすべて通らない)
{
    // 押せるのに何も起きない、を作らないための土台。
    Bench bench;
    const auto facts = bench.Facts();
    const SelectionPredicate needsSelection[] = {
        SelectionPredicate::OneWorkPlane,
        SelectionPredicate::OnePlanarFaceOrWorkPlane,
        SelectionPredicate::OneOrMoreWires,
        SelectionPredicate::TwoWireChains,
        SelectionPredicate::OneClosedProfile,
        SelectionPredicate::OneOrMoreClosedProfiles,
        SelectionPredicate::OnePart,
        SelectionPredicate::TwoParts,
        SelectionPredicate::OneDerivedEntity,
        SelectionPredicate::OneFabricationModel,
        SelectionPredicate::OneFabricationPanel,
        SelectionPredicate::OneOrMorePatterns,
        SelectionPredicate::OneOrMoreSelectedCurves,
        SelectionPredicate::OnePartOrSurface,
    };
    for (const SelectionPredicate predicate : needsSelection) {
        Require(!SelectionSatisfies(predicate, facts),
            std::string("選ばずには通らない: ")
                + std::string(kachakacha::v2::app::SelectionBlockReasonJa(predicate)));
    }
    // グループは「0でもよい」ので、ここだけは通る。
    Require(SelectionSatisfies(SelectionPredicate::ZeroOrOneGroup, facts),
        "グループは選ばなくてよい");
}

KACHA_V2_TEST(availability, 条件はどれも台帳のどれかで使われている)
{
    // 条件を足したのに、どのコマンドにも付け忘れると、
    // 判断だけがあって効かないものが残る。実際に一度そうなり、
    // 「部品を1つ選んでください」のまま直ったつもりになっていた。
    std::set<int> used;
    for (const auto& command : CommandCatalog()) {
        used.insert(static_cast<int>(command.predicate));
    }
    const SelectionPredicate all[] = {
        SelectionPredicate::Always,
        SelectionPredicate::HasDocument,
        SelectionPredicate::HasUndo,
        SelectionPredicate::HasRedo,
        SelectionPredicate::HasVisibleGeometry,
        SelectionPredicate::OneWorkPlane,
        SelectionPredicate::OnePlanarFaceOrWorkPlane,
        SelectionPredicate::ZeroOrOneGroup,
        SelectionPredicate::OneOrMoreWires,
        SelectionPredicate::TwoWireChains,
        SelectionPredicate::OneClosedProfile,
        SelectionPredicate::OneOrMoreClosedProfiles,
        SelectionPredicate::OnePart,
        SelectionPredicate::TwoParts,
        SelectionPredicate::OneDerivedEntity,
        SelectionPredicate::OneFabricationModel,
        SelectionPredicate::OneFabricationPanel,
        SelectionPredicate::OneOrMorePatterns,
        SelectionPredicate::OneOrMoreSelectedCurves,
        SelectionPredicate::OnePartOrSurface,
    };
    // まだどのコマンドにも付いていない条件。契約にはあるが、
    // それを使うコマンドがまだ無い。ここへ書いておけば、
    // 付け忘れと「まだ無い」の区別がつく。
    const SelectionPredicate notYetUsed[] = {
        // 閉じた輪郭ちょうど1つ ── 面を1枚だけ張るコマンドを入れるときに使う。
        SelectionPredicate::OneClosedProfile,
        // 曲線を1つ以上 ── 曲線そのものを対象にするコマンドを入れるときに使う。
        SelectionPredicate::OneOrMoreSelectedCurves,
    };
    std::string unused;
    for (const SelectionPredicate predicate : all) {
        if (used.find(static_cast<int>(predicate)) != used.end()) {
            continue;
        }
        bool known = false;
        for (const SelectionPredicate allowed : notYetUsed) {
            known = known || allowed == predicate;
        }
        if (!known) {
            unused += std::string(
                kachakacha::v2::app::SelectionBlockReasonJa(predicate)) + " ";
        }
    }
    Require(unused.empty(), "付け忘れた条件が無い: " + unused);
    // 「まだ無い」ほうも、使われだしたら書き換えること。
    for (const SelectionPredicate allowed : notYetUsed) {
        Require(used.find(static_cast<int>(allowed)) == used.end(),
            "まだ無いと書いた条件が、実は使われている: "
                + std::string(kachakacha::v2::app::SelectionBlockReasonJa(allowed)));
    }
}

KACHA_V2_TEST(availability, 台帳のすべての条件に判断がある)
{
    // 条件を足したのに判断を書き忘れると、そのコマンドは黙って押せなくなる。
    SelectionFacts everything;
    everything.hasDocument = true;
    everything.canUndo = true;
    everything.canRedo = true;
    everything.hasVisibleGeometry = true;
    everything.workPlanes = 1;
    everything.groups = 1;
    everything.wires = 2;
    everything.wireChains = 2;
    everything.closedProfiles = 1;
    everything.parts = 2;
    everything.guideSurfaces = 1;
    everything.derivedEntities = 1;
    everything.fabricationModels = 1;
    everything.fabricationPanels = 1;
    everything.patterns = 1;
    everything.curves = 2;
    std::vector<std::string> unreachable;
    for (const auto& command : CommandCatalog()) {
        if (!SelectionSatisfies(command.predicate, everything)) {
            unreachable.push_back(std::string(command.id));
        }
    }
    // 「ちょうど1つ」の条件は、上の材料(部品2つ)では通らない。
    // それ以外が通らないなら、判断の書き忘れである。
    std::vector<std::string> unexpected;
    for (const std::string& id : unreachable) {
        const bool wantsExactlyOnePart = id == "export.stl" || id == "export.step"
            || id == "export.validate" || id == "fabrication.create";
        if (!wantsExactlyOnePart) {
            unexpected.push_back(id);
        }
    }
    std::string joined;
    for (const std::string& id : unexpected) {
        joined += id + " ";
    }
    Require(unexpected.empty(), "判断のあるコマンドだけが残る: " + joined);
}

KACHA_V2_TEST_MAIN("command_availability_tests")
