//! 開いた文書の ID と、これから振る ID を重ねない(app/DocumentIds)。
//! オーナー報告 2026-09-27: 開いた文書で面を作る・固定すると DOC-C003 で断られることがあった。

#include "kachakacha/app/DocumentIds.h"

#include "kachakacha/base/Ids.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/io/DocumentFile.h"

#include <string>

using kachakacha::v2::app::AdvanceIdsPastDocument;
using kachakacha::v2::app::MaxDeterministicCounterIn;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

KACHA_V2_TEST(document_ids, 決定的なIDから通し番号が読める)
{
    DeterministicIdGenerator ids(41);
    const auto id = ids.Next();
    const auto counter = DeterministicIdGenerator::CounterOf(id);
    Require(counter.has_value() && *counter == 41, "41 番が読める: " + id.ToString());
    Require(!DeterministicIdGenerator::CounterOf(kachakacha::v2::base::Uuid{}).has_value(),
        "空の ID は決定的ではない");
    RequireEqual(std::to_string(MaxDeterministicCounterIn("x " + id.ToString() + " y "
                     + DeterministicIdGenerator(7).Next().ToString())),
        std::string("41"), "文字列から最大の番号を拾う");
}

KACHA_V2_TEST(document_ids, 開いた文書のどのIDよりも先へ進める)
{
    // 前の回: 番号 1〜 で要素を作った文書。
    DeterministicIdGenerator earlier(1);
    kachakacha::v2::document::DocumentSnapshot snapshot;
    for (int k = 0; k < 5; ++k) {
        kachakacha::v2::domain::Entity entity;
        entity.id = earlier.NextTyped<kachakacha::v2::base::IdKind::Entity>();
        entity.kind = kachakacha::v2::domain::EntityKind::Wire;
        entity.displayName = "線";
        snapshot.entities.push_back(entity);
    }
    // 今の回: 生成器はまた 1 から。進めなければ 1 番目の要素とぶつかる。
    DeterministicIdGenerator now(1);
    const auto advanced = AdvanceIdsPastDocument(now, snapshot);
    Require(advanced >= 6, "文書の 5 つの ID(1〜5)より先へ進む: " + std::to_string(advanced));
    const auto fresh = now.Next();
    for (const auto& entity : snapshot.entities) {
        Require(!(entity.id.Value() == fresh), "新しい ID は文書のどれとも重ならない");
    }
    // すでに先へ進んでいれば戻さない。
    DeterministicIdGenerator far(100);
    AdvanceIdsPastDocument(far, snapshot);
    RequireEqual(std::to_string(far.Counter()), std::string("100"), "戻さない");
}

KACHA_V2_TEST_MAIN("document_ids_tests")
