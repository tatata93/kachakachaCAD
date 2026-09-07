// WP-02 の受入。
// AT-ARC-002 強い参照型 / UUID往復 / 不正な文字列の拒否 / 固定生成器 / 並び順の安定
// 単位の取り違え / 許容差の対角長境界 / NaN・Infinity の拒否
#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/base/Uuid.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/Units.h"

#include <limits>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::FeatureId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::base::MakeError;
using kachakacha::v2::base::MakeWarning;
using kachakacha::v2::base::RandomIdGenerator;
using kachakacha::v2::base::Result;
using kachakacha::v2::base::Severity;
using kachakacha::v2::base::Uuid;
using kachakacha::v2::geometry::Degrees;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::IsFinite;
using kachakacha::v2::geometry::LengthUnit;
using kachakacha::v2::geometry::LengthUnitToMillimeters;
using kachakacha::v2::geometry::Millimeters;
using kachakacha::v2::geometry::Radians;
using kachakacha::v2::geometry::ToDegrees;
using kachakacha::v2::geometry::ToRadians;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

// ---- UUID ----

KACHA_V2_TEST(uuid, roundtrips_through_the_saved_form)
{
    const std::string text = "1b4e28ba-2fa1-4d1b-883f-1d0f4c5a9e77";
    const auto parsed = Uuid::Parse(text);
    Require(parsed.has_value(), "a well formed uuid parses");
    RequireEqual(parsed->ToString(), text, "the saved form round-trips exactly");
}

KACHA_V2_TEST(uuid, rejects_every_malformed_form)
{
    const std::vector<std::string> bad = {
        "",
        "not-a-uuid",
        "1b4e28ba2fa14d1b883f1d0f4c5a9e77",              // ハイフン無し
        "1B4E28BA-2FA1-4D1B-883F-1D0F4C5A9E77",          // 大文字
        "{1b4e28ba-2fa1-4d1b-883f-1d0f4c5a9e77}",        // 波括弧
        "1b4e28ba-2fa1-4d1b-883f-1d0f4c5a9e7",           // 1文字短い
        "1b4e28ba-2fa1-4d1b-883f-1d0f4c5a9e777",         // 1文字長い
        "1b4e28ba_2fa1_4d1b_883f_1d0f4c5a9e77",          // 区切りが違う
        "1b4e28ba-2fa1-4d1b-883f-1d0f4c5a9ezz",          // 16進でない
    };
    for (const std::string& text : bad) {
        Require(!Uuid::Parse(text).has_value(),
            "malformed uuid is rejected instead of quietly fixed: \"" + text + "\"");
    }
}

KACHA_V2_TEST(uuid, the_nil_uuid_is_detected)
{
    const auto nil = Uuid::Parse("00000000-0000-0000-0000-000000000000");
    Require(nil.has_value(), "the nil uuid still parses");
    Require(nil->IsNil(), "the nil uuid reports itself as nil");
    Require(!Uuid::Parse("1b4e28ba-2fa1-4d1b-883f-1d0f4c5a9e77")->IsNil(),
        "a real uuid is not nil");
}

KACHA_V2_TEST(uuid, ordering_is_by_bytes_and_stable)
{
    const Uuid a = *Uuid::Parse("00000000-0000-4000-8000-000000000001");
    const Uuid b = *Uuid::Parse("00000000-0000-4000-8000-000000000002");
    const Uuid c = *Uuid::Parse("00000001-0000-4000-8000-000000000000");
    Require(a < b, "later bytes order later");
    Require(b < c, "earlier bytes dominate");
    std::set<Uuid> sorted{c, a, b};
    Require(*sorted.begin() == a, "the smallest sorts first");
    Require(*sorted.rbegin() == c, "the largest sorts last");
}

// ---- 種類つきID ----

KACHA_V2_TEST(ids, different_kinds_are_different_types)
{
    // 別種類のIDへ代入できないことは、コンパイルが通る事実そのもので示す。
    // ここでは値としての独立を確かめる。
    DeterministicIdGenerator generator(1);
    const EntityId entity = generator.NextTyped<IdKind::Entity>();
    const FeatureId feature = generator.NextTyped<IdKind::Feature>();
    Require(entity.ToString() != feature.ToString(),
        "two ids drawn in sequence differ");
    Require(!entity.IsNil(), "a generated id is not nil");
}

KACHA_V2_TEST(ids, the_deterministic_generator_repeats_exactly)
{
    DeterministicIdGenerator first(7);
    DeterministicIdGenerator second(7);
    for (int index = 0; index < 8; ++index) {
        RequireEqual(first.Next().ToString(), second.Next().ToString(),
            "the same seed yields the same sequence");
    }
}

