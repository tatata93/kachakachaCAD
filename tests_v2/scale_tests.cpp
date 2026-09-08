// 大きな文書と、長い連続操作(AT-PER-003 / AT-PER-004)。
//
// 目標は「速いこと」そのものよりも、**壊れないこと**。
//   - Entityが勝手に増えない。
//   - IDがぶつからない。
//   - Undoを繰り返しても文書が元へ戻る。
//   - 保存して読み直すと同じものになる。
// 時間も測る。目標(読込5秒・保存5秒)から外れたら、数字を添えて失敗させる。
// クラウドの機械はWindows基準機より遅いこともあるので、余裕を持たせてある。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/io/DocumentFile.h"

#include <chrono>
#include <set>
#include <string>
#include <vector>

using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::DocumentId;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::FeatureId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::document::AddFeatureCommand;
using kachakacha::v2::document::Document;
using kachakacha::v2::document::RemoveFeatureCommand;
using kachakacha::v2::document::RemovePolicy;
using kachakacha::v2::document::RenameEntityCommand;
using kachakacha::v2::domain::CreateWireDefinition;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::FeatureOutput;
using kachakacha::v2::domain::FeatureType;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::io::DocumentFile;
using kachakacha::v2::io::LoadDocument;
using kachakacha::v2::io::ReadDocumentJson;
using kachakacha::v2::io::SaveDocument;
using kachakacha::v2::io::WriteDocumentJson;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

[[nodiscard]] double SecondsSince(
    const std::chrono::steady_clock::time_point& start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

//! ワイヤーを1本作る Feature と Entity。
struct Made {
    Feature feature;
    Entity entity;
};

[[nodiscard]] Made MakeWire(DeterministicIdGenerator& ids, int index, int segmentCount)
{
    Made made;
    made.feature.id = ids.NextTyped<IdKind::Feature>();
    made.feature.type = FeatureType::CreateWire;
    made.feature.displayName = "輪郭" + std::to_string(index);

    CreateWireDefinition definition;
    const double base = static_cast<double>(index) * 0.5;
    for (int at = 0; at < segmentCount; ++at) {
        const double x0 = base + static_cast<double>(at) * 2.0;
        const auto segment = CurveSegment::MakeLine({x0, base, 0.0},
            {x0 + 2.0, base + (at % 2 == 0 ? 1.0 : -1.0), 0.0});
        Require(segment.HasValue(), "線が作れること");
        definition.segments.push_back(segment.Value());
        definition.segmentIds.push_back(ids.NextTyped<IdKind::Segment>());
    }
    made.feature.definition = std::move(definition);

    made.entity.id = ids.NextTyped<IdKind::Entity>();
    made.entity.kind = EntityKind::Wire;
    made.entity.displayName = made.feature.displayName;
    made.entity.createdBy = made.feature.id;
    made.feature.outputs.push_back(
        FeatureOutput{"wire", made.entity.id, EntityKind::Wire});
    return made;
}

//! 1000本のワイヤーを持つ文書。
[[nodiscard]] DocumentFile MakeLargeDocument(int wireCount, int segmentsEach)
{
    DeterministicIdGenerator ids{1000};
    DocumentFile file;
    file.snapshot.id =
        DocumentId::Parse("00000000-0000-4000-8000-0000000000aa").value();
    file.metadata.title = "大きな文書";
    for (int index = 0; index < wireCount; ++index) {
        Made made = MakeWire(ids, index, segmentsEach);
        file.snapshot.features.push_back(std::move(made.feature));
        file.snapshot.entities.push_back(std::move(made.entity));
    }
    return file;
}

} // namespace

