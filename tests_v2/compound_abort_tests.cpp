// P1-EXTRUDE-R2 の指摘 B1 の受入。
// 押し出しは「面の境界線を作る → 立体を作る → 線を出す → 元の立体を隠す」を
// ひとまとめでやる。途中のどれか1つでも失敗したら、押す前に戻さなければならない。
//
// ここで見るのは3つ。
//  1. 途中で失敗しても、文書の中身・版番号・表示の入切が押す前と同じであること。
//  2. 失敗が「取り消し」の履歴を1つも増やさないこと。
//  3. 失敗が、押す前に利用者がやっていた別の操作を巻き添えで消さないこと。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/document/Document.h"

#include <string>
#include <vector>

using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::DocumentId;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::document::AddFeatureCommand;
using kachakacha::v2::document::Document;
using kachakacha::v2::document::DocumentSnapshot;
using kachakacha::v2::document::SetVisibilityCommand;
using kachakacha::v2::domain::CreatePointDefinition;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::FeatureOutput;
using kachakacha::v2::domain::FeatureType;
using kachakacha::v2::domain::Visibility;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

//! 点を1つ作るFeatureと、その出力Entityの組。押し出しの各段の代わり。
struct Step {
    Feature feature;
    Entity entity;
};

//! 試験用の作り手。押し出しの1段ぶんを組み立てる。
class Maker {
public:
    [[nodiscard]] Step Make(const std::string& name)
    {
        Step step;
        step.feature.id = ids_.NextTyped<IdKind::Feature>();
        step.feature.type = FeatureType::CreatePoint;
        step.feature.displayName = name;
        step.entity.id = ids_.NextTyped<IdKind::Entity>();
        step.entity.kind = EntityKind::Point;
        step.entity.displayName = name;
        step.entity.createdBy = step.feature.id;
        step.feature.outputs.push_back(FeatureOutput{"point", step.entity.id, EntityKind::Point});
        step.feature.definition = CreatePointDefinition{{0.0, 0.0, 0.0}};
        return step;
    }

private:
    DeterministicIdGenerator ids_{4100};
};

//! 押す前の状態。あとで「本当に戻ったか」を突き合わせるために取っておく。
struct Before {
    std::size_t entityCount = 0;
    std::uint64_t revision = 0;
    std::size_t undoDepth = 0;
    std::string undoLabel;
    Visibility targetVisibility = Visibility::Visible;
};

[[nodiscard]] std::size_t UndoDepth(Document& document)
{
    // 履歴の深さは直接は見えないので、取り消せる回数を数えて、やり直して戻す。
    std::size_t depth = 0;
    while (document.CanUndo()) {
        Require(document.Undo(), "取り消せること");
        ++depth;
    }
    for (std::size_t index = 0; index < depth; ++index) {
        Require(document.Redo(), "やり直せること");
    }
    return depth;
}

[[nodiscard]] Visibility VisibilityOf(const DocumentSnapshot& snapshot, EntityId id)
{
    for (const auto& entity : snapshot.entities) {
        if (entity.id == id) {
            return entity.visibility;
        }
    }
    return Visibility::Hidden;
}

//! 「押す前」に、利用者が別の操作を1つやってある文書を作る。
struct Fixture {
    Document document{DocumentId{}};
    Maker maker;
    EntityId earlier;

    Fixture()
    {
        const auto first = maker.Make("押す前にやっていた点");
        const auto placed
            = document.Run(AddFeatureCommand(first.feature, {first.entity}, "点を置く"));
        Require(placed.committed, "下ごしらえが通ること");
        earlier = first.entity.id;
    }

    [[nodiscard]] Before Capture()
    {
        Before before;
        before.entityCount = document.Snapshot().entities.size();
        before.revision = document.Snapshot().revision;
        before.undoDepth = UndoDepth(document);
        before.undoLabel = document.UndoLabel();
        before.targetVisibility = VisibilityOf(document.Snapshot(), earlier);
        return before;
    }

    void RequireSameAs(const Before& before, const std::string& where)
    {
        Require(document.Snapshot().entities.size() == before.entityCount,
            where + ": 物の数が押す前と同じこと");
        Require(document.Snapshot().revision == before.revision,
            where + ": 版番号が押す前と同じこと");
        Require(UndoDepth(document) == before.undoDepth,
            where + ": 取り消しの履歴が増えていないこと");
        RequireEqual(document.UndoLabel(), before.undoLabel,
            where + ": 履歴の一番上が押す前のままであること");
        Require(VisibilityOf(document.Snapshot(), earlier) == before.targetVisibility,
            where + ": 表示の入切が押す前と同じこと");
    }
};

