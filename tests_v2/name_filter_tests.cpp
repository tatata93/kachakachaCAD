// 一覧の絞り込み(V1 の「名前・種類で絞り込み」欄)。
#include "kachakacha/app/NameFilter.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::NameMatchesFilter;
using kachakacha::v2::app::TrimmedFilterTerm;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

KACHA_V2_TEST(name_filter, 空の語は全部残す)
{
    Require(NameMatchesFilter("屋根の稜線", "ワイヤー", ""), "空なら残る");
    Require(NameMatchesFilter("", "", ""), "名前が空でも残る");
}

KACHA_V2_TEST(name_filter, 空白だけの語も空と同じ)
{
    Require(NameMatchesFilter("屋根の稜線", "ワイヤー", "   "), "空白だけなら残る");
    RequireEqual(std::string(TrimmedFilterTerm("  \t 屋根 \n ")), std::string("屋根"),
        "前後の空白は落ちる");
}

KACHA_V2_TEST(name_filter, 名前の一部で当たる)
{
    Require(NameMatchesFilter("屋根の稜線", "ワイヤー", "稜線"), "途中でも当たる");
    Require(!NameMatchesFilter("屋根の稜線", "ワイヤー", "側板"), "無ければ落ちる");
}

KACHA_V2_TEST(name_filter, 種類でも当たる)
{
    // V1 の欄は「名前・種類で絞り込み」。種類名だけでも探せる。
    Require(NameMatchesFilter("屋根の稜線", "ワイヤー", "ワイヤー"), "種類で当たる");
    Require(NameMatchesFilter("前面", "作業平面", "平面"), "種類の一部でも当たる");
}

KACHA_V2_TEST(name_filter, 英字の大文字小文字は区別しない)
{
    Require(NameMatchesFilter("Roof_Ridge", "Wire", "roof"), "小文字で探せる");
    Require(NameMatchesFilter("roof_ridge", "wire", "ROOF"), "大文字で探せる");
    Require(NameMatchesFilter("top_XY", "作業平面", "xy"), "原点の面も探せる");
}

KACHA_V2_TEST(name_filter, 日本語の途中で誤って当たらない)
{
    // UTF-8 はある文字の並びが別の文字の途中と一致しない。
    // バイトで探しても、切れた半端なところには当たらない。
    Require(!NameMatchesFilter("屋根", "ワイヤー", "根屋"), "並びが違えば落ちる");
    Require(NameMatchesFilter("屋根", "ワイヤー", "屋根"), "並びが同じなら当たる");
}

KACHA_V2_TEST(name_filter, 語が名前より長ければ落ちる)
{
    Require(!NameMatchesFilter("面", "点", "面取りの相手"), "長い語は当たらない");
}

KACHA_V2_TEST_MAIN("name_filter_tests")
