// V2ターゲットが実際にリンクでき、空でないことを確かめる(WP-01 Gate)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/base/Version.h"
#include "kachakacha/kernel/KernelInfo.h"

#include <string>

using kachakacha::v2::base::MigrationNoticeJa;
using kachakacha::v2::base::ProductVersionString;
using kachakacha::v2::kernel::IsKernelAvailable;
using kachakacha::v2::kernel::KernelVersionString;
using kachakacha::v2::test::Require;

KACHA_V2_TEST(scaffold, v2_core_links_and_reports_a_version)
{
    const std::string version = ProductVersionString();
    Require(!version.empty(), "the V2 core reports a non-empty version");
    Require(version != "0.0.0",
        "CMake passes the real project version into the V2 core");
}

KACHA_V2_TEST(scaffold, the_migration_notice_is_present)
{
    Require(MigrationNoticeJa() == std::string("V2準備中"),
        "the shell states that V2 is still being prepared");
}

KACHA_V2_TEST(scaffold, the_kernel_adapter_links)
{
    const std::string kernel = KernelVersionString();
    Require(!kernel.empty(), "the kernel adapter reports something");
    if (IsKernelAvailable()) {
        Require(kernel.find("Open CASCADE") != std::string::npos,
            "with OCCT enabled the adapter names Open CASCADE");
    } else {
        Require(kernel == std::string("kernel disabled"),
            "without OCCT the adapter says so instead of pretending");
    }
}

KACHA_V2_TEST_MAIN("scaffold_tests")