KACHA_V2_TEST(scale, 千本のワイヤーを保存して読み直せる)
{
    // AT-PER-003。1000 Wire。段は3本ずつなので Segment は 3000 本。
    const DocumentFile file = MakeLargeDocument(1000, 3);
    RequireCount(file.snapshot.entities.size(), 1000, "ワイヤーの数");

    const auto saveStart = std::chrono::steady_clock::now();
    const auto saved = SaveDocument(file);
    const double saveSeconds = SecondsSince(saveStart);
    Require(saved.HasValue(), "保存できること");
    Require(saveSeconds < 5.0,
        "保存が5秒未満であること (" + std::to_string(saveSeconds) + " 秒)");

    const auto loadStart = std::chrono::steady_clock::now();
    const auto loaded = LoadDocument(saved.Value());
    const double loadSeconds = SecondsSince(loadStart);
    Require(loaded.HasValue(), "読み込めること");
    Require(loadSeconds < 5.0,
        "読込が5秒未満であること (" + std::to_string(loadSeconds) + " 秒)");

    RequireCount(loaded.Value().snapshot.entities.size(), 1000, "読み直した数");
    RequireCount(loaded.Value().snapshot.features.size(), 1000, "指示の数");
    // 中身も同じであること。
    Require(WriteDocumentJson(loaded.Value()) == WriteDocumentJson(file),
        "同じ文書になること");
}

KACHA_V2_TEST(scale, 大きな文書でもIDがぶつからない)
{
    const DocumentFile file = MakeLargeDocument(1000, 3);
    std::set<std::string> ids;
    for (const auto& entity : file.snapshot.entities) {
        Require(ids.insert(entity.id.ToString()).second,
            "オブジェクトのIDが重ならないこと");
    }
    for (const auto& feature : file.snapshot.features) {
        Require(ids.insert(feature.id.ToString()).second, "指示のIDが重ならないこと");
        const auto& wire = std::get<CreateWireDefinition>(feature.definition);
        for (const auto& segment : wire.segmentIds) {
            Require(ids.insert(segment.ToString()).second, "線のIDが重ならないこと");
        }
    }
    RequireCount(ids.size(), 1000 + 1000 + 3000, "IDの総数");
}

KACHA_V2_TEST(scale, 大きな文書の検証が現実的な時間で終わる)
{
    const DocumentFile file = MakeLargeDocument(1000, 3);
    const auto start = std::chrono::steady_clock::now();
    const auto diagnostics = Document::Validate(file.snapshot);
    const double seconds = SecondsSince(start);
    Require(diagnostics.empty(),
        "検証が通ること (" + (diagnostics.empty() ? std::string() : diagnostics.front().summaryJa)
            + ")");
    Require(seconds < 5.0, "検証が5秒未満 (" + std::to_string(seconds) + " 秒)");
}

KACHA_V2_TEST(scale, 大きな文書の評価順が現実的な時間で決まる)
{
    const DocumentFile file = MakeLargeDocument(1000, 3);
    const auto start = std::chrono::steady_clock::now();
    const auto order = Document::TopologicalOrder(file.snapshot);
    const double seconds = SecondsSince(start);
    RequireCount(order.size(), 1000, "全部の指示が並ぶこと");
    Require(seconds < 5.0, "並べ替えが5秒未満 (" + std::to_string(seconds) + " 秒)");
}

