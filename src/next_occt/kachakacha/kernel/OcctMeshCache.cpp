#include "kachakacha/kernel/OcctMeshCache.h"

#include <list>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kachakacha::v2::kernel::detail {
namespace {
struct Entry {
    modeling::ShapeMesh mesh;
    std::size_t bytes;
    std::list<std::string>::iterator recent;
};
struct Cache {
    std::mutex mutex;
    std::list<std::string> recent;
    std::unordered_map<std::string, Entry> entries;
    std::size_t bytes = 0, hits = 0;
};
Cache& State() { static Cache cache; return cache; }
std::size_t MeshBytes(const modeling::ShapeMesh& mesh) {
    std::size_t bytes = mesh.triangles.capacity() * sizeof(modeling::MeshTriangle);
    for (const auto* lines : {&mesh.edges, &mesh.isoLines}) {
        bytes += lines->capacity() * sizeof(std::vector<geometry::Vector3>);
        for (const auto& line : *lines) bytes += line.capacity() * sizeof(geometry::Vector3);
    }
    return bytes + sizeof(Entry);
}
}
std::optional<modeling::ShapeMesh> CachedMesh(const std::string& key) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    const auto found = state.entries.find(key);
    if (found == state.entries.end()) return std::nullopt;
    state.recent.splice(state.recent.end(), state.recent, found->second.recent);
    ++state.hits;
    return found->second.mesh;
}
void RememberMesh(std::string key, const modeling::ShapeMesh& mesh) {
    constexpr std::size_t limit = 256 * 1024 * 1024;
    const std::size_t bytes = MeshBytes(mesh) + key.capacity() * 2;
    if (key.empty() || bytes > limit) return;
    auto& state = State(); std::lock_guard lock(state.mutex);
    if (state.entries.contains(key)) return;
    while (!state.recent.empty() && (state.bytes + bytes > limit || state.entries.size() >= 4096)) {
        const auto oldest = state.entries.find(state.recent.front());
        state.bytes -= oldest->second.bytes;
        state.entries.erase(oldest); state.recent.pop_front();
    }
    try { state.recent.push_back(key); } catch (...) { return; }
    try {
        state.entries.emplace(std::move(key), Entry{mesh, bytes, std::prev(state.recent.end())});
    } catch (...) {
        // Cache allocation is optional. Preserve the completed mesh and leave
        // no orphan LRU node that a later eviction could dereference.
        state.recent.pop_back(); return;
    }
    state.bytes += bytes;
}
void ClearMeshCache() {
    auto& state = State(); std::lock_guard lock(state.mutex);
    state.entries.clear(); state.recent.clear(); state.bytes = state.hits = 0;
}
std::size_t MeshCacheHits() {
    auto& state = State(); std::lock_guard lock(state.mutex); return state.hits;
}
}
