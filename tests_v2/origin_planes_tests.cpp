// 原点の基準平面(app/OriginPlanes.h)。V1 の一覧の「原点」ノード。
#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/io/DocumentFile.h"

#include <string>

using kachakacha::v2::app::EnsureOriginPlanes;
using kachakacha::v2::app::IsOriginPlane;
using kachakacha::v2::app::OriginPlaneId;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::document::Document;
using kachakacha::v2::modeling::StandardPlaneKind;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

KACHA_V2_TEST(origin_planes, 新しい文書に3平面が入り二度目は足さない)
{
    DeterministicIdGenerator ids{5};
    Document document{ids.NextTyped<kachakacha::v2::base::IdKind::Document>()};
    std::array<EntityId, 3> made{};
    Require(EnsureOriginPlanes(document, ids, &made), "足した");
    RequireEqual(std::to_string(document.Snapshot().entities.size()), std::string("3"), "3平面");
    Require(OriginPlaneId(document.Snapshot(), StandardPlaneKind::XY) == made[0], "XY");
    Require(OriginPlaneId(document.Snapshot(), StandardPlaneKind::ZX) == made[1], "XZ");
    Require(OriginPlaneId(document.Snapshot(), StandardPlaneKind::YZ) == made[2], "YZ");
    Require(document.Snapshot().entities[0].displayName == "top_XY", "V1 と同じ名前");
    Require(IsOriginPlane(document.Snapshot(), made[0]), "原点の平面");
    Require(!EnsureOriginPlanes(document, ids, nullptr), "二度目は変えない");
    RequireEqual(std::to_string(document.Snapshot().entities.size()), std::string("3"), "増えない");
}

KACHA_V2_TEST(origin_planes, 消せず名前も変えられない)
{
    DeterministicIdGenerator ids{5};
    Document document{ids.NextTyped<kachakacha::v2::base::IdKind::Document>()};
    std::array<EntityId, 3> made{};
    Require(EnsureOriginPlanes(document, ids, &made), "足した");
    const auto* entity = document.FindEntity(made[0]);
    Require(entity != nullptr, "ある");
    const auto removed = document.Run(kachakacha::v2::document::RemoveFeatureCommand(
        entity->createdBy, kachakacha::v2::document::RemovePolicy::RefuseIfUsed, "削除"));
    Require(!removed.committed, "消せない");
    RequireEqual(removed.diagnostics.front().code, std::string("DOC-C009"), "理由の番号");
    const auto renamed = document.Run(
        kachakacha::v2::document::RenameEntityCommand(made[0], "床"));
    Require(!renamed.committed, "改名できない");
    RequireEqual(renamed.diagnostics.front().code, std::string("DOC-C009"), "理由の番号");
    // 隠すことはできる。
    const auto hidden = document.Run(kachakacha::v2::document::SetVisibilityCommand(
        {made[0]}, kachakacha::v2::domain::Visibility::Hidden));
    Require(hidden.committed, "隠せる");
}

KACHA_V2_TEST(origin_planes, 保存して読み直しても原点のまま)
{
    DeterministicIdGenerator ids{5};
    Document document{ids.NextTyped<kachakacha::v2::base::IdKind::Document>()};
    std::array<EntityId, 3> made{};
    Require(EnsureOriginPlanes(document, ids, &made), "足した");
    kachakacha::v2::io::DocumentFile file;
    file.snapshot = document.Snapshot();
    const auto read = kachakacha::v2::io::ReadDocumentJson(
        kachakacha::v2::io::WriteDocumentJson(file));
    Require(read.HasValue(), "読める");
    Require(IsOriginPlane(read.Value().snapshot, made[0]), "印が残る");
    Require(OriginPlaneId(read.Value().snapshot, StandardPlaneKind::YZ) == made[2], "向きも残る");
}

KACHA_V2_TEST_MAIN("origin_planes")
