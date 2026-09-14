#include "kachakacha/app/GroupTree.h"

namespace kachakacha::v2::app {
namespace {

[[nodiscard]] const Group* Find(const DocumentSnapshot& snapshot, const GroupId& id)
{
    for (const Group& group : snapshot.groups) {
        if (group.id == id) {
            return &group;
        }
    }
    return nullptr;
}

} // namespace

bool GroupChainVisible(const DocumentSnapshot& snapshot,
    const std::optional<GroupId>& groupId)
{
    std::optional<GroupId> walk = groupId;
    // まとまりの数だけ辿れば必ず終わる。壊れた文書で無限に回らないための保険。
    for (std::size_t step = 0; step <= snapshot.groups.size() && walk.has_value(); ++step) {
        const Group* group = Find(snapshot, *walk);
        if (group == nullptr) {
            return true;   // 無いまとまりは出ている扱い。隠して行方不明にしない。
        }
        if (!group->visible) {
            return false;
        }
        walk = group->parentId;
    }
    return true;
}

bool EntityEffectivelyVisible(const DocumentSnapshot& snapshot,
    const domain::Entity& entity)
{
    if (entity.visibility != domain::Visibility::Visible) {
        return false;
    }
    return GroupChainVisible(snapshot, entity.groupId);
}

std::size_t GroupDepth(const DocumentSnapshot& snapshot, const GroupId& groupId)
{
    std::size_t depth = 0;
    const Group* group = Find(snapshot, groupId);
    while (group != nullptr && group->parentId.has_value()
        && depth <= snapshot.groups.size()) {
        ++depth;
        group = Find(snapshot, *group->parentId);
    }
    return depth;
}

std::string GroupPathJa(const DocumentSnapshot& snapshot, const GroupId& groupId)
{
    std::vector<std::string> names;
    const Group* group = Find(snapshot, groupId);
    for (std::size_t step = 0; group != nullptr && step <= snapshot.groups.size(); ++step) {
        names.push_back(group->displayName);
        if (!group->parentId.has_value()) {
            break;
        }
        group = Find(snapshot, *group->parentId);
    }
    std::string path;
    for (auto name = names.rbegin(); name != names.rend(); ++name) {
        if (!path.empty()) {
            path += "/";
        }
        path += *name;
    }
    return path;
}

std::vector<GroupId> ChildGroupsOf(const DocumentSnapshot& snapshot,
    const std::optional<GroupId>& parentId)
{
    std::vector<GroupId> children;
    for (const Group& group : snapshot.groups) {
        if (group.parentId == parentId) {
            children.push_back(group.id);
        }
    }
    return children;
}

std::vector<base::EntityId> EntitiesUnderGroup(const DocumentSnapshot& snapshot,
    const GroupId& groupId)
{
    std::vector<base::EntityId> found;
    for (const domain::Entity& entity : snapshot.entities) {
        std::optional<GroupId> walk = entity.groupId;
        for (std::size_t step = 0; step <= snapshot.groups.size() && walk.has_value();
            ++step) {
            if (*walk == groupId) {
                found.push_back(entity.id);
                break;
            }
            const Group* group = Find(snapshot, *walk);
            walk = group == nullptr ? std::nullopt : group->parentId;
        }
    }
    return found;
}

} // namespace kachakacha::v2::app
