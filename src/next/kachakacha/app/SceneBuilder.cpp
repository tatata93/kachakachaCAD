#include "kachakacha/app/SceneBuilder.h"

#include "kachakacha/domain/Feature.h"

#include <variant>

namespace kachakacha::v2::app {
namespace {

using domain::CreatePointDefinition;
using domain::CreateWireDefinition;
using domain::Visibility;
using modeling::SnapCurve;
using modeling::SnapDrawingPoint;

//! そのFeatureが作ったEntity。無ければ値を持たない。
[[nodiscard]] const domain::Entity* EntityOf(const document::DocumentSnapshot& snapshot,
    const domain::Feature& feature)
{
    for (const domain::FeatureOutput& output : feature.outputs) {
        for (const domain::Entity& entity : snapshot.entities) {
            if (entity.id == output.entityId) {
                return &entity;
            }
        }
    }
    return nullptr;
}

} // namespace

modeling::SnapScene BuildSceneFromDocument(const document::DocumentSnapshot& snapshot,
    base::IdGenerator& ids)
{
    modeling::SnapScene scene;
    for (const domain::Feature& feature : snapshot.features) {
        const domain::Entity* entity = EntityOf(snapshot, feature);
        if (entity == nullptr || entity->visibility != Visibility::Visible) {
            continue;
        }
        if (!feature.enabled) {
            continue;
        }
        if (const auto* wire = std::get_if<CreateWireDefinition>(&feature.definition)) {
            for (std::size_t index = 0; index < wire->segments.size(); ++index) {
                // Segment の ID は定義が持っていればそれを使う。
                // 使わずに振り直すと、保存して開くたびに ID が変わってしまう。
                const base::SegmentId segmentId = index < wire->segmentIds.size()
                    ? wire->segmentIds[index]
                    : ids.NextTyped<base::IdKind::Segment>();
                scene.curves.push_back(SnapCurve{entity->id, segmentId,
                    wire->segments[index], wire->construction, entity->datum});
            }
            continue;
        }
        if (const auto* point = std::get_if<CreatePointDefinition>(&feature.definition)) {
            scene.points.push_back(SnapDrawingPoint{entity->id, point->positionMm});
        }
    }
    return scene;
}

modeling::SnapScene RebuildSceneKeepingView(const modeling::SnapScene& current,
    const document::DocumentSnapshot& snapshot, base::IdGenerator& ids)
{
    modeling::SnapScene scene = BuildSceneFromDocument(snapshot, ids);
    // 作業平面とグリッドは画面の都合であって、文書の中身ではない。
    // ファイルを開いたからといって、見ている場所まで変えない。
    scene.grid = current.grid;
    scene.workPlane = current.workPlane;
    return scene;
}

} // namespace kachakacha::v2::app
