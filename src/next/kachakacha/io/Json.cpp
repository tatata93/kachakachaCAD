#include "kachakacha/io/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace kachakacha::v2::io {

using base::Diagnostic;
using base::MakeError;
using base::Result;

namespace {

constexpr const char* kSyntax = "KCD2-J001";
constexpr const char* kMissingKey = "KCD2-J002";
constexpr const char* kWrongType = "KCD2-J003";
constexpr const char* kNotFinite = "KCD2-J004";

constexpr int kMaximumDepth = 64;

//! 1文字を JSON 文字列としてエスケープする。制御文字は \u 形式。
void AppendEscaped(std::string& out, std::string_view text)
{
    out.push_back('"');
    for (const char raw : text) {
        const auto character = static_cast<unsigned char>(raw);
        switch (raw) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (character < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", character);
                out += buffer;
            } else {
                out.push_back(raw);
            }
            break;
        }
    }
    out.push_back('"');
}

//! 数の書き方。往復して同じ値に戻ることを優先する。
void AppendNumber(std::string& out, double value)
{
    if (value == static_cast<double>(static_cast<long long>(value))
        && std::abs(value) < 1.0e15) {
        // 整数はそのまま。ただし JSON としては小数点を付けない。
        out += std::to_string(static_cast<long long>(value));
        return;
    }
    char buffer[64];
    // 17桁あれば double は往復する。
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    out += buffer;
}

void WriteValue(std::string& out, const JsonValue& value, int indent, int depth)
{
    const auto newline = [&out, indent](int level) {
        if (indent < 0) {
            return;
        }
        out.push_back('\n');
        out.append(static_cast<std::size_t>(indent * level), ' ');
    };
    switch (value.Type()) {
    case JsonType::Null:
        out += "null";
        return;
    case JsonType::Bool:
        out += value.AsBool() ? "true" : "false";
        return;
    case JsonType::Number:
        AppendNumber(out, value.AsNumber());
        return;
    case JsonType::String:
        AppendEscaped(out, value.AsString());
        return;
    case JsonType::Array: {
        const JsonArray& array = value.AsArray();
        if (array.empty()) {
            out += "[]";
            return;
        }
        out.push_back('[');
        for (std::size_t index = 0; index < array.size(); ++index) {
            if (index > 0) {
                out.push_back(',');
            }
            newline(depth + 1);
            WriteValue(out, array[index], indent, depth + 1);
        }
        newline(depth);
        out.push_back(']');
        return;
    }
    case JsonType::Object: {
        const JsonObject& object = value.AsObject();
        if (object.empty()) {
            out += "{}";
            return;
        }
        out.push_back('{');
        bool first = true;
        // std::map なのでキーは常に辞書順。書き出しが決定的になる。
        for (const auto& [key, child] : object) {
            if (!first) {
                out.push_back(',');
            }
            first = false;
            newline(depth + 1);
            AppendEscaped(out, key);
            out.push_back(':');
            if (indent >= 0) {
                out.push_back(' ');
            }
            WriteValue(out, child, indent, depth + 1);
        }
        newline(depth);
        out.push_back('}');
        return;
    }
    }
}

class Parser {
public:
    Parser(std::string_view text) : text_(text) {}

    [[nodiscard]] bool ParseValue(JsonValue& out, int depth);
    void SkipWhitespace();
    [[nodiscard]] bool AtEnd()
    {
        SkipWhitespace();
        return position_ >= text_.size();
    }
    [[nodiscard]] const std::string& Error() const noexcept { return error_; }
    [[nodiscard]] std::size_t Position() const noexcept { return position_; }

private:
    [[nodiscard]] bool Fail(std::string message)
    {
        if (error_.empty()) {
            error_ = std::move(message) + " (" + std::to_string(position_) + " 文字目)";
        }
        return false;
    }
    [[nodiscard]] bool ParseString(std::string& out);
    [[nodiscard]] bool ParseNumber(double& out);
    [[nodiscard]] bool Expect(char character);
    [[nodiscard]] bool ParseCodePoint(std::string& out);

    std::string_view text_;
    std::size_t position_ = 0;
    std::string error_;
};

void Parser::SkipWhitespace()
{
    while (position_ < text_.size()) {
        const char character = text_[position_];
        if (character == ' ' || character == '\t' || character == '\n'
            || character == '\r') {
            ++position_;
            continue;
        }
        break;
    }
}

bool Parser::Expect(char character)
{
    SkipWhitespace();
    if (position_ < text_.size() && text_[position_] == character) {
        ++position_;
        return true;
    }
    return Fail(std::string("'") + character + "' が必要です");
}

