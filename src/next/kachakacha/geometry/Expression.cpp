#include "kachakacha/geometry/Expression.h"

#include <cmath>
#include <cstdlib>
#include <optional>
#include <vector>

namespace kachakacha::v2::geometry {

using base::MakeError;
using base::Result;

namespace {

constexpr const char* kSyntax = "MEA-E001";
constexpr const char* kDivideByZero = "MEA-E002";
constexpr const char* kUnitMismatch = "MEA-E003";
constexpr const char* kNotFinite = "MEA-E004";
constexpr const char* kTooDeep = "MEA-E005";

constexpr int kMaximumDepth = 32;

//! 解析の途中で持つ値。単位が付いているかどうかも一緒に運ぶ。
struct Quantity {
    double value = 0.0;              //!< 長さはmm、角度はrad、無単位はそのまま
    QuantityKind kind = QuantityKind::Scalar;
};

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    [[nodiscard]] std::optional<Quantity> ParseExpression(int depth);
    [[nodiscard]] const std::string& Error() const noexcept { return error_; }
    [[nodiscard]] const std::string& ErrorCode() const noexcept { return errorCode_; }
    [[nodiscard]] bool AtEnd()
    {
        SkipSpaces();
        return position_ >= text_.size();
    }

private:
    void SkipSpaces()
    {
        while (position_ < text_.size()
            && (text_[position_] == ' ' || text_[position_] == '\t')) {
            ++position_;
        }
    }

