//! 選んだものから押し出しの意味を読み取って、画面へ出す。
//!
//! オーナー指示 2026-09-14。ここまでの押し出しは
//! 「先にワイヤーを選んでください」で終わりだった。
//! 立体だけを選んだ人には、次に何をすればよいのかが分からない。
//!
//! 読み取りは core(app/ExtrudePlan)がやる。ここは文書から数を数えて渡し、
//! 返ってきた日本語を画面へ出すだけである。

#include "V2MainWindow.h"

#include "V2Viewport.h"

#include "kachakacha/app/ExtrudePlan.h"
#include "kachakacha/app/ProfileRegion.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/domain/Entity.h"

#include <QString>

#include <algorithm>
#include <string>
#include <vector>

kachakacha::v2::app::ExtrudePlan V2MainWindow::PlanExtrudeFromSelection() const
{
    using kachakacha::v2::domain::EntityKind;
    kachakacha::v2::app::ExtrudeSelectionFacts facts;
    std::vector<kachakacha::v2::base::EntityId> solids;
    std::vector<kachakacha::v2::base::EntityId> profiles;
    std::vector<kachakacha::v2::base::EntityId> wireIds;
    bool profilesAreFaces = false;

    const auto& selection = viewport_->Selection();
    const auto& document = session_->GetDocument();
    for (const auto& id : selection.entityIds) {
        const auto* entity = document.FindEntity(id);
        if (entity == nullptr) {
            continue;
        }
        switch (entity->kind) {
        case EntityKind::Part:
            ++facts.solids;
            solids.push_back(id);
            break;
        case EntityKind::GuideSurface:
            ++facts.surfaces;
            break;
        case EntityKind::Wire:
            wireIds.push_back(id);
            break;
        default:
            break;
        }
    }
    // 線の物体単位ではなく、端点接続で閉じた領域を読む。曲線の混在、5本以上、
    // 穴、複数領域を同じ規則で扱い、開いた余分な線は輪郭へ混ぜない。
    const auto regions = kachakacha::v2::app::DetectProfileRegions(session_->Scene(), wireIds,
        document.Snapshot().settings.tolerance);
    for (const auto& region : regions) {
        auto ids = kachakacha::v2::app::ProfileRegionEntityIds(region);
        for (const auto& id : ids) {
            if (std::find(profiles.begin(), profiles.end(), id) == profiles.end()) {
                profiles.push_back(id);
            }
        }
        facts.closedWires += 1 + static_cast<int>(region.holes.size());
    }
    facts.openWires = static_cast<int>(wireIds.size() - profiles.size());
    // 面の選択(部分要素)。立体の面を押し引きするときに使う。
    // いまの画面はまだ面を拾えない(拾えるのは物体まで)。拾えるようになったら
    // ここが数える。数え方を先に置いておくのは、拾う側と読む側を
    // 別々に入れられるようにするためである。
    for (const auto& ref : selection.ordered) {
        if (ref.kind == kachakacha::v2::app::SelectionElementKind::Face) {
            ++facts.faces;
            profiles.push_back(ref.entityId);
            profilesAreFaces = true;
        }
    }
    return kachakacha::v2::app::PlanExtrude(facts, solids, profiles, profilesAreFaces);
}

QString V2MainWindow::ExtrudePlanTextJa() const
{
    const auto plan = PlanExtrudeFromSelection();
    const auto& document = session_->GetDocument();
    const auto nameOf = [&document](const kachakacha::v2::base::EntityId& id) {
        const auto* entity = document.FindEntity(id);
        return entity != nullptr && !entity->displayName.empty() ? entity->displayName
                                                                 : std::string("名前のないもの");
    };
    std::string target;
    if (!plan.targetSolid.IsNil()) {
        target = nameOf(plan.targetSolid);
    }
    std::vector<std::string> names;
    names.reserve(plan.profiles.size());
    for (const auto& id : plan.profiles) {
        names.push_back(nameOf(id));
    }
    return QString::fromStdString(kachakacha::v2::app::ExplainExtrudePlanJa(plan, target,
        names, plan.defaultOperation));
}
