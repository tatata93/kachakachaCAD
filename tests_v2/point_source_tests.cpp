// 測定や既にある形から作図点を作る(AT-MEA-005 / PRD-072)。
#include "kachakacha/app/PointSources.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/document/Document.h"

#include <cmath>
#include <set>
#include <string>

using kachakacha::v2::app::MakePointDefinition;
using kachakacha::v2::app::PointCandidate;
using kachakacha::v2::app::PointCandidatesOfApproach;
using kachakacha::v2::app::PointCandidatesOfCurve;
using kachakacha::v2::app::PointCandidatesOfDistance;
using kachakacha::v2::app::PointDisplayNameJa;
using kachakacha::v2::app::PointSourceKind;
using kachakacha::v2::app::PointSourceKindNameJa;
using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::document::AddFeatureCommand;
using kachakacha::v2::document::AddGroupCommand;
using kachakacha::v2::document::Document;
using kachakacha::v2::document::Group;
using kachakacha::v2::document::SetActiveGroupCommand;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::FeatureOutput;
using kachakacha::v2::domain::FeatureType;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::MeasureCurveToCurve;
using kachakacha::v2::geometry::MeasureTwoPoints;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[nodiscard]] const PointCandidate* Find(const std::vector<PointCandidate>& candidates,
    PointSourceKind kind, std::size_t index = 0)
{
    for (const PointCandidate& candidate : candidates) {
        if (candidate.kind == kind && candidate.index == index) {
            return &candidate;
        }
    }
    return nullptr;
}

} // namespace

KACHA_V2_TEST(point_source, 出どころの名前がそろっている)
{
    std::set<std::string> names;
    for (PointSourceKind kind : {PointSourceKind::MeasuredPoint,
             PointSourceKind::MeasuredMidpoint, PointSourceKind::CurveCenter,
             PointSourceKind::CurveStart, PointSourceKind::CurveEnd,
             PointSourceKind::CurveMidpoint, PointSourceKind::ControlPoint,
             PointSourceKind::ClosestApproach}) {
        const std::string name = PointSourceKindNameJa(kind);
        Require(!name.empty() && name != "不明", "名前がある");
        Require(names.insert(name).second, "重ならない: " + name);
    }
}

KACHA_V2_TEST(point_source, 円の中心から点を作れる)
{
    const CurveSegment circle =
        CurveSegment::MakeCircle({12, -5, 3}, {0, 0, 1}, {1, 0, 0}, 20.0).Value();
    const auto candidates = PointCandidatesOfCurve(circle, std::nullopt);
    const PointCandidate* center = Find(candidates, PointSourceKind::CurveCenter);
    Require(center != nullptr, "中心がある");
    RequireNear(center->positionMm.x, 12.0, 1e-9, "x");
    RequireNear(center->positionMm.y, -5.0, 1e-9, "y");
    RequireNear(center->positionMm.z, 3.0, 1e-9, "z");
}

KACHA_V2_TEST(point_source, 円は始点と終点を二重に出さない)
{
    const CurveSegment circle =
        CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0).Value();
    const auto candidates = PointCandidatesOfCurve(circle, std::nullopt);
    Require(Find(candidates, PointSourceKind::CurveStart) != nullptr, "始点はある");
    Require(Find(candidates, PointSourceKind::CurveEnd) == nullptr, "終点は出さない");
}

KACHA_V2_TEST(point_source, 円弧は始点と終点と中心を出す)
{
    const CurveSegment arc =
        CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0, 0.0,
            3.14159265358979323846)
            .Value();
    const auto candidates = PointCandidatesOfCurve(arc, std::nullopt);
    Require(Find(candidates, PointSourceKind::CurveStart) != nullptr, "始点");
    Require(Find(candidates, PointSourceKind::CurveEnd) != nullptr, "終点");
    Require(Find(candidates, PointSourceKind::CurveCenter) != nullptr, "中心");
    const PointCandidate* mid = Find(candidates, PointSourceKind::CurveMidpoint);
    Require(mid != nullptr, "中点");
    // 半円の中点は真上。
    RequireNear(mid->positionMm.y, 10.0, 1e-9, "真上");
}

