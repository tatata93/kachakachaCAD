#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctGuideSurface.h"

#include <cstdint>
#include <map>
#include <mutex>

namespace kachakacha::v2::kernel {

namespace {

std::mutex& CacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

#ifdef KACHACAD_V2_WITH_OCCT
using StoredShape = TopoDS_Shape;
#else
using StoredShape = int;
#endif

std::map<std::uint64_t, StoredShape>& CacheTable()
{
    static std::map<std::uint64_t, StoredShape> table;
    return table;
}

} // namespace

#ifdef KACHACAD_V2_WITH_OCCT

modeling::KernelShapeHandle StoreShape(const TopoDS_Shape& shape)
{
    std::lock_guard<std::mutex> lock(CacheMutex());
    static std::uint64_t next = 1;
    const std::uint64_t value = next++;
    CacheTable().emplace(value, shape);
    return modeling::KernelShapeHandle{value};
}

bool LookupShape(modeling::KernelShapeHandle handle, TopoDS_Shape& out)
{
    std::lock_guard<std::mutex> lock(CacheMutex());
    const auto found = CacheTable().find(handle.value);
    if (found == CacheTable().end()) {
        return false;
    }
    out = found->second;
    return true;
}

#endif

bool ReleaseShape(modeling::KernelShapeHandle handle)
{
    std::lock_guard<std::mutex> lock(CacheMutex());
    return CacheTable().erase(handle.value) > 0;
}

bool HasShape(modeling::KernelShapeHandle handle)
{
    std::lock_guard<std::mutex> lock(CacheMutex());
    return CacheTable().find(handle.value) != CacheTable().end();
}

std::size_t CachedShapeCount()
{
    std::lock_guard<std::mutex> lock(CacheMutex());
    return CacheTable().size();
}

void ClearShapeCache()
{
    std::lock_guard<std::mutex> lock(CacheMutex());
    CacheTable().clear();
}

} // namespace kachakacha::v2::kernel
