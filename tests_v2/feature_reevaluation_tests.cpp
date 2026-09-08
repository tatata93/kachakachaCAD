// Feature の連続編集と再評価(AT-EXT-008)。
//
// 大事なのは1つだけ。輪郭の寸法・押し出しの距離・相手の位置を変えても、
// 出力の EntityId は変わらず、形だけが計算し直されることである。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/document/FeatureReevaluation.h"

#include <cmath>
#include <string>

using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::FeatureId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::base::SegmentId;
using kachakacha::v2::document::AddFeatureCommand;
using kachakacha::v2::document::CheckDefinitionSwap;
using kachakacha::v2::document::Document;
using kachakacha::v2::document::PlanReevaluation;
using kachakacha::v2::document::ReevaluateFeature;
using kachakacha::v2::document::UpdateFeatureDefinitionCommand;
using kachakacha::v2::domain::CreatePointDefinition;
using kachakacha::v2::domain::CreateWireDefinition;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::FeatureOutput;
using kachakacha::v2::domain::FeatureType;
using kachakacha::v2::domain::TransformWireDefinition;
using kachakacha::v2::domain::WireTransformMethod;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[nodiscard]] CurveSegment Line(Vector3 start, Vector3 end)
{
    return CurveSegment::MakeLine(start, end).Value();
}

//! 長方形の輪郭。幅と高さを変えられる。
[[nodiscard]] std::vector<CurveSegment> Rectangle(double width, double height)
{
    return {Line({0, 0, 0}, {width, 0, 0}), Line({width, 0, 0}, {width, height, 0}),
        Line({width, height, 0}, {0, height, 0}), Line({0, height, 0}, {0, 0, 0})};
}

struct Made {
    Feature feature;
    Entity entity;
};

struct Maker {
    DeterministicIdGenerator ids{31};

    [[nodiscard]] Made MakeWire(const std::string& name,
        std::vector<CurveSegment> segments)
    {
        Made made;
        made.feature.id = ids.NextTyped<IdKind::Feature>();
        made.feature.type = FeatureType::CreateWire;
        made.feature.displayName = name;
        CreateWireDefinition definition;
        definition.segments = std::move(segments);
        for (std::size_t index = 0; index < definition.segments.size(); ++index) {
            definition.segmentIds.push_back(ids.NextTyped<IdKind::Segment>());
        }
        made.feature.definition = definition;
        made.entity.id = ids.NextTyped<IdKind::Entity>();
        made.entity.kind = EntityKind::Wire;
        made.entity.displayName = name;
        made.entity.createdBy = made.feature.id;
        made.feature.outputs.push_back(
            FeatureOutput{"wire", made.entity.id, EntityKind::Wire});
        return made;
    }

    [[nodiscard]] Made MakeMove(const std::string& name, EntityId input, Vector3 delta)
    {
        Made made;
        made.feature.id = ids.NextTyped<IdKind::Feature>();
        made.feature.type = FeatureType::TransformWire;
        made.feature.displayName = name;
        made.feature.inputEntityIds.push_back(input);
        TransformWireDefinition definition;
        definition.method = WireTransformMethod::Move;
        definition.vectorArgument = delta;
        made.feature.definition = definition;
        made.entity.id = ids.NextTyped<IdKind::Entity>();
        made.entity.kind = EntityKind::Wire;
        made.entity.displayName = name;
        made.entity.createdBy = made.feature.id;
        made.feature.outputs.push_back(
            FeatureOutput{"wire", made.entity.id, EntityKind::Wire});
        return made;
    }

    [[nodiscard]] Made MakePoint(const std::string& name, Vector3 position)
    {
        Made made;
        made.feature.id = ids.NextTyped<IdKind::Feature>();
        made.feature.type = FeatureType::CreatePoint;
        made.feature.displayName = name;
        CreatePointDefinition definition;
        definition.positionMm = position;
        made.feature.definition = definition;
        made.entity.id = ids.NextTyped<IdKind::Entity>();
        made.entity.kind = EntityKind::Point;
        made.entity.displayName = name;
        made.entity.createdBy = made.feature.id;
        made.feature.outputs.push_back(
            FeatureOutput{"point", made.entity.id, EntityKind::Point});
        return made;
    }
};

//! 輪郭 -> 移動 -> もう一度移動、という3段の文書を作る。
struct Chain {
    Maker maker;
    Document document{DeterministicIdGenerator(77).NextTyped<IdKind::Document>()};
    Made profile;
    Made moved;
    Made movedAgain;