bool Parser::ParseCodePoint(std::string& out)
{
    if (position_ + 4 > text_.size()) {
        return Fail("\\u の後ろが足りません");
    }
    unsigned int value = 0;
    for (int index = 0; index < 4; ++index) {
        const char character = text_[position_ + static_cast<std::size_t>(index)];
        value <<= 4;
        if (character >= '0' && character <= '9') {
            value |= static_cast<unsigned int>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            value |= static_cast<unsigned int>(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
            value |= static_cast<unsigned int>(character - 'A' + 10);
        } else {
            return Fail("\\u の後ろが16進ではありません");
        }
    }
    position_ += 4;
    // サロゲートペア。
    if (value >= 0xD800 && value <= 0xDBFF) {
        if (position_ + 6 > text_.size() || text_[position_] != '\\'
            || text_[position_ + 1] != 'u') {
            return Fail("サロゲートペアの後半がありません");
        }
        position_ += 2;
        std::string tail;
        const std::size_t mark = position_;
        unsigned int low = 0;
        for (int index = 0; index < 4; ++index) {
            const char character = text_[mark + static_cast<std::size_t>(index)];
            low <<= 4;
            if (character >= '0' && character <= '9') {
                low |= static_cast<unsigned int>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                low |= static_cast<unsigned int>(character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                low |= static_cast<unsigned int>(character - 'A' + 10);
            } else {
                return Fail("サロゲートペアの後半が16進ではありません");
            }
        }
        position_ += 4;
        if (low < 0xDC00 || low > 0xDFFF) {
            return Fail("サロゲートペアの後半の範囲が違います");
        }
        value = 0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
    } else if (value >= 0xDC00 && value <= 0xDFFF) {
        return Fail("サロゲートペアの後半だけがあります");
    }
    // UTF-8 へ。
    if (value < 0x80) {
        out.push_back(static_cast<char>(value));
    } else if (value < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (value >> 6)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else if (value < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (value >> 12)));
        out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (value >> 18)));
        out.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    }
    return true;
}

bool Parser::ParseString(std::string& out)
{
    if (!Expect('"')) {
        return false;
    }
    out.clear();
    while (position_ < text_.size()) {
        const char character = text_[position_];
        if (character == '"') {
            ++position_;
            return true;
        }
        if (character == '\\') {
            ++position_;
            if (position_ >= text_.size()) {
                return Fail("エスケープの後ろがありません");
            }
            const char escaped = text_[position_++];
            switch (escaped) {
            case '"':  out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/'); break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u':
                if (!ParseCodePoint(out)) {
                    return false;
                }
                break;
            default:
                return Fail("使えないエスケープです");
            }
            continue;
        }
        if (static_cast<unsigned char>(character) < 0x20) {
            return Fail("文字列の中に生の制御文字があります");
        }
        out.push_back(character);
        ++position_;
    }
    return Fail("文字列が閉じていません");
}

bool Parser::ParseNumber(double& out)
{
    SkipWhitespace();
    const std::size_t start = position_;
    if (position_ < text_.size() && text_[position_] == '-') {
        ++position_;
    }
    // 先頭の 0 のあとに数字を続けない(JSONの決まり)。
    if (position_ >= text_.size()) {
        return Fail("数が途中で終わっています");
    }
    if (text_[position_] == '0') {
        ++position_;
    } else if (text_[position_] >= '1' && text_[position_] <= '9') {
        while (position_ < text_.size() && text_[position_] >= '0'
            && text_[position_] <= '9') {
            ++position_;
        }
    } else {
        return Fail("数として読めません");
    }
    if (position_ < text_.size() && text_[position_] == '.') {
        ++position_;
        const std::size_t fractionStart = position_;
        while (position_ < text_.size() && text_[position_] >= '0'
            && text_[position_] <= '9') {
            ++position_;
        }
        if (position_ == fractionStart) {
            return Fail("小数点の後ろに数字がありません");
        }
    }
    if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
        ++position_;
        if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) {
            ++position_;
        }
        const std::size_t exponentStart = position_;
        while (position_ < text_.size() && text_[position_] >= '0'
            && text_[position_] <= '9') {
            ++position_;
        }
        if (position_ == exponentStart) {
            return Fail("指数の後ろに数字がありません");
        }
    }
    const std::string number(text_.substr(start, position_ - start));
    char* end = nullptr;
    out = std::strtod(number.c_str(), &end);
    if (end == nullptr || *end != '\0') {
        return Fail("数として読めません");
    }
    if (!std::isfinite(out)) {
        return Fail("数が有限ではありません");
    }
    return true;
}

