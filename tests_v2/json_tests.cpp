// .kcd2 の土台になる JSON。保存と読み込みが全部ここを通るので、多数の入力で押さえる。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/io/Json.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::io::JsonArray;
using kachakacha::v2::io::JsonObject;
using kachakacha::v2::io::JsonReader;
using kachakacha::v2::io::JsonType;
using kachakacha::v2::io::JsonValue;
using kachakacha::v2::io::ParseJson;
using kachakacha::v2::io::WriteJson;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

void RequireAccepts(const std::string& text, const std::string& why)
{
    const auto parsed = ParseJson(text);
    Require(parsed.HasValue(), why + " : " + text);
}

void RequireRejects(const std::string& text, const std::string& why)
{
    const auto parsed = ParseJson(text);
    Require(!parsed.HasValue(), why + " : \"" + text + "\"");
    Require(!parsed.Diagnostics().empty(), "the refusal carries a diagnostic: " + text);
    RequireEqual(parsed.Diagnostics().front().code, "KCD2-J001",
        "the refusal uses the syntax code: " + text);
}

//! 書いて読んで書いて、2回目と3回目が一致すること。
void RequireStableRoundTrip(const JsonValue& value, const std::string& why)
{
    const std::string first = WriteJson(value);
    const auto parsed = ParseJson(first);
    Require(parsed.HasValue(), "the written form parses back: " + why);
    const std::string second = WriteJson(parsed.Value());
    RequireEqual(second, first, "writing is stable across a round trip: " + why);
}

} // namespace

// ---- 受け付ける形 ----

KACHA_V2_TEST(json, accepts_every_scalar_form)
{
    for (const char* text : {"null", "true", "false", "0", "-0", "1", "-1", "123",
             "1.5", "-1.5", "0.0", "1e3", "1E3", "1e+3", "1e-3", "1.5e10", "-1.5E-10",
             "\"\"", "\"hello\"", "\"日本語\"", "\"a\\\"b\"", "\"tab\\there\"",
             "\"\\u3042\"", "\"\\ud83d\\ude00\""}) {
        RequireAccepts(text, "a valid scalar");
    }
}

KACHA_V2_TEST(json, accepts_nested_structures)
{
    for (const char* text : {"[]", "{}", "[1,2,3]", "[[1],[2]]", "{\"a\":1}",
             "{\"a\":{\"b\":{\"c\":[1,2,{\"d\":null}]}}}", "[{},[],\"\",0,true,null]",
             "  {  \"a\"  :  [ 1 , 2 ]  }  "}) {
        RequireAccepts(text, "a valid structure");
    }
}

// ---- 断る形 ----

KACHA_V2_TEST(json, rejects_malformed_input)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"", "empty input"},
        {"   ", "whitespace only"},
        {"{", "unterminated object"},
        {"[", "unterminated array"},
        {"\"unterminated", "unterminated string"},
        {"{\"a\":1,}", "trailing comma in an object"},
        {"[1,2,]", "trailing comma in an array"},
        {"{a:1}", "unquoted key"},
        {"{'a':1}", "single quoted key"},
        {"[1 2]", "missing comma"},
        {"{\"a\" 1}", "missing colon"},
        {"01", "leading zero"},
        {"-01", "negative leading zero"},
        {"1.", "trailing decimal point"},
        {".5", "leading decimal point"},
        {"1e", "exponent with no digits"},
        {"1e+", "exponent sign with no digits"},
        {"+1", "leading plus"},
        {"NaN", "NaN is not JSON"},
        {"Infinity", "Infinity is not JSON"},
        {"-Infinity", "negative infinity is not JSON"},
        {"nul", "truncated null"},
        {"TRUE", "uppercase true"},
        {"{\"a\":1} extra", "trailing content"},
        {"[1,2] [3]", "two documents"},
        {"{\"a\":1,\"a\":2}", "duplicate keys"},
        {"// comment\n1", "comments are not JSON"},
        {"/* comment */ 1", "block comments are not JSON"},
        {"\"bad\\xescape\"", "unknown escape"},
        {"\"\\u12\"", "short unicode escape"},
        {"\"\\ud800\"", "lone high surrogate"},
        {"\"\\udc00\"", "lone low surrogate"},
    };
    for (const auto& [text, why] : cases) {
        RequireRejects(text, why);
    }
}

KACHA_V2_TEST(json, rejects_a_raw_control_character_in_a_string)
{
    std::string text = "\"a";
    text.push_back('\n');
    text += "b\"";
    RequireRejects(text, "a raw newline inside a string");
}

KACHA_V2_TEST(json, rejects_a_byte_order_mark)
{
    std::string text = "\xEF\xBB\xBF{}";
    const auto parsed = ParseJson(text);
    Require(!parsed.HasValue(), "a BOM is refused rather than silently skipped");
}

KACHA_V2_TEST(json, rejects_input_that_is_nested_too_deeply)
{
    std::string text;
    for (int index = 0; index < 200; ++index) {
        text += '[';
    }
    for (int index = 0; index < 200; ++index) {
        text += ']';
    }
    const auto parsed = ParseJson(text);
    Require(!parsed.HasValue(), "200 levels of nesting is refused instead of overflowing");
}