    Chain()
        : profile(maker.MakeWire("輪郭", Rectangle(40.0, 30.0)))
    {
        (void)document.Run(AddFeatureCommand(profile.feature, {profile.entity}, "輪郭"));
        moved = maker.MakeMove("持ち上げ", profile.entity.id, Vector3{0, 0, 10});
        (void)document.Run(AddFeatureCommand(moved.feature, {moved.entity}, "持ち上げ"));
        movedAgain = maker.MakeMove("横へ", moved.entity.id, Vector3{5, 0, 0});
        (void)document.Run(
            AddFeatureCommand(movedAgain.feature, {movedAgain.entity}, "横へ"));
    }
};

} // namespace

KACHA_V2_TEST(reeval, 輪郭の寸法を変えても出力のIDは変わらない)
{
    Chain chain;
    const EntityId before = chain.profile.entity.id;
    const auto first = ReevaluateFeature(chain.document.Snapshot(),
        chain.profile.feature.id);
    Require(first.HasValue(), "計算できる");
    RequireNear(first.Value().outputs[0].segments[0].EndPoint().x, 40.0, 1e-9, "幅40");

    // 幅を 40 -> 90 にする。
    CreateWireDefinition wider;
    wider.segments = Rectangle(90.0, 30.0);
    const auto edited = chain.document.Run(UpdateFeatureDefinitionCommand(
        chain.profile.feature.id, wider, {}, "輪郭の寸法を変える"));
    Require(edited.committed, "変えられた");

    const auto after = ReevaluateFeature(chain.document.Snapshot(),
        chain.profile.feature.id);
    Require(after.HasValue(), "計算できる");
    RequireEqual(after.Value().outputs[0].entityId.ToString(), before.ToString(),
        "IDは同じ");
    RequireNear(after.Value().outputs[0].segments[0].EndPoint().x, 90.0, 1e-9, "幅90");
}

KACHA_V2_TEST(reeval, 出力の安定キーも変わらない)
{
    Chain chain;
    CreateWireDefinition wider;
    wider.segments = Rectangle(90.0, 30.0);
    (void)chain.document.Run(UpdateFeatureDefinitionCommand(chain.profile.feature.id,
        wider, {}, "変える"));
    const auto after = ReevaluateFeature(chain.document.Snapshot(),
        chain.profile.feature.id);
    RequireEqual(after.Value().outputs[0].key, "wire", "キーは同じ");
}

KACHA_V2_TEST(reeval, 変えるたびに版が上がる)
{
    Chain chain;
    const Feature* before = chain.document.FindFeature(chain.profile.feature.id);
    const std::uint64_t revision = before->revision;
    CreateWireDefinition wider;
    wider.segments = Rectangle(90.0, 30.0);
    (void)chain.document.Run(UpdateFeatureDefinitionCommand(chain.profile.feature.id,
        wider, {}, "変える"));
    const Feature* after = chain.document.FindFeature(chain.profile.feature.id);
    RequireEqual(std::to_string(after->revision), std::to_string(revision + 1), "1つ上がる");
}

KACHA_V2_TEST(reeval, 下流も上流から順に計算し直す)
{
    Chain chain;
    const auto plan = PlanReevaluation(chain.document.Snapshot(),
        chain.profile.feature.id);
    Require(plan.HasValue(), "順番が出る");
    RequireEqual(std::to_string(plan.Value().order.size()), "3", "3段すべて");
    RequireEqual(plan.Value().order[0].ToString(), chain.profile.feature.id.ToString(),
        "輪郭が先");
    RequireEqual(plan.Value().order[1].ToString(), chain.moved.feature.id.ToString(),
        "次が持ち上げ");
    RequireEqual(plan.Value().order[2].ToString(), chain.movedAgain.feature.id.ToString(),
        "最後が横へ");
    RequireEqual(std::to_string(plan.Value().affectedEntityIds.size()), "3", "3つ");
}

KACHA_V2_TEST(reeval, 途中を変えたら上流は計算し直さない)
{
    Chain chain;
    const auto plan = PlanReevaluation(chain.document.Snapshot(), chain.moved.feature.id);
    Require(plan.HasValue(), "順番が出る");
    RequireEqual(std::to_string(plan.Value().order.size()), "2", "自分と下流だけ");
    RequireEqual(plan.Value().order[0].ToString(), chain.moved.feature.id.ToString(),
        "自分が先");
    RequireEqual(plan.Value().order[1].ToString(), chain.movedAgain.feature.id.ToString(),
        "下流が続く");
}