bool Parser::ParseValue(JsonValue& out, int depth)
{
    if (depth > kMaximumDepth) {
        return Fail("入れ子が深すぎます");
    }
    SkipWhitespace();
    if (position_ >= text_.size()) {
        return Fail("値がありません");
    }
    const char character = text_[position_];
    if (character == '{') {
        ++position_;
        JsonObject object;
        SkipWhitespace();
        if (position_ < text_.size() && text_[position_] == '}') {
            ++position_;
            out = JsonValue::Object(std::move(object));
            return true;
        }
        for (;;) {
            std::string key;
            if (!ParseString(key)) {
                return false;
            }
            if (!Expect(':')) {
                return false;
            }
            JsonValue child;
            if (!ParseValue(child, depth + 1)) {
                return false;
            }
            if (!object.emplace(std::move(key), std::move(child)).second) {
                return Fail("同じキーが2回あります");
            }
            SkipWhitespace();
            if (position_ < text_.size() && text_[position_] == ',') {
                ++position_;
                SkipWhitespace();
                // 末尾カンマは認めない。
                if (position_ < text_.size() && text_[position_] == '}') {
                    return Fail("末尾のカンマは使えません");
                }
                continue;
            }
            if (!Expect('}')) {
                return false;
            }
            break;
        }
        out = JsonValue::Object(std::move(object));
        return true;
    }
    if (character == '[') {
        ++position_;
        JsonArray array;
        SkipWhitespace();
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            out = JsonValue::Array(std::move(array));
            return true;
        }
        for (;;) {
            JsonValue child;
            if (!ParseValue(child, depth + 1)) {
                return false;
            }
            array.push_back(std::move(child));
            SkipWhitespace();
            if (position_ < text_.size() && text_[position_] == ',') {
                ++position_;
                SkipWhitespace();
                if (position_ < text_.size() && text_[position_] == ']') {
                    return Fail("末尾のカンマは使えません");
                }
                continue;
            }
            if (!Expect(']')) {
                return false;
            }
            break;
        }
        out = JsonValue::Array(std::move(array));
        return true;
    }
    if (character == '"') {
        std::string text;
        if (!ParseString(text)) {
            return false;
        }
        out = JsonValue::String(std::move(text));
        return true;
    }
    if (text_.compare(position_, 4, "true") == 0) {
        position_ += 4;
        out = JsonValue::Bool(true);
        return true;
    }
    if (text_.compare(position_, 5, "false") == 0) {
        position_ += 5;
        out = JsonValue::Bool(false);
        return true;
    }
    if (text_.compare(position_, 4, "null") == 0) {
        position_ += 4;
        out = JsonValue::Null();
        return true;
    }
    double number = 0.0;
    if (!ParseNumber(number)) {
        return false;
    }
    out = JsonValue::Number(number);
    return true;
}

} // namespace

// ---------------- JsonValue ----------------

JsonValue JsonValue::Null()
{
    return JsonValue();
}

JsonValue JsonValue::Bool(bool value)
{
    JsonValue json;
    json.type_ = JsonType::Bool;
    json.boolean_ = value;
    return json;
}

JsonValue JsonValue::Number(double value)
{
    JsonValue json;
    json.type_ = JsonType::Number;
    json.number_ = value;
    return json;
}

JsonValue JsonValue::String(std::string value)
{
    JsonValue json;
    json.type_ = JsonType::String;
    json.string_ = std::move(value);
    return json;
}

JsonValue JsonValue::Array(JsonArray value)
{
    JsonValue json;
    json.type_ = JsonType::Array;
    json.array_ = std::move(value);
    return json;
}

JsonValue JsonValue::Object(JsonObject value)
{
    JsonValue json;
    json.type_ = JsonType::Object;
    json.object_ = std::move(value);
    return json;
}

const JsonValue* JsonValue::Find(const std::string& key) const
{
    if (type_ != JsonType::Object) {
        return nullptr;
    }
    const auto found = object_.find(key);
    return found == object_.end() ? nullptr : &found->second;
}

// ---------------- 書き出し・読み込み ----------------

std::string WriteJson(const JsonValue& value, int indent)
{
    std::string out;
    WriteValue(out, value, indent, 0);
    if (indent >= 0) {
        out.push_back('\n');
    }
    return out;
}