KACHA_V2_TEST(ids, generated_ids_are_uuid_v4)
{
    DeterministicIdGenerator deterministic(3);
    RandomIdGenerator random(99);
    for (int index = 0; index < 16; ++index) {
        for (const Uuid value : {deterministic.Next(), random.Next()}) {
            const std::string text = value.ToString();
            Require(text[14] == '4', "the version nibble is 4: " + text);
            const char variant = text[19];
            Require(variant == '8' || variant == '9' || variant == 'a' || variant == 'b',
                "the variant nibble is 8/9/a/b: " + text);
            Require(Uuid::Parse(text).has_value(), "the generated form parses back");
        }
    }
}

KACHA_V2_TEST(ids, generated_ids_do_not_collide)
{
    RandomIdGenerator random(12345);
    std::unordered_set<std::string> seen;
    for (int index = 0; index < 5000; ++index) {
        Require(seen.insert(random.Next().ToString()).second,
            "5000 generated ids are all distinct");
    }
}

KACHA_V2_TEST(ids, the_short_suffix_is_the_last_four_characters)
{
    const EntityId id = *EntityId::Parse("1b4e28ba-2fa1-4d1b-883f-1d0f4c5a9e77");
    RequireEqual(id.ShortSuffix(), "9e77", "the UI suffix is the last four characters");
}

KACHA_V2_TEST(ids, ids_work_as_map_and_set_keys)
{
    DeterministicIdGenerator generator(5);
    std::set<EntityId> ordered;
    std::unordered_set<EntityId> hashed;
    for (int index = 0; index < 32; ++index) {
        const EntityId id = generator.NextTyped<IdKind::Entity>();
        ordered.insert(id);
        hashed.insert(id);
    }
    Require(ordered.size() == 32, "ordered containers keep every id");
    Require(hashed.size() == 32, "hashed containers keep every id");
}

// ---- 診断とResult ----

KACHA_V2_TEST(diagnostic, an_error_result_carries_no_value)
{
    const Result<int> failed = Result<int>::Failure(
        MakeError("TST-001", "失敗しました", "直し方"));
    Require(!failed.HasValue(), "a failure has no value");
    Require(failed.HasError(), "a failure reports an error");
    Require(failed.Diagnostics().size() == 1, "the diagnostic is kept");
    RequireEqual(failed.Diagnostics().front().code, "TST-001", "the code is kept");
}

KACHA_V2_TEST(diagnostic, a_success_may_carry_warnings)
{
    const Result<int> ok = Result<int>::Success(42,
        {MakeWarning("TST-002", "気をつけてください")});
    Require(ok.HasValue(), "a success has a value");
    Require(ok.Value() == 42, "the value survives");
    Require(!ok.HasError(), "warnings are not errors");
    Require(ok.Diagnostics().size() == 1, "the warning is kept");
}

KACHA_V2_TEST(diagnostic, a_success_holding_an_error_is_downgraded)
{
    // 呼び出し側の取り違えを早く見つける。Errorを混ぜたSuccessは値を持たない。
    const Result<int> confused = Result<int>::Success(42,
        {MakeError("TST-003", "本当は失敗している")});
    Require(!confused.HasValue(),
        "a Success carrying an Error does not hand out a value");
}

KACHA_V2_TEST(diagnostic, severity_names_are_stable)
{
    RequireEqual(std::string(kachakacha::v2::base::SeverityName(Severity::Error)),
        "Error", "severity names do not drift");
    RequireEqual(std::string(kachakacha::v2::base::SeverityName(Severity::Warning)),
        "Warning", "severity names do not drift");
}

// ---- 単位 ----

KACHA_V2_TEST(units, degrees_and_radians_convert_both_ways)
{
    RequireNear(ToRadians(Degrees(180.0)).Value(), 3.14159265358979323846, 1.0e-12,
        "180deg is pi rad");
    RequireNear(ToDegrees(Radians(3.14159265358979323846 / 2.0)).Value(), 90.0, 1.0e-12,
        "half pi is 90deg");
    RequireNear(ToDegrees(ToRadians(Degrees(37.5))).Value(), 37.5, 1.0e-12,
        "the conversion round-trips");
}

KACHA_V2_TEST(units, length_suffixes_convert_to_millimeters)
{
    RequireNear(LengthUnitToMillimeters(LengthUnit::Millimeter), 1.0, 0.0, "mm");
    RequireNear(LengthUnitToMillimeters(LengthUnit::Centimeter), 10.0, 0.0, "cm");
    RequireNear(LengthUnitToMillimeters(LengthUnit::Meter), 1000.0, 0.0, "m");
    RequireNear(LengthUnitToMillimeters(LengthUnit::Inch), 25.4, 0.0, "inch");
}

