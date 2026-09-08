#include "kachakacha/app/SampleDocument.h"

#include "kachakacha/domain/Feature.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <vector>

namespace kachakacha::v2::app {
namespace {

using base::DeterministicIdGenerator;
using base::IdKind;
using document::DocumentSnapshot;
using document::Group;
using domain::CreateWireDefinition;
using domain::Entity;
using domain::EntityKind;
using domain::Feature;
using domain::FeatureOutput;
using domain::FeatureType;
using geometry::CurveSegment;
using geometry::Vector3;

//! 1/87。ER1/ER2 の幅 3520mm を割ったものと同じ基準を使う。
constexpr double kScaleDenominator = 87.0;

[[nodiscard]] double Scaled(double realMm) noexcept
{
    return realMm / kScaleDenominator;
}

//! 見本を組み立てる道具。IDは決まった並びで振る。
struct Builder {
    DeterministicIdGenerator ids{101};
    DocumentSnapshot snapshot;

    void AddWire(const std::string& name, std::vector<CurveSegment> segments,
        bool construction, const base::GroupId& groupId)
    {
        Feature feature;
        feature.id = ids.NextTyped<IdKind::Feature>();
        feature.type = FeatureType::CreateWire;
        feature.displayName = name;
        CreateWireDefinition definition;
        definition.segments = std::move(segments);
        for (std::size_t index = 0; index < definition.segments.size(); ++index) {
            definition.segmentIds.push_back(ids.NextTyped<IdKind::Segment>());
        }
        definition.construction = construction;
        feature.definition = std::move(definition);

        Entity entity;
        entity.id = ids.NextTyped<IdKind::Entity>();
        entity.kind = EntityKind::Wire;
        entity.displayName = name;
        entity.createdBy = feature.id;
        entity.construction = construction;
        entity.groupId = groupId;
        feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});
        snapshot.evaluationOrder.push_back(feature.id);
        snapshot.features.push_back(std::move(feature));
        snapshot.entities.push_back(std::move(entity));
    }
};

//! 角を丸めない四角。型紙の外形にそのまま使える。
[[nodiscard]] std::vector<CurveSegment> Rectangle(double x, double y, double width,
    double height)
{
    std::vector<CurveSegment> segments;
    segments.push_back(CurveSegment::MakeLine({x, y, 0.0}, {x + width, y, 0.0}).Value());
    segments.push_back(
        CurveSegment::MakeLine({x + width, y, 0.0}, {x + width, y + height, 0.0}).Value());
    segments.push_back(
        CurveSegment::MakeLine({x + width, y + height, 0.0}, {x, y + height, 0.0}).Value());
    segments.push_back(CurveSegment::MakeLine({x, y + height, 0.0}, {x, y, 0.0}).Value());
    return segments;
}

} // namespace

std::string_view SampleDocumentVersion() noexcept
{
    return "1";
}

io::DocumentFile BuildSampleDocument()
{
    Builder builder;
    DocumentSnapshot& snapshot = builder.snapshot;
    snapshot.id = base::DocumentId::Parse("00000000-0000-4000-8000-0000000000a1").value();
    snapshot.revision = 1;

    Group body;
    body.id = builder.ids.NextTyped<IdKind::Group>();
    body.displayName = "車体";
    snapshot.groups.push_back(body);

    Group windows;
    windows.id = builder.ids.NextTyped<IdKind::Group>();
    windows.displayName = "窓";
    windows.parentId = body.id;
    snapshot.groups.push_back(windows);

    // 側面の外形。実物 20000mm x 3000mm を 1/87 にする。
    const double sideLength = Scaled(20000.0);
    const double sideHeight = Scaled(3000.0);
    builder.AddWire("側面の外形", Rectangle(0.0, 0.0, sideLength, sideHeight), false,
        body.id);

    // 腰の高さ。折るところの目印なので補助線にする。切る線ではない。
    std::vector<CurveSegment> waist;
    waist.push_back(CurveSegment::MakeLine({0.0, Scaled(900.0), 0.0},
        {sideLength, Scaled(900.0), 0.0}).Value());
    builder.AddWire("腰の高さ", std::move(waist), true, body.id);

    // 窓6枚。等間隔に並べる。窓の間隔も実物の寸法から出す。
    const double windowWidth = Scaled(1200.0);
    const double windowHeight = Scaled(900.0);
    const double windowBottom = Scaled(1500.0);
    const double firstLeft = Scaled(2400.0);
    const double pitch = Scaled(2600.0);
    for (int index = 0; index < 6; ++index) {
        builder.AddWire("窓" + std::to_string(index + 1),
            Rectangle(firstLeft + pitch * static_cast<double>(index), windowBottom,
                windowWidth, windowHeight),
            false, windows.id);
    }

    // 前照灯。丸いものが1つ入っていると、円が円のまま保たれることを目で確かめられる。
    std::vector<CurveSegment> headlamp;
    headlamp.push_back(CurveSegment::MakeCircle(
        {Scaled(1200.0), Scaled(2400.0), 0.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0},
        Scaled(300.0)).Value());
    builder.AddWire("前照灯", std::move(headlamp), false, body.id);

    io::DocumentFile file;
    file.snapshot = std::move(snapshot);
    file.metadata.title = "見本 1/87 側面";
    file.metadata.author = "kachakachaCAD";
    file.metadata.description =
        "マニュアルの手順で使う見本。実物 20000mm x 3000mm の側面を 1/87 にしたもの。"
        "外形・腰の高さ(補助線)・窓6枚・前照灯が入っている。立体は入っていない。";
    return file;
}

base::Result<std::string> BuildSampleArchive()
{
    return io::SaveDocument(BuildSampleDocument());
}

} // namespace kachakacha::v2::app