KACHA_V2_TEST(scale, 五百回の操作を繰り返しても壊れない)
{
    // AT-PER-004。作図 → Undo → 作図 → 保存 を繰り返す。
    DeterministicIdGenerator ids{2000};
    Document document(DocumentId::Parse("00000000-0000-4000-8000-0000000000bb").value());
    std::set<std::string> everSeen;
    const auto start = std::chrono::steady_clock::now();

    for (int round = 0; round < 500; ++round) {
        Made made = MakeWire(ids, round, 2);
        const EntityId createdId = made.entity.id;
        Require(everSeen.insert(createdId.ToString()).second, "IDが使い回されないこと");

        const auto added = document.Run(
            AddFeatureCommand(made.feature, {made.entity}, "作図"));
        Require(added.committed, "足せること " + std::to_string(round));

        if (round % 3 == 0) {
            // 取り消して、やり直す。
            Require(document.Undo(), "取り消せること");
            Require(document.Redo(), "やり直せること");
        }
        if (round % 5 == 0) {
            // 名前を変えて、取り消す。
            Require(document.Run(RenameEntityCommand(createdId, "変えた名前")).committed,
                "名前を変えられること");
            Require(document.Undo(), "取り消せること");
            RequireEqual(document.FindEntity(createdId)->displayName,
                made.entity.displayName, "名前が戻ること");
        }
        if (round % 7 == 0) {
            // 消して、取り消す。
            Require(document.Run(RemoveFeatureCommand(made.feature.id,
                                     RemovePolicy::RefuseIfUsed, "消す"))
                        .committed,
                "消せること");
            Require(document.FindEntity(createdId) == nullptr, "消えたこと");
            Require(document.Undo(), "取り消せること");
            Require(document.FindEntity(createdId) != nullptr, "戻ったこと");
        }
    }
    const double seconds = SecondsSince(start);

    // 増えも減りもしていないこと。
    RequireCount(document.Snapshot().entities.size(), 500, "オブジェクトの数");
    RequireCount(document.Snapshot().features.size(), 500, "指示の数");
    Require(Document::Validate(document.Snapshot()).empty(), "壊れていないこと");
    Require(seconds < 30.0, "500操作が30秒未満 (" + std::to_string(seconds) + " 秒)");
}

KACHA_V2_TEST(scale, 全部取り消すと空の文書へ戻る)
{
    DeterministicIdGenerator ids{3000};
    Document document(DocumentId::Parse("00000000-0000-4000-8000-0000000000cc").value());
    for (int round = 0; round < 200; ++round) {
        Made made = MakeWire(ids, round, 2);
        Require(document.Run(AddFeatureCommand(made.feature, {made.entity}, "作図"))
                    .committed,
            "足せること");
    }
    RequireCount(document.Snapshot().entities.size(), 200, "200本ある");
    int undone = 0;
    while (document.Undo()) {
        ++undone;
        Require(undone <= 300, "無限に取り消せてしまわないこと");
    }
    RequireCount(undone, 200, "取り消した回数");
    Require(document.Snapshot().entities.empty(), "空へ戻ること");
    Require(document.Snapshot().features.empty(), "指示も空");

    // 全部やり直せること。
    int redone = 0;
    while (document.Redo()) {
        ++redone;
        Require(redone <= 300, "無限にやり直せてしまわないこと");
    }
    RequireCount(redone, 200, "やり直した回数");
    RequireCount(document.Snapshot().entities.size(), 200, "戻ること");
}

KACHA_V2_TEST(scale, 保存を繰り返してもファイルが太らない)
{
    DocumentFile file = MakeLargeDocument(200, 3);
    const auto first = SaveDocument(file);
    Require(first.HasValue(), "保存できること");
    const std::size_t size = first.Value().size();
    for (int round = 0; round < 5; ++round) {
        const auto loaded = LoadDocument(first.Value());
        Require(loaded.HasValue(), "読めること");
        const auto again = SaveDocument(loaded.Value());
        Require(again.HasValue(), "保存できること");
        RequireCount(again.Value().size(), size, "大きさが変わらないこと");
        Require(again.Value() == first.Value(), "バイト単位で同じこと");
    }
}

KACHA_V2_TEST(scale, 一万本でも読み書きできる)
{
    // 目標(1000本)の10倍。ここまでは現実的な時間で動くことを確かめておく。
    const DocumentFile file = MakeLargeDocument(10000, 1);
    const auto start = std::chrono::steady_clock::now();
    const auto saved = SaveDocument(file);
    Require(saved.HasValue(), "保存できること");
    const auto loaded = LoadDocument(saved.Value());
    Require(loaded.HasValue(), "読めること");
    const double seconds = SecondsSince(start);
    RequireCount(loaded.Value().snapshot.entities.size(), 10000, "数");
    Require(seconds < 60.0, "1万本で60秒未満 (" + std::to_string(seconds) + " 秒)");
}

KACHA_V2_TEST_MAIN("scale_tests")