KACHA_V2_TEST(reeval, 末端を変えたら自分だけ)
{
    Chain chain;
    const auto plan = PlanReevaluation(chain.document.Snapshot(),
        chain.movedAgain.feature.id);
    Require(plan.HasValue(), "順番が出る");
    RequireEqual(std::to_string(plan.Value().order.size()), "1", "自分だけ");
}

KACHA_V2_TEST(reeval, 距離を変えると下流の形だけが動く)
{
    Chain chain;
    const auto before = ReevaluateFeature(chain.document.Snapshot(),
        chain.moved.feature.id);
    Require(before.HasValue(), "計算できる");
    RequireNear(before.Value().outputs[0].segments[0].StartPoint().z, 10.0, 1e-9, "10mm上");
    const EntityId keptId = before.Value().outputs[0].entityId;

    // 10mm -> 25mm。
    TransformWireDefinition higher;
    higher.method = WireTransformMethod::Move;
    higher.vectorArgument = Vector3{0, 0, 25};
    const auto edited = chain.document.Run(UpdateFeatureDefinitionCommand(
        chain.moved.feature.id, higher, {chain.profile.entity.id}, "距離を変える"));
    Require(edited.committed, "変えられた");

    const auto after = ReevaluateFeature(chain.document.Snapshot(),
        chain.moved.feature.id);
    Require(after.HasValue(), "計算できる");
    RequireEqual(after.Value().outputs[0].entityId.ToString(), keptId.ToString(),
        "IDは同じ");
    RequireNear(after.Value().outputs[0].segments[0].StartPoint().z, 25.0, 1e-9, "25mm上");
}

KACHA_V2_TEST(reeval, 上流を変えると下流の形も変わる)
{
    Chain chain;
    CreateWireDefinition wider;
    wider.segments = Rectangle(90.0, 30.0);
    (void)chain.document.Run(UpdateFeatureDefinitionCommand(chain.profile.feature.id,
        wider, {}, "変える"));
    const auto downstream = ReevaluateFeature(chain.document.Snapshot(),
        chain.moved.feature.id);
    Require(downstream.HasValue(), "計算できる");
    // 移動しても幅は保たれる。上流の変更が伝わっている。
    RequireNear(downstream.Value().outputs[0].segments[0].EndPoint().x, 90.0, 1e-9,
        "幅90が伝わる");
    RequireNear(downstream.Value().outputs[0].segments[0].EndPoint().z, 10.0, 1e-9,
        "10mm上のまま");
}

KACHA_V2_TEST(reeval, 種類の違う定義への差し替えは断る)
{
    Maker maker;
    const Made wire = maker.MakeWire("輪郭", Rectangle(10.0, 10.0));
    CreatePointDefinition point;
    point.positionMm = Vector3{1, 2, 3};
    const auto refused = CheckDefinitionSwap(wire.feature, point);
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "DOC-C006", "種類が変わる");

    CreateWireDefinition sameKind;
    sameKind.segments = Rectangle(20.0, 20.0);
    const auto allowed = CheckDefinitionSwap(wire.feature, sameKind);
    Require(allowed.HasValue(), "同じ種類なら通る");
}

KACHA_V2_TEST(reeval, 無い操作を指したら断る)
{
    Chain chain;
    DeterministicIdGenerator other(999);
    const FeatureId missing = other.NextTyped<IdKind::Feature>();
    const auto plan = PlanReevaluation(chain.document.Snapshot(), missing);
    Require(!plan.HasValue(), "断る");
    RequireEqual(FirstCode(plan.Diagnostics()), "DOC-C001", "見つからない");
    const auto evaluated = ReevaluateFeature(chain.document.Snapshot(), missing);
    Require(!evaluated.HasValue(), "断る");
    RequireEqual(FirstCode(evaluated.Diagnostics()), "DOC-C001", "見つからない");
}

KACHA_V2_TEST(reeval, 回転も鏡像も計算し直せる)
{
    Maker maker;
    Document document(DeterministicIdGenerator(55).NextTyped<IdKind::Document>());
    const Made wire = maker.MakeWire("線", {Line({10, 0, 0}, {20, 0, 0})});
    (void)document.Run(AddFeatureCommand(wire.feature, {wire.entity}, "線"));

    Made turned = maker.MakeMove("回す", wire.entity.id, Vector3{0, 0, 0});
    TransformWireDefinition rotate;
    rotate.method = WireTransformMethod::Rotate;
    rotate.pointArgument = Vector3{0, 0, 0};
    rotate.vectorArgument = Vector3{0, 0, 1};
    rotate.scalarArgument.value = kPi * 0.5;
    turned.feature.definition = rotate;
    (void)document.Run(AddFeatureCommand(turned.feature, {turned.entity}, "回す"));

    const auto result = ReevaluateFeature(document.Snapshot(), turned.feature.id);
    Require(result.HasValue(), "計算できる");
    RequireNear(result.Value().outputs[0].segments[0].StartPoint().y, 10.0, 1e-9,
        "X が Y へ回る");
}

