// WP-04 の受入(数値入力)。geometry-contract §11。
// 現行版のパーサは + - * / と単項と括弧と pi だけ。ここで足したものを1つずつ確かめる。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/Expression.h"

#include <cmath>
#include <string>

using kachakacha::v2::geometry::EvaluateExpression;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::geometry::NormalizeFullWidth;
using kachakacha::v2::geometry::QuantityKind;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] double Length(const std::string& text)
{
    const auto result = EvaluateExpression(text, QuantityKind::Length);
    Require(result.HasValue(), "the length expression is accepted: " + text);
    return result.Value().value;
}

[[nodiscard]] double Angle(const std::string& text)
{
    const auto result = EvaluateExpression(text, QuantityKind::Angle);
    Require(result.HasValue(), "the angle expression is accepted: " + text);
    return result.Value().value;
}

void RequireRefused(const std::string& text, QuantityKind kind, const std::string& code)
{
    const auto result = EvaluateExpression(text, kind);
    Require(!result.HasValue(), "refused: " + text);
    Require(!result.Diagnostics().empty(), "the refusal carries a diagnostic: " + text);
    RequireEqual(result.Diagnostics().front().code, code,
        "the refusal uses the expected code for: " + text);
}

} // namespace

KACHA_V2_TEST(expression, the_four_operations_and_parentheses_work)
{
    RequireNear(Length("12"), 12.0, 1.0e-12, "a plain number");
    RequireNear(Length("1+2*3"), 7.0, 1.0e-12, "multiplication binds tighter");
    RequireNear(Length("(1+2)*3"), 9.0, 1.0e-12, "parentheses override precedence");
    RequireNear(Length("(180/2)*3"), 270.0, 1.0e-12, "the example from the contract");
    RequireNear(Length("10-3-2"), 5.0, 1.0e-12, "subtraction is left associative");
    RequireNear(Length("100/4/5"), 5.0, 1.0e-12, "division is left associative");
}

KACHA_V2_TEST(expression, unary_signs_work)
{
    RequireNear(Length("-5"), -5.0, 1.0e-12, "a leading minus");
    RequireNear(Length("+5"), 5.0, 1.0e-12, "a leading plus");
    RequireNear(Length("3*-2"), -6.0, 1.0e-12, "a unary minus after an operator");
    RequireNear(Length("--4"), 4.0, 1.0e-12, "a double negative");
}

KACHA_V2_TEST(expression, powers_are_right_associative)
{
    RequireNear(Length("2^3"), 8.0, 1.0e-12, "a simple power");
    RequireNear(Length("2^3^2"), 512.0, 1.0e-9, "powers associate to the right");
    RequireNear(Length("2*3^2"), 18.0, 1.0e-12, "powers bind tighter than multiplication");
    RequireNear(Length("(2*3)^2"), 36.0, 1.0e-12, "parentheses still win");
}

KACHA_V2_TEST(expression, pi_is_available)
{
    RequireNear(EvaluateExpression("pi", QuantityKind::Scalar).Value().value, kPi, 1.0e-12,
        "pi is the constant");
    RequireNear(EvaluateExpression("2*pi", QuantityKind::Scalar).Value().value, 2.0 * kPi,
        1.0e-12, "pi takes part in arithmetic");
}

KACHA_V2_TEST(expression, deg_and_rad_functions_produce_angles)
{
    RequireNear(Angle("deg(180)"), kPi, 1.0e-12, "deg(180) is pi radians");
    RequireNear(Angle("deg(90)"), kPi / 2.0, 1.0e-12, "deg(90) is a right angle");
    RequireNear(Angle("rad(1.5)"), 1.5, 1.0e-12, "rad() passes radians through");
    RequireNear(Angle("deg((45/3)*2)"), kPi * 30.0 / 180.0, 1.0e-12,
        "an expression inside deg()");
}

KACHA_V2_TEST(expression, length_suffixes_convert_to_millimetres)
{
    RequireNear(Length("5mm"), 5.0, 1.0e-12, "mm stays as is");
    RequireNear(Length("5cm"), 50.0, 1.0e-12, "cm becomes millimetres");
    RequireNear(Length("2m"), 2000.0, 1.0e-12, "m becomes millimetres");
    RequireNear(Length("1cm+5mm"), 15.0, 1.0e-12, "mixed units add correctly");
    RequireNear(Length("2*3cm"), 60.0, 1.0e-12, "a scalar multiplies a length");
}

KACHA_V2_TEST(expression, angle_suffixes_convert_to_radians)
{
    RequireNear(Angle("180deg"), kPi, 1.0e-12, "deg suffix becomes radians");
    RequireNear(Angle("1.5rad"), 1.5, 1.0e-12, "rad suffix stays radians");
    RequireNear(Angle("30deg+30deg"), kPi / 3.0, 1.0e-12, "angles add");
}