//! 何段目で失敗したことにするかを変えながら、まとめて1回ぶんを流す。
//! failAt が 0 なら最初の段で、last なら最後の段で失敗する。
void RunAbortedExtrude(Fixture& fixture, std::size_t stepCount, std::size_t failAt)
{
    Document::Transaction transaction(fixture.document, "押し出し");
    for (std::size_t index = 0; index < stepCount; ++index) {
        if (index == failAt) {
            // ここで失敗した。Commit を呼ばずに抜けると、Transaction が戻す。
            return;
        }
        const auto step = fixture.maker.Make("押し出しの途中" + std::to_string(index));
        const auto made = fixture.document.Run(
            AddFeatureCommand(step.feature, {step.entity}, "途中"));
        Require(made.committed, "途中の段は通ること");
    }
    const auto hidden = fixture.document.Run(
        SetVisibilityCommand({fixture.earlier}, Visibility::Hidden));
    Require(hidden.committed, "元を隠せること");
    transaction.Commit();
}

}  // namespace

KACHA_V2_TEST(compound_abort, failing_on_the_first_step_leaves_everything_as_it_was)
{
    Fixture fixture;
    const auto before = fixture.Capture();
    RunAbortedExtrude(fixture, 4, 0);
    fixture.RequireSameAs(before, "最初の段で失敗");
}

KACHA_V2_TEST(compound_abort, failing_in_the_middle_leaves_everything_as_it_was)
{
    Fixture fixture;
    const auto before = fixture.Capture();
    RunAbortedExtrude(fixture, 4, 2);
    fixture.RequireSameAs(before, "途中の段で失敗");
}

KACHA_V2_TEST(compound_abort, failing_on_the_last_step_leaves_everything_as_it_was)
{
    Fixture fixture;
    const auto before = fixture.Capture();
    RunAbortedExtrude(fixture, 4, 3);
    fixture.RequireSameAs(before, "最後の段で失敗");
}

KACHA_V2_TEST(compound_abort, a_failure_does_not_undo_what_the_user_did_before)
{
    Fixture fixture;
    RunAbortedExtrude(fixture, 4, 2);
    // 押す前にやっていた点は残っていなければならない。
    Require(fixture.document.Snapshot().entities.size() == 1,
        "押す前にやっていた操作が生きていること");
    // そして、その1つは今でも取り消せる。巻き添えで履歴から消えていない。
    Require(fixture.document.CanUndo(), "押す前の操作を取り消せること");
    RequireEqual(fixture.document.UndoLabel(), std::string("点を置く"),
        "取り消せるのは押す前の操作であること");
    Require(fixture.document.Undo(), "取り消せること");
    Require(fixture.document.Snapshot().entities.empty(), "空に戻ること");
}

KACHA_V2_TEST(compound_abort, a_successful_run_is_one_undo_step)
{
    Fixture fixture;
    Document::Transaction transaction(fixture.document, "押し出し");
    for (int index = 0; index < 3; ++index) {
        const auto step = fixture.maker.Make("押し出しの途中" + std::to_string(index));
        Require(fixture.document.Run(
                        AddFeatureCommand(step.feature, {step.entity}, "途中"))
                     .committed,
            "途中の段が通ること");
    }
    Require(fixture.document
                 .Run(SetVisibilityCommand({fixture.earlier}, Visibility::Hidden))
                 .committed,
        "元を隠せること");
    transaction.Commit();

    Require(fixture.document.Snapshot().entities.size() == 4, "4つになること");
    Require(VisibilityOf(fixture.document.Snapshot(), fixture.earlier)
            == Visibility::Hidden,
        "元が隠れていること");
    RequireEqual(fixture.document.UndoLabel(), std::string("押し出し"),
        "履歴の名前が押し出しであること");

    Require(fixture.document.Undo(), "1回の取り消し");
    Require(fixture.document.Snapshot().entities.size() == 1,
        "作った物と、隠したことが、まとめて1回で戻ること");
    Require(VisibilityOf(fixture.document.Snapshot(), fixture.earlier)
            == Visibility::Visible,
        "隠したのも一緒に戻ること");

    Require(fixture.document.Redo(), "1回のやり直し");
    Require(fixture.document.Snapshot().entities.size() == 4, "やり直しで4つに戻ること");
    Require(VisibilityOf(fixture.document.Snapshot(), fixture.earlier)
            == Visibility::Hidden,
        "やり直しで元がまた隠れること");
}

KACHA_V2_TEST(compound_abort, an_abort_inside_an_abort_still_returns_to_the_start)
{
    Fixture fixture;
    const auto before = fixture.Capture();
    {
        Document::Transaction outer(fixture.document, "外側");
        const auto step = fixture.maker.Make("外側で作った点");
        Require(fixture.document.Run(
                        AddFeatureCommand(step.feature, {step.entity}, "外側"))
                     .committed,
            "外側の段が通ること");
        {
            Document::Transaction inner(fixture.document, "内側");
            const auto nested = fixture.maker.Make("内側で作った点");
            Require(fixture.document.Run(
                            AddFeatureCommand(nested.feature, {nested.entity}, "内側"))
                         .committed,
                "内側の段が通ること");
            // 内側は Commit する。外側が失敗したら、内側ごと戻る。
            inner.Commit();
        }
        // 外側は Commit しない。
    }
    fixture.RequireSameAs(before, "入れ子の外側で失敗");
}

KACHA_V2_TEST_MAIN("compound_abort_tests")