// ---- 値が正しく読めるか ----

KACHA_V2_TEST(json, scalar_values_read_back_correctly)
{
    RequireNear(ParseJson("1.5").Value().AsNumber(), 1.5, 0.0, "a decimal");
    RequireNear(ParseJson("-2.5e3").Value().AsNumber(), -2500.0, 0.0, "an exponent");
    Require(ParseJson("true").Value().AsBool(), "true");
    Require(!ParseJson("false").Value().AsBool(), "false");
    Require(ParseJson("null").Value().IsNull(), "null");
    RequireEqual(ParseJson("\"abc\"").Value().AsString(), "abc", "a plain string");
}

KACHA_V2_TEST(json, escapes_decode_to_the_right_bytes)
{
    RequireEqual(ParseJson("\"a\\nb\"").Value().AsString(), "a\nb", "newline escape");
    RequireEqual(ParseJson("\"a\\\\b\"").Value().AsString(), "a\\b", "backslash escape");
    RequireEqual(ParseJson("\"a\\\"b\"").Value().AsString(), "a\"b", "quote escape");
    RequireEqual(ParseJson("\"\\u3042\"").Value().AsString(), "\xe3\x81\x82",
        "a BMP code point becomes UTF-8");
    RequireEqual(ParseJson("\"\\ud83d\\ude00\"").Value().AsString(),
        "\xf0\x9f\x98\x80", "a surrogate pair becomes a 4 byte UTF-8 sequence");
    RequireEqual(ParseJson("\"\\u0041\"").Value().AsString(), "A", "an ASCII escape");
}

KACHA_V2_TEST(json, japanese_text_survives_unescaped)
{
    const std::string source = "{\"title\":\"ER2前頭部\"}";
    const auto parsed = ParseJson(source);
    Require(parsed.HasValue(), "japanese text parses");
    RequireEqual(parsed.Value().Find("title")->AsString(), "ER2前頭部",
        "the japanese text is preserved byte for byte");
    const std::string written = WriteJson(parsed.Value(), -1);
    Require(written.find("ER2前頭部") != std::string::npos,
        "japanese is written out unescaped, not as \\u sequences");
}

// ---- 書き出し ----

KACHA_V2_TEST(json, object_keys_are_written_in_a_fixed_order)
{
    // 挿入順が違っても、書き出しは同じ並びになること(決定的な保存のため)。
    JsonObject forward;
    forward["alpha"] = JsonValue::Number(1);
    forward["beta"] = JsonValue::Number(2);
    forward["gamma"] = JsonValue::Number(3);
    JsonObject backward;
    backward["gamma"] = JsonValue::Number(3);
    backward["beta"] = JsonValue::Number(2);
    backward["alpha"] = JsonValue::Number(1);
    RequireEqual(WriteJson(JsonValue::Object(forward), -1),
        WriteJson(JsonValue::Object(backward), -1),
        "the written order does not depend on insertion order");
}

KACHA_V2_TEST(json, numbers_round_trip_exactly)
{
    const std::vector<double> values = {0.0, 1.0, -1.0, 0.5, -0.5, 1.0e-9, 1.0e9,
        3.14159265358979323846, 1.0 / 3.0, 1234567890.123456, -0.000001, 2.5e-15,
        9007199254740992.0};
    for (const double value : values) {
        const std::string text = WriteJson(JsonValue::Number(value), -1);
        const auto parsed = ParseJson(text);
        Require(parsed.HasValue(), "the written number parses: " + text);
        RequireNear(parsed.Value().AsNumber(), value, 0.0,
            "the number survives the round trip exactly: " + text);
    }
}

KACHA_V2_TEST(json, control_characters_are_escaped_on_the_way_out)
{
    const std::string written = WriteJson(JsonValue::String("a\nb\tc"), -1);
    Require(written.find("\\n") != std::string::npos, "newline is escaped");
    Require(written.find("\\t") != std::string::npos, "tab is escaped");
    RequireEqual(ParseJson(written).Value().AsString(), "a\nb\tc",
        "and it comes back the same");
}

KACHA_V2_TEST(json, writing_is_stable_across_round_trips)
{
    JsonObject nested;
    nested["number"] = JsonValue::Number(1.5);
    nested["text"] = JsonValue::String("日本語 with \"quotes\"");
    nested["flag"] = JsonValue::Bool(true);
    nested["nothing"] = JsonValue::Null();
    nested["list"] = JsonValue::Array({JsonValue::Number(1), JsonValue::Number(2),
        JsonValue::Object({{"deep", JsonValue::String("value")}})});
    nested["empty_object"] = JsonValue::Object({});
    nested["empty_array"] = JsonValue::Array({});
    RequireStableRoundTrip(JsonValue::Object(nested), "a mixed document");
    RequireStableRoundTrip(JsonValue::Array({}), "an empty array");
    RequireStableRoundTrip(JsonValue::Object({}), "an empty object");
}

