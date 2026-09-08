#pragma once

//! .kcd2 のための JSON。第三者ライブラリを増やさず、必要な範囲を厳密に実装する
//! (ADR 0027: ビルドできる唯一のPCを止めないため、依存を増やさない)。
//!
//! 厳密であることを優先する。コメント無し、末尾カンマ無し、UTF-8 BOM 無し、
//! 数は有限のみ(NaN と Infinity は書かないし読まない)。
//! 読めない入力は位置つきの診断で断り、黙って直さない。

#include "kachakacha/base/Diagnostic.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kachakacha::v2::io {

class JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

enum class JsonType { Null, Bool, Number, String, Array, Object };

class JsonValue {
public:
    JsonValue() = default;
    static JsonValue Null();
    static JsonValue Bool(bool value);
    static JsonValue Number(double value);
    static JsonValue String(std::string value);
    static JsonValue Array(JsonArray value);
    static JsonValue Object(JsonObject value);

    [[nodiscard]] JsonType Type() const noexcept { return type_; }
    [[nodiscard]] bool IsNull() const noexcept { return type_ == JsonType::Null; }
    [[nodiscard]] bool IsObject() const noexcept { return type_ == JsonType::Object; }
    [[nodiscard]] bool IsArray() const noexcept { return type_ == JsonType::Array; }

    [[nodiscard]] bool AsBool() const noexcept { return boolean_; }
    [[nodiscard]] double AsNumber() const noexcept { return number_; }
    [[nodiscard]] const std::string& AsString() const noexcept { return string_; }
    [[nodiscard]] const JsonArray& AsArray() const noexcept { return array_; }
    [[nodiscard]] const JsonObject& AsObject() const noexcept { return object_; }
    [[nodiscard]] JsonObject& MutableObject() noexcept { return object_; }
    [[nodiscard]] JsonArray& MutableArray() noexcept { return array_; }

    //! object のキー引き。無ければ nullptr。
    [[nodiscard]] const JsonValue* Find(const std::string& key) const;

private:
    JsonType type_ = JsonType::Null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string string_;
    JsonArray array_;
    JsonObject object_;
};

//! 書き出し。キー順は挿入順ではなく辞書順で固定する(決定的にするため)。
//! indent < 0 なら1行にまとめる。
[[nodiscard]] std::string WriteJson(const JsonValue& value, int indent = 2);

//! 読み込み。失敗したら位置つきの診断を返す。
[[nodiscard]] base::Result<JsonValue> ParseJson(std::string_view text);

// ---- 型つきの取り出し。欠損・型違いを診断にする ----

class JsonReader {
public:
    JsonReader(const JsonValue& root, std::string path = "$");

    [[nodiscard]] const std::vector<base::Diagnostic>& Diagnostics() const noexcept
    {
        return *diagnostics_;
    }
    [[nodiscard]] bool HasError() const;

    [[nodiscard]] JsonReader Child(const std::string& key);
    [[nodiscard]] JsonReader Index(std::size_t index, const JsonValue& element);

    [[nodiscard]] bool Has(const std::string& key) const;
    [[nodiscard]] const JsonValue& Value() const noexcept { return *value_; }

    [[nodiscard]] double RequireNumber(const std::string& key);
    [[nodiscard]] double OptionalNumber(const std::string& key, double fallback);
    [[nodiscard]] bool RequireBool(const std::string& key);
    [[nodiscard]] bool OptionalBool(const std::string& key, bool fallback);
    [[nodiscard]] std::string RequireString(const std::string& key);
    [[nodiscard]] std::string OptionalString(const std::string& key,
        const std::string& fallback);
    [[nodiscard]] const JsonValue* RequireArray(const std::string& key);
    [[nodiscard]] const JsonValue* RequireObject(const std::string& key);

    void Fail(std::string code, std::string summaryJa, std::string detailsJa = {});

private:
    const JsonValue* value_ = nullptr;
    std::string path_;
    std::shared_ptr<std::vector<base::Diagnostic>> diagnostics_;
};

} // namespace kachakacha::v2::io