Result<JsonValue> ParseJson(std::string_view text)
{
    // UTF-8 BOM は付いていない前提。付いていたら断る。
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF) {
        return Result<JsonValue>::Failure(MakeError(kSyntax,
            "ファイルの先頭にBOMがあります。", "UTF-8のBOM無しで保存してください。"));
    }
    Parser parser(text);
    JsonValue value;
    if (!parser.ParseValue(value, 0)) {
        return Result<JsonValue>::Failure(MakeError(kSyntax,
            "JSONとして読めません。", parser.Error()));
    }
    if (!parser.AtEnd()) {
        return Result<JsonValue>::Failure(MakeError(kSyntax,
            "JSONの後ろに余分な文字があります。",
            std::to_string(parser.Position()) + " 文字目"));
    }
    return Result<JsonValue>::Success(std::move(value));
}

// ---------------- JsonReader ----------------

JsonReader::JsonReader(const JsonValue& root, std::string path)
    : value_(&root)
    , path_(std::move(path))
    , diagnostics_(std::make_shared<std::vector<Diagnostic>>())
{
}

bool JsonReader::HasError() const
{
    for (const Diagnostic& diagnostic : *diagnostics_) {
        if (diagnostic.IsError()) {
            return true;
        }
    }
    return false;
}

void JsonReader::Fail(std::string code, std::string summaryJa, std::string detailsJa)
{
    diagnostics_->push_back(MakeError(std::move(code), std::move(summaryJa),
        detailsJa.empty() ? path_ : path_ + ": " + detailsJa));
}

JsonReader JsonReader::Child(const std::string& key)
{
    JsonReader child(*value_, path_ + "." + key);
    child.diagnostics_ = diagnostics_;
    const JsonValue* found = value_->Find(key);
    if (found != nullptr) {
        child.value_ = found;
    }
    return child;
}

JsonReader JsonReader::Index(std::size_t index, const JsonValue& element)
{
    JsonReader child(element, path_ + "[" + std::to_string(index) + "]");
    child.diagnostics_ = diagnostics_;
    return child;
}

bool JsonReader::Has(const std::string& key) const
{
    return value_->Find(key) != nullptr;
}

double JsonReader::RequireNumber(const std::string& key)
{
    const JsonValue* found = value_->Find(key);
    if (found == nullptr) {
        Fail(kMissingKey, "必要な項目がありません。", key);
        return 0.0;
    }
    if (found->Type() != JsonType::Number) {
        Fail(kWrongType, "数値であるべき項目が数値ではありません。", key);
        return 0.0;
    }
    if (!std::isfinite(found->AsNumber())) {
        Fail(kNotFinite, "数値が有限ではありません。", key);
        return 0.0;
    }
    return found->AsNumber();
}

double JsonReader::OptionalNumber(const std::string& key, double fallback)
{
    return value_->Find(key) == nullptr ? fallback : RequireNumber(key);
}

bool JsonReader::RequireBool(const std::string& key)
{
    const JsonValue* found = value_->Find(key);
    if (found == nullptr) {
        Fail(kMissingKey, "必要な項目がありません。", key);
        return false;
    }
    if (found->Type() != JsonType::Bool) {
        Fail(kWrongType, "true/false であるべき項目が違います。", key);
        return false;
    }
    return found->AsBool();
}

bool JsonReader::OptionalBool(const std::string& key, bool fallback)
{
    return value_->Find(key) == nullptr ? fallback : RequireBool(key);
}

std::string JsonReader::RequireString(const std::string& key)
{
    const JsonValue* found = value_->Find(key);
    if (found == nullptr) {
        Fail(kMissingKey, "必要な項目がありません。", key);
        return {};
    }
    if (found->Type() != JsonType::String) {
        Fail(kWrongType, "文字列であるべき項目が文字列ではありません。", key);
        return {};
    }
    return found->AsString();
}

std::string JsonReader::OptionalString(const std::string& key, const std::string& fallback)
{
    return value_->Find(key) == nullptr ? fallback : RequireString(key);
}

const JsonValue* JsonReader::RequireArray(const std::string& key)
{
    const JsonValue* found = value_->Find(key);
    if (found == nullptr) {
        Fail(kMissingKey, "必要な一覧がありません。", key);
        return nullptr;
    }
    if (!found->IsArray()) {
        Fail(kWrongType, "一覧であるべき項目が一覧ではありません。", key);
        return nullptr;
    }
    return found;
}

const JsonValue* JsonReader::RequireObject(const std::string& key)
{
    const JsonValue* found = value_->Find(key);
    if (found == nullptr) {
        Fail(kMissingKey, "必要な項目がありません。", key);
        return nullptr;
    }
    if (!found->IsObject()) {
        Fail(kWrongType, "まとまりであるべき項目が違います。", key);
        return nullptr;
    }
    return found;
}

} // namespace kachakacha::v2::io
