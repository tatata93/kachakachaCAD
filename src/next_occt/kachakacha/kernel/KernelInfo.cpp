#include "kachakacha/kernel/KernelInfo.h"

#ifdef KACHACAD_V2_WITH_OCCT
#include <Standard_Version.hxx>
#endif

namespace kachakacha::v2::kernel {

std::string KernelVersionString()
{
#ifdef KACHACAD_V2_WITH_OCCT
    return std::string("Open CASCADE ") + OCC_VERSION_STRING;
#else
    return std::string("kernel disabled");
#endif
}

bool IsKernelAvailable()
{
#ifdef KACHACAD_V2_WITH_OCCT
    return true;
#else
    return false;
#endif
}

} // namespace kachakacha::v2::kernel