KACHA_V2_TEST(expression, the_wrong_unit_for_the_field_is_refused)
{
    RequireRefused("30deg", QuantityKind::Length, "MEA-E003");
    RequireRefused("5mm", QuantityKind::Angle, "MEA-E003");
    RequireRefused("deg(90)", QuantityKind::Length, "MEA-E003");
    RequireRefused("5mm", QuantityKind::Scalar, "MEA-E003");
    RequireRefused("1mm+2deg", QuantityKind::Length, "MEA-E003");
    RequireRefused("2mm*3mm", QuantityKind::Length, "MEA-E003");
    RequireRefused("2^(3mm)", QuantityKind::Length, "MEA-E003");
}

KACHA_V2_TEST(expression, division_by_zero_does_not_commit)
{
    RequireRefused("1/0", QuantityKind::Length, "MEA-E002");
    RequireRefused("5/(3-3)", QuantityKind::Length, "MEA-E002");
    RequireRefused("10/0mm", QuantityKind::Length, "MEA-E002");
}

KACHA_V2_TEST(expression, syntax_errors_do_not_commit)
{
    RequireRefused("", QuantityKind::Length, "MEA-E001");
    RequireRefused("   ", QuantityKind::Length, "MEA-E001");
    RequireRefused("1+", QuantityKind::Length, "MEA-E001");
    RequireRefused("(1+2", QuantityKind::Length, "MEA-E001");
    RequireRefused("1+2)", QuantityKind::Length, "MEA-E001");
    RequireRefused("abc", QuantityKind::Length, "MEA-E001");
    RequireRefused("1 2", QuantityKind::Length, "MEA-E001");
    RequireRefused("deg(90", QuantityKind::Angle, "MEA-E001");
}

KACHA_V2_TEST(expression, full_width_input_is_normalized)
{
    RequireEqual(NormalizeFullWidth("１２３"), "123", "full width digits");
    RequireEqual(NormalizeFullWidth("（１＋２）＊３"), "(1+2)*3", "full width operators");
    RequireEqual(NormalizeFullWidth("１．５"), "1.5", "a full width decimal point");
    RequireNear(Length("（１８０／２）＊３"), 270.0, 1.0e-12,
        "a fully full-width expression evaluates");
    RequireNear(Length("１０ｍｍ"), 10.0, 1.0e-12,
        "full width digits with a half width unit");
}

KACHA_V2_TEST(expression, the_expression_is_kept_for_re_editing)
{
    const auto result = EvaluateExpression("(180/2)*3", QuantityKind::Length);
    Require(result.HasValue(), "the expression evaluates");
    RequireEqual(result.Value().expression, "(180/2)*3",
        "the original text is kept so the user can edit it again");
    RequireNear(result.Value().value, 270.0, 1.0e-12, "the evaluated value is kept too");
    Require(result.Value().kind == QuantityKind::Length, "the field kind is recorded");
}

KACHA_V2_TEST(expression, the_original_text_survives_normalization)
{
    const auto result = EvaluateExpression("（１８０／２）", QuantityKind::Length);
    Require(result.HasValue(), "the full width expression evaluates");
    RequireEqual(result.Value().expression, "（１８０／２）",
        "the user's own text is kept, not the normalized form");
    RequireNear(result.Value().value, 90.0, 1.0e-12, "the value is computed correctly");
}

KACHA_V2_TEST(expression, deeply_nested_parentheses_are_refused_not_crashed)
{
    std::string text;
    for (int index = 0; index < 64; ++index) {
        text += '(';
    }
    text += "1";
    for (int index = 0; index < 64; ++index) {
        text += ')';
    }
    const auto result = EvaluateExpression(text, QuantityKind::Length);
    Require(!result.HasValue(), "64 levels of nesting is refused instead of overflowing");
    RequireEqual(result.Diagnostics().front().code, "MEA-E005",
        "the refusal names the depth limit");
}

KACHA_V2_TEST(expression, results_that_are_not_finite_are_refused)
{
    RequireRefused("10^400", QuantityKind::Length, "MEA-E004");
}

KACHA_V2_TEST(expression, 角度の欄の単位なしの数は度として読む)
{
    // オーナー報告 2026-09-21:「長さと角度を指定しても線が引けない」。
    // 90 と打つと 90 ラジアン(約 5156 度)になっていた。欄の表示は度なので、
    // 打つほうも度で読む。rad と書いたときだけラジアン。
    const auto plain = EvaluateExpression("90", QuantityKind::Angle);
    Require(plain.HasValue(), "角度の欄に 90 と打てる");
    RequireNear(plain.Value().value, kPi / 2.0, 1e-12, "90 は直角(ラジアンで π/2)");
    const auto degrees = EvaluateExpression("90deg", QuantityKind::Angle);
    Require(degrees.HasValue() && std::abs(degrees.Value().value - kPi / 2.0) < 1e-12,
        "deg と書いても同じ");
    const auto radians = EvaluateExpression("1.5708rad", QuantityKind::Angle);
    Require(radians.HasValue() && std::abs(radians.Value().value - 1.5708) < 1e-12,
        "rad と書けばラジアンのまま");
    // 長さの欄は触らない。
    const auto length = EvaluateExpression("90", QuantityKind::Length);
    Require(length.HasValue() && std::abs(length.Value().value - 90.0) < 1e-12,
        "長さの 90 は 90mm のまま");
}

KACHA_V2_TEST_MAIN("expression_tests")