    [[nodiscard]] bool Consume(char character)
    {
        SkipSpaces();
        if (position_ < text_.size() && text_[position_] == character) {
            ++position_;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool ConsumeWord(std::string_view word)
    {
        SkipSpaces();
        if (text_.compare(position_, word.size(), word) == 0) {
            position_ += word.size();
            return true;
        }
        return false;
    }

    void Fail(const char* code, std::string message)
    {
        if (errorCode_.empty()) {
            errorCode_ = code;
            error_ = std::move(message);
        }
    }

    [[nodiscard]] std::optional<Quantity> ParseTerm(int depth);
    [[nodiscard]] std::optional<Quantity> ParsePower(int depth);
    [[nodiscard]] std::optional<Quantity> ParseUnary(int depth);
    [[nodiscard]] std::optional<Quantity> ParsePrimary(int depth);
    [[nodiscard]] std::optional<Quantity> ApplySuffix(Quantity value);

    std::string_view text_;
    std::size_t position_ = 0;
    std::string error_;
    std::string errorCode_;
};

//! 加減。単位は左右で一致させる(無単位は相手に合わせる)。
std::optional<Quantity> Parser::ParseExpression(int depth)
{
    if (depth > kMaximumDepth) {
        Fail(kTooDeep, "式の括弧が深すぎます。");
        return std::nullopt;
    }
    std::optional<Quantity> left = ParseTerm(depth);
    if (!left.has_value()) {
        return std::nullopt;
    }
    for (;;) {
        SkipSpaces();
        const bool plus = Consume('+');
        const bool minus = !plus && Consume('-');
        if (!plus && !minus) {
            return left;
        }
        const std::optional<Quantity> right = ParseTerm(depth);
        if (!right.has_value()) {
            return std::nullopt;
        }
        if (left->kind != QuantityKind::Scalar && right->kind != QuantityKind::Scalar
            && left->kind != right->kind) {
            Fail(kUnitMismatch, "長さと角度は足し引きできません。");
            return std::nullopt;
        }
        Quantity result;
        result.value = plus ? left->value + right->value : left->value - right->value;
        result.kind = left->kind != QuantityKind::Scalar ? left->kind : right->kind;
        left = result;
    }
}

//! 乗除。単位付き同士の掛け算は受け付けない(mm*mm を長さとして扱わない)。
std::optional<Quantity> Parser::ParseTerm(int depth)
{
    std::optional<Quantity> left = ParsePower(depth);
    if (!left.has_value()) {
        return std::nullopt;
    }
    for (;;) {
        SkipSpaces();
        const bool times = Consume('*');
        const bool divide = !times && Consume('/');
        if (!times && !divide) {
            return left;
        }
        const std::optional<Quantity> right = ParsePower(depth);
        if (!right.has_value()) {
            return std::nullopt;
        }
        if (divide && right->value == 0.0) {
            Fail(kDivideByZero, "0で割ることはできません。");
            return std::nullopt;
        }
        if (left->kind != QuantityKind::Scalar && right->kind != QuantityKind::Scalar) {
            Fail(kUnitMismatch, "単位のついた値どうしは掛け割りできません。");
            return std::nullopt;
        }
        Quantity result;
        result.value = times ? left->value * right->value : left->value / right->value;
        result.kind = left->kind != QuantityKind::Scalar ? left->kind : right->kind;
        left = result;
    }
}

//! べき乗。右結合。指数は無単位のみ。
std::optional<Quantity> Parser::ParsePower(int depth)
{
    std::optional<Quantity> base = ParseUnary(depth);
    if (!base.has_value()) {
        return std::nullopt;
    }
    SkipSpaces();
    if (!Consume('^')) {
        return base;
    }
    const std::optional<Quantity> exponent = ParsePower(depth);
    if (!exponent.has_value()) {
        return std::nullopt;
    }
    if (exponent->kind != QuantityKind::Scalar) {
        Fail(kUnitMismatch, "指数に単位は付けられません。");
        return std::nullopt;
    }
    Quantity result;
    result.value = std::pow(base->value, exponent->value);
    result.kind = base->kind;
    if (!IsFinite(result.value)) {
        Fail(kNotFinite, "計算結果が数値になりません。");
        return std::nullopt;
    }
    return result;
}

std::optional<Quantity> Parser::ParseUnary(int depth)
{
    SkipSpaces();
    if (Consume('-')) {
        std::optional<Quantity> inner = ParseUnary(depth);
        if (!inner.has_value()) {
            return std::nullopt;
        }
        inner->value = -inner->value;
        return inner;
    }
    if (Consume('+')) {
        return ParseUnary(depth);
    }
    return ParsePrimary(depth);
}

//! 数、pi、括弧、deg()、rad()。
std::optional<Quantity> Parser::ParsePrimary(int depth)
{
    SkipSpaces();
    if (Consume('(')) {
        const std::optional<Quantity> inner = ParseExpression(depth + 1);
        if (!inner.has_value()) {
            return std::nullopt;
        }
        if (!Consume(')')) {
            Fail(kSyntax, "閉じ括弧がありません。");
            return std::nullopt;
        }
        return ApplySuffix(*inner);
    }
    if (ConsumeWord("deg(")) {
        const std::optional<Quantity> inner = ParseExpression(depth + 1);
        if (!inner.has_value()) {
            return std::nullopt;
        }
        if (!Consume(')')) {
            Fail(kSyntax, "deg( の閉じ括弧がありません。");
            return std::nullopt;
        }
        if (inner->kind != QuantityKind::Scalar) {
            Fail(kUnitMismatch, "deg() には単位のない数を入れてください。");
            return std::nullopt;
        }
        return Quantity{ToRadians(Degrees(inner->value)).Value(), QuantityKind::Angle};
    }
    if (ConsumeWord("rad(")) {
        const std::optional<Quantity> inner = ParseExpression(depth + 1);
        if (!inner.has_value()) {
            return std::nullopt;
        }
        if (!Consume(')')) {
            Fail(kSyntax, "rad( の閉じ括弧がありません。");
            return std::nullopt;
        }
        if (inner->kind != QuantityKind::Scalar) {
            Fail(kUnitMismatch, "rad() には単位のない数を入れてください。");
            return std::nullopt;
        }
        return Quantity{inner->value, QuantityKind::Angle};
    }
    if (ConsumeWord("pi")) {
        return ApplySuffix(Quantity{kPi, QuantityKind::Scalar});
    }

    // 数。保存も入力も小数点は '.' だけ。
    SkipSpaces();
    const std::size_t start = position_;
    while (position_ < text_.size()
        && ((text_[position_] >= '0' && text_[position_] <= '9')
            || text_[position_] == '.')) {
        ++position_;
    }
    if (position_ == start) {
        Fail(kSyntax, "数式として読めません。");
        return std::nullopt;
    }
    const std::string number(text_.substr(start, position_ - start));
    char* end = nullptr;
    const double parsed = std::strtod(number.c_str(), &end);
    if (end == nullptr || *end != '\0' || !IsFinite(parsed)) {
        Fail(kSyntax, "数の書き方が正しくありません: " + number);
        return std::nullopt;
    }
    return ApplySuffix(Quantity{parsed, QuantityKind::Scalar});
}

//! 単位suffix。長い綴りから先に見る(mm を m と読み違えないため)。
std::optional<Quantity> Parser::ApplySuffix(Quantity value)
{
    const std::size_t mark = position_;
    if (ConsumeWord("mm")) {
        value.kind = QuantityKind::Length;
        return value;
    }
    if (ConsumeWord("cm")) {
        value.value *= 10.0;
        value.kind = QuantityKind::Length;
        return value;
    }
    if (ConsumeWord("deg")) {
        value.value = ToRadians(Degrees(value.value)).Value();
        value.kind = QuantityKind::Angle;
        return value;
    }
    if (ConsumeWord("rad")) {
        value.kind = QuantityKind::Angle;
        return value;
    }
    if (ConsumeWord("m")) {
        // "m" は "mm"/"cm" を先に見た後なので安全。ただし直後が英字なら単位ではない。
        if (position_ < text_.size()
            && ((text_[position_] >= 'a' && text_[position_] <= 'z')
                || (text_[position_] >= 'A' && text_[position_] <= 'Z'))) {
            position_ = mark;
            return value;
        }
        value.value *= 1000.0;
        value.kind = QuantityKind::Length;
        return value;
    }
    return value;
}

} // namespace

std::string NormalizeFullWidth(std::string_view text)
{
    // 全角ASCII(U+FF01〜U+FF5E)はまとめて半角へ直す。数字も英字も演算子も一度に片づく。
    // そのほか、数式で使われがちな記号だけを個別に拾う。
    static const struct {
        const char* wide;
        const char* narrow;
    } kSpecials[] = {
        {"\xe3\x80\x80", " "},  // 全角空白
        {"\xc3\x97", "*"},  // ×
        {"\xc3\xb7", "/"},  // ÷
        {"\xe3\x83\xbc", "-"},  // 長音符(マイナスの打ち間違い)
        {"\xe2\x88\x92", "-"},  // 全角マイナス記号
        {"\xe3\x80\x82", "."},  // 句点(小数点の打ち間違い)
    };

    const auto decodeThreeByte = [](std::string_view view, std::size_t index) -> int {
        if (index + 2 >= view.size()) {
            return -1;
        }
        const auto b0 = static_cast<unsigned char>(view[index]);
        const auto b1 = static_cast<unsigned char>(view[index + 1]);
        const auto b2 = static_cast<unsigned char>(view[index + 2]);
        if ((b0 & 0xF0) != 0xE0 || (b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) {
            return -1;
        }
        return ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
    };

    std::string result;
    result.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size()) {
        bool replaced = false;
        for (const auto& entry : kSpecials) {
            const std::size_t length = std::string_view(entry.wide).size();
            if (text.compare(index, length, entry.wide) == 0) {
                result += entry.narrow;
                index += length;
                replaced = true;
                break;
            }
        }
        if (replaced) {
            continue;
        }
        const int codePoint = decodeThreeByte(text, index);
        if (codePoint >= 0xFF01 && codePoint <= 0xFF5E) {
            result.push_back(static_cast<char>(codePoint - 0xFEE0));
            index += 3;
            continue;
        }
        result.push_back(text[index]);
        ++index;
    }
    return result;
}

Result<EvaluatedValue> EvaluateExpression(std::string_view text, QuantityKind expected)
{
    const std::string original(text);
    const std::string normalized = NormalizeFullWidth(text);
    if (normalized.find_first_not_of(" \t") == std::string::npos) {
        return Result<EvaluatedValue>::Failure(MakeError(kSyntax,
            "値が入力されていません。", "数か式を入れてください。"));
    }

    Parser parser(normalized);
    const std::optional<Quantity> parsed = parser.ParseExpression(0);
    if (!parsed.has_value()) {
        return Result<EvaluatedValue>::Failure(MakeError(
            parser.ErrorCode().empty() ? kSyntax : parser.ErrorCode().c_str(),
            parser.Error().empty() ? std::string("数式として読めません。") : parser.Error(),
            "入力: " + original));
    }
    if (!parser.AtEnd()) {
        return Result<EvaluatedValue>::Failure(MakeError(kSyntax,
            "式の後ろに読めない文字が残っています。", "入力: " + original));
    }
    if (!IsFinite(parsed->value)) {
        return Result<EvaluatedValue>::Failure(MakeError(kNotFinite,
            "計算結果が数値になりません。", "入力: " + original));
    }

    // 欄の種類と合っているか。長さ欄に角度、角度欄に長さは拒否する。
    if (parsed->kind != QuantityKind::Scalar && parsed->kind != expected) {
        return Result<EvaluatedValue>::Failure(MakeError(kUnitMismatch,
            expected == QuantityKind::Length ? "長さの欄に角度が入っています。"
                                             : "角度の欄に長さが入っています。",
            "入力: " + original));
    }
    if (expected == QuantityKind::Scalar && parsed->kind != QuantityKind::Scalar) {
        return Result<EvaluatedValue>::Failure(MakeError(kUnitMismatch,
            "この欄に単位は付けられません。", "入力: " + original));
    }

    EvaluatedValue evaluated;
    evaluated.expression = original;
    evaluated.value = parsed->value;
    evaluated.kind = expected;
    return Result<EvaluatedValue>::Success(std::move(evaluated));
}

} // namespace kachakacha::v2::geometry