KACHA_V2_TEST(json, compact_and_indented_forms_carry_the_same_data)
{
    JsonObject object;
    object["a"] = JsonValue::Number(1);
    object["b"] = JsonValue::Array({JsonValue::Number(2), JsonValue::Number(3)});
    const JsonValue value = JsonValue::Object(object);
    const auto compact = ParseJson(WriteJson(value, -1));
    const auto indented = ParseJson(WriteJson(value, 2));
    Require(compact.HasValue() && indented.HasValue(), "both forms parse");
    RequireEqual(WriteJson(compact.Value(), -1), WriteJson(indented.Value(), -1),
        "indentation does not change the data");
}

// ---- 型つき読み出し ----

KACHA_V2_TEST(json, the_reader_reports_a_missing_key)
{
    const auto parsed = ParseJson("{\"a\":1}");
    JsonReader reader(parsed.Value());
    const double value = reader.RequireNumber("missing");
    RequireNear(value, 0.0, 0.0, "a missing number reads as zero");
    Require(reader.HasError(), "a missing key is an error");
    RequireEqual(reader.Diagnostics().front().code, "KCD2-J002",
        "the missing key code is used");
}

KACHA_V2_TEST(json, the_reader_reports_a_wrong_type)
{
    const auto parsed = ParseJson("{\"a\":\"text\",\"b\":1,\"c\":[1]}");
    JsonReader reader(parsed.Value());
    (void)reader.RequireNumber("a");
    Require(reader.HasError(), "a string where a number was expected is an error");
    RequireEqual(reader.Diagnostics().front().code, "KCD2-J003", "the wrong type code");

    JsonReader second(parsed.Value());
    (void)second.RequireString("b");
    Require(second.HasError(), "a number where a string was expected is an error");

    JsonReader third(parsed.Value());
    (void)third.RequireObject("c");
    Require(third.HasError(), "an array where an object was expected is an error");
}

KACHA_V2_TEST(json, the_reader_names_the_path_of_the_problem)
{
    const auto parsed = ParseJson("{\"outer\":{\"inner\":\"text\"}}");
    JsonReader reader(parsed.Value());
    JsonReader outer = reader.Child("outer");
    (void)outer.RequireNumber("inner");
    Require(reader.HasError(), "the error reaches the parent reader");
    Require(reader.Diagnostics().front().detailsJa.find("outer") != std::string::npos,
        "the message names the path so the user can find it");
}

KACHA_V2_TEST(json, optional_values_fall_back_without_an_error)
{
    const auto parsed = ParseJson("{\"a\":1}");
    JsonReader reader(parsed.Value());
    RequireNear(reader.OptionalNumber("missing", 42.0), 42.0, 0.0, "the fallback is used");
    RequireEqual(reader.OptionalString("missing", "既定"), "既定", "for strings too");
    Require(reader.OptionalBool("missing", true), "and for booleans");
    Require(!reader.HasError(), "an absent optional is not an error");
}

KACHA_V2_TEST(json, non_finite_numbers_can_never_enter_the_document)
{
    // パーサは NaN/Infinity のリテラルを拒否する。書き出し側でも作らない。
    RequireRejects("{\"a\":NaN}", "NaN in an object");
    RequireRejects("{\"a\":Infinity}", "Infinity in an object");
    RequireRejects("[1e400]", "an overflowing exponent");
}

KACHA_V2_TEST(json, a_large_document_parses_and_round_trips)
{
    // 実際の模型ぐらいの規模(ワイヤー1000本ぶん)を通す。
    JsonArray entities;
    for (int index = 0; index < 1000; ++index) {
        JsonObject entity;
        entity["id"] = JsonValue::String("20000000-0000-4000-8000-"
            + std::string(12 - std::to_string(index).size(), '0')
            + std::to_string(index));
        entity["kind"] = JsonValue::String("wire");
        entity["displayName"] = JsonValue::String("線" + std::to_string(index));
        entity["revision"] = JsonValue::Number(index);
        entity["position"] = JsonValue::Object({{"x", JsonValue::Number(index * 0.5)},
            {"y", JsonValue::Number(index * -0.25)}, {"z", JsonValue::Number(0.0)}});
        entities.push_back(JsonValue::Object(std::move(entity)));
    }
    JsonObject document;
    document["format"] = JsonValue::String("kachakachaCAD");
    document["schemaVersion"] = JsonValue::Number(2);
    document["entities"] = JsonValue::Array(std::move(entities));

    const JsonValue value = JsonValue::Object(std::move(document));
    const std::string text = WriteJson(value);
    Require(text.size() > 100000, "the document is genuinely large");
    const auto parsed = ParseJson(text);
    Require(parsed.HasValue(), "a large document parses");
    Require(parsed.Value().Find("entities")->AsArray().size() == 1000,
        "every entity survives");
    RequireEqual(WriteJson(parsed.Value()), text, "and it round-trips byte for byte");
}

KACHA_V2_TEST_MAIN("json_tests")
