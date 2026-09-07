#include "kachakacha/base/Version.h"

namespace kachakacha::v2::base {

std::string ProductVersionString()
{
#ifdef KACHACAD_V2_VERSION
    return std::string(KACHACAD_V2_VERSION);
#else
    return std::string("0.0.0");
#endif
}

std::string MigrationNoticeJa()
{
    return std::string("V2準備中");
}

} // namespace kachakacha::v2::base
