#pragma once

#include "kachakacha/modeling/ShapeMesh.h"

#include <cstddef>
#include <optional>
#include <string>

namespace kachakacha::v2::kernel::detail {
// Private cache: keys contain native geometry, topology, placement and quality.
// No native handle or editable geometry is retained.
[[nodiscard]] std::optional<modeling::ShapeMesh> CachedMesh(const std::string& key);
void RememberMesh(std::string key, const modeling::ShapeMesh& mesh);
void ClearMeshCache();
[[nodiscard]] std::size_t MeshCacheHits();
}