KACHA_V2_TEST(reeval, 相手の線が要る編集は嘘をつかずに断る)
{
    Maker maker;
    Document document(DeterministicIdGenerator(56).NextTyped<IdKind::Document>());
    const Made wire = maker.MakeWire("線", {Line({0, 0, 0}, {10, 0, 0})});
    (void)document.Run(AddFeatureCommand(wire.feature, {wire.entity}, "線"));

    Made trimmed = maker.MakeMove("トリム", wire.entity.id, Vector3{});
    TransformWireDefinition trim;
    trim.method = WireTransformMethod::Trim;
    trimmed.feature.definition = trim;
    (void)document.Run(AddFeatureCommand(trimmed.feature, {trimmed.entity}, "トリム"));

    const auto result = ReevaluateFeature(document.Snapshot(), trimmed.feature.id);
    Require(!result.HasValue(), "断る");
    RequireEqual(FirstCode(result.Diagnostics()), "DOC-C007", "この場では計算できない");
}

KACHA_V2_TEST(reeval, 立体は核が要ると印をつけて返す)
{
    // core で形を作れないものを、作ったふりで返さない。
    Maker maker;
    Document document(DeterministicIdGenerator(57).NextTyped<IdKind::Document>());
    Made part = maker.MakePoint("部品", Vector3{});
    part.feature.type = FeatureType::Extrude;
    part.feature.definition = std::monostate{};
    part.feature.outputs.clear();
    part.feature.outputs.push_back(
        FeatureOutput{"part", part.entity.id, EntityKind::Part});
    part.entity.kind = EntityKind::Part;
    (void)document.Run(AddFeatureCommand(part.feature, {part.entity}, "押し出し"));

    const auto result = ReevaluateFeature(document.Snapshot(), part.feature.id);
    Require(result.HasValue(), "計算できる");
    Require(result.Value().outputs[0].needsKernel, "核が要る印がつく");
    Require(result.Value().outputs[0].segments.empty(), "形を作ったふりをしない");
    RequireEqual(result.Value().outputs[0].entityId.ToString(), part.entity.id.ToString(),
        "IDは持ったまま");
}

KACHA_V2_TEST(reeval, 作図点の位置を変えても同じIDのまま)
{
    Maker maker;
    Document document(DeterministicIdGenerator(58).NextTyped<IdKind::Document>());
    const Made point = maker.MakePoint("点", Vector3{1, 2, 3});
    (void)document.Run(AddFeatureCommand(point.feature, {point.entity}, "点"));

    CreatePointDefinition moved;
    moved.positionMm = Vector3{7, 8, 9};
    (void)document.Run(UpdateFeatureDefinitionCommand(point.feature.id, moved, {},
        "位置を変える"));

    const auto result = ReevaluateFeature(document.Snapshot(), point.feature.id);
    Require(result.HasValue(), "計算できる");
    RequireEqual(result.Value().outputs[0].entityId.ToString(), point.entity.id.ToString(),
        "IDは同じ");
    RequireNear(result.Value().outputs[0].positionMm.x, 7.0, 1e-9, "位置は変わる");
}

KACHA_V2_TEST(reeval, 何度編集してもIDは動かない)
{
    Chain chain;
    const std::string kept = chain.profile.entity.id.ToString();
    for (int round = 1; round <= 25; ++round) {
        CreateWireDefinition definition;
        definition.segments = Rectangle(10.0 * round, 5.0 * round);
        const auto edited = chain.document.Run(UpdateFeatureDefinitionCommand(
            chain.profile.feature.id, definition, {}, "変える"));
        Require(edited.committed, "変えられた");
        const auto result = ReevaluateFeature(chain.document.Snapshot(),
            chain.profile.feature.id);
        Require(result.HasValue(), "計算できる");
        RequireEqual(result.Value().outputs[0].entityId.ToString(), kept, "IDは同じ");
        RequireNear(result.Value().outputs[0].segments[0].EndPoint().x, 10.0 * round, 1e-9,
            "形は変わる");
    }
    // 下流の計画も、25回編集したあとで同じ形をしている。
    const auto plan = PlanReevaluation(chain.document.Snapshot(),
        chain.profile.feature.id);
    RequireEqual(std::to_string(plan.Value().order.size()), "3", "3段のまま");
}

KACHA_V2_TEST_MAIN("feature_reevaluation_tests")