KACHA_V2_TEST(point_source, ベジェの制御点を全部出す)
{
    const CurveSegment bezier =
        CurveSegment::MakeCubicBezier({{0, 0, 0}, {5, 12, 0}, {18, 12, 0}, {24, 0, 0}})
            .Value();
    const auto candidates = PointCandidatesOfCurve(bezier, std::nullopt);
    for (std::size_t index = 0; index < 4; ++index) {
        const PointCandidate* found = Find(candidates, PointSourceKind::ControlPoint, index);
        Require(found != nullptr, "制御点" + std::to_string(index + 1) + "がある");
        RequireEqual(found->labelJa, "制御点" + std::to_string(index + 1), "名前");
    }
    const PointCandidate* second = Find(candidates, PointSourceKind::ControlPoint, 1);
    RequireNear(second->positionMm.x, 5.0, 1e-9, "位置");
    RequireNear(second->positionMm.y, 12.0, 1e-9, "位置");
}

KACHA_V2_TEST(point_source, Bスプラインの制御点も出す)
{
    const CurveSegment spline = CurveSegment::MakeCubicBSpline(
        {{0, 0, 0}, {5, 8, 0}, {12, 2, 0}, {18, 9, 0}, {24, 4, 0}})
                                    .Value();
    const auto candidates = PointCandidatesOfCurve(spline, std::nullopt);
    int controls = 0;
    for (const PointCandidate& candidate : candidates) {
        controls += candidate.kind == PointSourceKind::ControlPoint ? 1 : 0;
    }
    RequireEqual(std::to_string(controls), "5", "5点");
}

KACHA_V2_TEST(point_source, 直線は中心も制御点も出さない)
{
    const CurveSegment line = CurveSegment::MakeLine({0, 0, 0}, {10, 0, 0}).Value();
    const auto candidates = PointCandidatesOfCurve(line, std::nullopt);
    Require(Find(candidates, PointSourceKind::CurveCenter) == nullptr, "中心はない");
    Require(Find(candidates, PointSourceKind::ControlPoint) == nullptr, "制御点はない");
    RequireEqual(std::to_string(candidates.size()), "3", "始点・終点・中点");
}

KACHA_V2_TEST(point_source, 同じ入力なら同じ並びで出る)
{
    const CurveSegment bezier =
        CurveSegment::MakeCubicBezier({{0, 0, 0}, {5, 12, 0}, {18, 12, 0}, {24, 0, 0}})
            .Value();
    const auto first = PointCandidatesOfCurve(bezier, std::nullopt);
    const auto second = PointCandidatesOfCurve(bezier, std::nullopt);
    RequireEqual(std::to_string(first.size()), std::to_string(second.size()), "同じ数");
    for (std::size_t index = 0; index < first.size(); ++index) {
        RequireEqual(first[index].labelJa, second[index].labelJa, "同じ並び");
    }
}

KACHA_V2_TEST(point_source, 2点測定から両端と中点を作れる)
{
    const auto measured = MeasureTwoPoints(Vector3{0, 0, 0}, Vector3{30, 40, 0});
    Require(measured.HasValue(), "測れる");
    const auto candidates = PointCandidatesOfDistance(measured.Value());
    RequireEqual(std::to_string(candidates.size()), "3", "両端と中点");
    const PointCandidate* mid = Find(candidates, PointSourceKind::MeasuredMidpoint);
    Require(mid != nullptr, "中点がある");
    RequireNear(mid->positionMm.x, 15.0, 1e-9, "中点x");
    RequireNear(mid->positionMm.y, 20.0, 1e-9, "中点y");
}