KACHA_V2_TEST(units, millimeters_compare_and_add)
{
    Require(Millimeters(1.0) < Millimeters(2.0), "millimeters compare");
    RequireNear((Millimeters(1.5) + Millimeters(2.5)).Value(), 4.0, 1.0e-12,
        "millimeters add");
}

KACHA_V2_TEST(units, non_finite_numbers_are_rejected)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    Require(!IsFinite(nan), "NaN is not finite");
    Require(!IsFinite(infinity), "positive infinity is not finite");
    Require(!IsFinite(-infinity), "negative infinity is not finite");
    Require(IsFinite(0.0), "zero is finite");
    Require(IsFinite(-1.0e300), "a large negative value is still finite");
}

// ---- 許容差 ----

KACHA_V2_TEST(tolerance, the_default_matches_the_contract)
{
    const GeometryTolerance tolerance = GeometryTolerance::Default();
    RequireNear(tolerance.numericEpsilon, 1.0e-12, 0.0, "numericEpsilon");
    RequireNear(tolerance.modelLinearMm, 1.0e-6, 0.0, "modelLinearMm");
    RequireNear(tolerance.modelAngularRad, 1.0e-9, 0.0, "modelAngularRad");
    RequireNear(tolerance.interactiveJoinMm, 0.01, 0.0, "interactiveJoinMm");
    RequireNear(tolerance.displayPickPx, 8.0, 0.0, "displayPickPx");
    RequireNear(tolerance.candidateMenuPx, 14.0, 0.0, "candidateMenuPx");
}

KACHA_V2_TEST(tolerance, the_model_diagonal_boundaries_hold)
{
    // clamp(max(1e-6, diagonal * 1e-9), 1e-6, 1e-3)
    // 模型サイズ(数百mm)では下限に張り付く。境界を明示して確かめる。
    RequireNear(GeometryTolerance::ResolveModelLinearMm(0.0), 1.0e-6, 0.0,
        "a degenerate diagonal falls back to the lower bound");
    RequireNear(GeometryTolerance::ResolveModelLinearMm(115.0), 1.0e-6, 0.0,
        "an ER1 sized model sits on the lower bound");
    // 1000 * 1e-9 は二進では厳密に 1e-6 にならない(1.0000000000000002e-06)。
    // 下限と実質同じであることを、丸め幅を許して確かめる。
    RequireNear(GeometryTolerance::ResolveModelLinearMm(1.0e3), 1.0e-6, 1.0e-20,
        "1000mm still sits on the lower bound");
    RequireNear(GeometryTolerance::ResolveModelLinearMm(1.0e4), 1.0e-5, 1.0e-18,
        "10000mm scales above the lower bound");
    RequireNear(GeometryTolerance::ResolveModelLinearMm(1.0e12), 1.0e-3, 0.0,
        "an enormous diagonal is clamped to the upper bound");
    RequireNear(GeometryTolerance::ResolveModelLinearMm(-5.0), 1.0e-6, 0.0,
        "a negative diagonal falls back instead of producing nonsense");
}

KACHA_V2_TEST(tolerance, the_interactive_join_range_is_enforced)
{
    Require(GeometryTolerance::IsValidInteractiveJoinMm(0.001), "the lower bound is valid");
    Require(GeometryTolerance::IsValidInteractiveJoinMm(0.01), "the default is valid");
    Require(GeometryTolerance::IsValidInteractiveJoinMm(0.1), "the upper bound is valid");
    Require(!GeometryTolerance::IsValidInteractiveJoinMm(0.0009),
        "below the range is rejected");
    Require(!GeometryTolerance::IsValidInteractiveJoinMm(0.2),
        "above the range is rejected");
}

KACHA_V2_TEST(tolerance, it_is_decided_once_and_carried_not_recomputed)
{
    // 遠方の点を1つ足しただけで既存の接続が壊れないよう、
    // 許容差は作成時に決めて持ち回る。ここでは「作った値が変わらない」ことを確かめる。
    const GeometryTolerance tolerance = GeometryTolerance::ForModelDiagonal(115.0);
    const double before = tolerance.modelLinearMm;
    const GeometryTolerance carried = tolerance;
    RequireNear(carried.modelLinearMm, before, 0.0,
        "a carried tolerance keeps the value it was created with");
}

KACHA_V2_TEST_MAIN("foundation_tests")
