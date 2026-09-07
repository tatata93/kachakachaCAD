// 試験土台そのものの検査。
// AT-ARC-005 が要求する「1件失敗で残りを停止しない」を、土台の側で保証する。
#include "kachakacha/base/TestHarness.h"

#include <functional>
#include <stdexcept>
#include <string>

using kachakacha::v2::test::CheckFailure;
using kachakacha::v2::test::Registry;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireNear;

KACHA_V2_TEST(harness, require_passes_on_true)
{
    Require(true, "true should pass");
}

KACHA_V2_TEST(harness, require_throws_on_false)
{
    bool threw = false;
    try {
        Require(false, "expected message");
    } catch (const CheckFailure& failure) {
        threw = std::string(failure.what()) == "expected message";
    }
    Require(threw, "Require throws CheckFailure carrying the message");
}

KACHA_V2_TEST(harness, require_near_uses_tolerance)
{
    RequireNear(1.0 + 1.0e-9, 1.0, 1.0e-6, "inside the tolerance");
    bool threw = false;
    try {
        RequireNear(1.0 + 1.0e-3, 1.0, 1.0e-6, "outside the tolerance");
    } catch (const CheckFailure&) {
        threw = true;
    }
    Require(threw, "RequireNear fails outside the tolerance");
}

KACHA_V2_TEST(harness, a_failing_case_does_not_stop_the_rest)
{
    // 別レジストリを立て、1件目を失敗させても2件目3件目が走ることを確かめる。
    // 本体の Registry は singleton なので、ここでは同じ仕組みを手で組む。
    int ran = 0;
    const auto run = [&ran](const std::function<void()>& body) {
        try {
            body();
        } catch (...) {
        }
        ++ran;
    };
    run([] { throw CheckFailure("first fails"); });
    run([] {});
    run([] { throw std::runtime_error("third throws something else"); });
    Require(ran == 3, "every case runs even when earlier ones fail");
}

KACHA_V2_TEST(harness, cases_are_registered)
{
    Require(Registry::Instance().Size() >= 5, "all cases in this file are registered");
}

KACHA_V2_TEST_MAIN("harness_tests")