KACHA_V2_TEST(point_source, 最接近から点を作れる)
{
    const CurveSegment first = CurveSegment::MakeLine({0, 0, 0}, {10, 0, 0}).Value();
    const CurveSegment second = CurveSegment::MakeLine({0, 5, 0}, {10, 5, 0}).Value();
    const auto approach = MeasureCurveToCurve(first, second);
    const auto candidates = PointCandidatesOfApproach(approach);
    RequireEqual(std::to_string(candidates.size()), "3", "2点と中点");
    // 中点は2本の真ん中の高さにある。
    RequireNear(candidates[2].positionMm.y, 2.5, 1e-6, "真ん中");
}

KACHA_V2_TEST(point_source, 候補から作図点の定義を作れる)
{
    const CurveSegment circle =
        CurveSegment::MakeCircle({7, 8, 9}, {0, 0, 1}, {1, 0, 0}, 5.0).Value();
    const auto candidates = PointCandidatesOfCurve(circle, std::nullopt);
    const PointCandidate* center = Find(candidates, PointSourceKind::CurveCenter);
    const auto definition = MakePointDefinition(*center);
    Require(definition.HasValue(), "作れる");
    RequireNear(definition.Value().positionMm.x, 7.0, 1e-9, "x");
    RequireNear(definition.Value().positionMm.z, 9.0, 1e-9, "z");
    // 測った位置から作った点は座標が正本。見かけの式を持たせない。
    Require(definition.Value().xExpression.expression.empty(), "式を持たない");
}

KACHA_V2_TEST(point_source, 数値でない位置からは点を作らない)
{
    PointCandidate broken;
    broken.positionMm = Vector3{std::nan(""), 0.0, 0.0};
    broken.labelJa = "中心";
    const auto refused = MakePointDefinition(broken);
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "MEA-M001", "数値でない");
}

KACHA_V2_TEST(point_source, 由来の分かる名前になる)
{
    PointCandidate candidate;
    candidate.kind = PointSourceKind::CurveCenter;
    candidate.labelJa = "中心";
    RequireEqual(PointDisplayNameJa(candidate, "円1"), "円1の中心", "由来つき");
    RequireEqual(PointDisplayNameJa(candidate, ""), "中心", "由来が無ければそのまま");
}

KACHA_V2_TEST(point_source, 作った点は作業中グループへ入る)
{
    // AT-MEA-005 の「active group所属」。
    DeterministicIdGenerator ids(41);
    Document document(ids.NextTyped<IdKind::Document>());
    Group measured;
    measured.id = ids.NextTyped<IdKind::Group>();
    measured.displayName = "測定";
    (void)document.Run(AddGroupCommand(measured));
    (void)document.Run(SetActiveGroupCommand(measured.id));

    const CurveSegment circle =
        CurveSegment::MakeCircle({12, -5, 3}, {0, 0, 1}, {1, 0, 0}, 20.0).Value();
    const auto candidates = PointCandidatesOfCurve(circle, std::nullopt);
    const PointCandidate* center = Find(candidates, PointSourceKind::CurveCenter);
    const auto definition = MakePointDefinition(*center);
    Require(definition.HasValue(), "作れる");

    Feature feature;
    feature.id = ids.NextTyped<IdKind::Feature>();
    feature.type = FeatureType::CreatePoint;
    feature.displayName = PointDisplayNameJa(*center, "円1");
    feature.definition = definition.Value();
    Entity entity;
    entity.id = ids.NextTyped<IdKind::Entity>();
    entity.kind = EntityKind::Point;
    entity.displayName = feature.displayName;
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"point", entity.id, EntityKind::Point});

    const auto added = document.Run(AddFeatureCommand(feature, {entity}, "測定から点"));
    Require(added.committed, "入れられた");
    const Entity* stored = document.FindEntity(entity.id);
    Require(stored != nullptr, "ある");
    Require(stored->groupId.has_value() && *stored->groupId == measured.id,
        "測定グループへ入る");
    RequireEqual(stored->displayName, "円1の中心", "名前も残る");
}

KACHA_V2_TEST_MAIN("point_source_tests")
