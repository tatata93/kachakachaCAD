// 一覧で名前を変える(v1-input-parity.md §2-5 の F2)。
#include "kachakacha/app/EntityNaming.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::NameActuallyChanges;
using kachakacha::v2::app::NormalizeEntityName;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

KACHA_V2_TEST(entity_naming, 前後の空白を落とす)
{
    // 落とさないと「線」と「線 」が別の名前に見える。
    const auto made = NormalizeEntityName("  屋根の線  ");
    Require(made.HasValue(), "通る");
    RequireEqual(made.Value(), std::string("屋根の線"), "前後が落ちる");
}

KACHA_V2_TEST(entity_naming, 全角の空白も落とす)
{
    // 日本語入力のまま打つと全角空白が入る。見えないので気づけない。
    const auto made = NormalizeEntityName("　側板　");
    Require(made.HasValue(), "通る");
    RequireEqual(made.Value(), std::string("側板"), "全角空白が落ちる");
}

KACHA_V2_TEST(entity_naming, 中の空白は残す)
{
    const auto made = NormalizeEntityName("屋根 の 線");
    Require(made.HasValue(), "通る");
    RequireEqual(made.Value(), std::string("屋根 の 線"), "中は触らない");
}

KACHA_V2_TEST(entity_naming, 空の名前は断る)
{
    Require(!NormalizeEntityName("").HasValue(), "空は断る");
    Require(!NormalizeEntityName("   ").HasValue(), "空白だけも断る");
    Require(!NormalizeEntityName("　　").HasValue(), "全角空白だけも断る");
}

KACHA_V2_TEST(entity_naming, 断るときはコードで言う)
{
    const auto made = NormalizeEntityName("  ");
    Require(!made.HasValue(), "断る");
    Require(!made.Diagnostics().empty(), "理由がある");
    RequireEqual(made.Diagnostics().front().code, std::string("UI-N001"), "UI-N001");
}

KACHA_V2_TEST(entity_naming, 変わらない付け替えは変わらないと言う)
{
    // 同じ名前を入れ直すたびに履歴が伸びると、元に戻すが効かなくなる。
    Require(!NameActuallyChanges("側板", "側板"), "同じなら変わらない");
    Require(NameActuallyChanges("側板", "屋根"), "違えば変わる");
    Require(NameActuallyChanges("", "側板"), "名前なしから付けるのは変わる");
}

KACHA_V2_TEST_MAIN("entity_naming_tests")
