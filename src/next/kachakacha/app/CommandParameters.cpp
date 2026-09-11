#include "kachakacha/app/CommandParameters.h"

#include <cstdio>

namespace kachakacha::v2::app {
namespace {

using base::MakeError;

[[nodiscard]] std::string Fixed(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return std::string(buffer);
}

} // namespace

const std::vector<ParameterDefinition>& ParameterDefinitions()
{
    static const std::vector<ParameterDefinition> table = {
        {ParameterId::ExtrudeDistance, "extrude_distance", "板厚(押し出しの距離)",
            geometry::QuantityKind::Length, 0.5, 0.05, 20.0,
            "プラ板は 0.1〜1.0mm あたりを使います。0 では切れず、20mm を超えると板ではありません。"},
        {ParameterId::CornerSize, "corner_size", "面取り量 / 丸め半径",
            geometry::QuantityKind::Length, 1.0, 0.01, 100.0,
            "0 では角が落ちません。線より大きい量は落としようがありません。"},
        {ParameterId::OffsetDistanceMm, "offset_distance", "オフセット距離",
            geometry::QuantityKind::Length, 1.0, -1000.0, 1000.0,
            "線を平面の中でこの距離だけ平行に写します。負なら反対側です。0 では動きません。"},
        {ParameterId::PatternMarginMm, "pattern_margin", "型紙の余白",
            geometry::QuantityKind::Length, 5.0, 0.0, 50.0,
            "余白が広すぎると、1枚に入る部材が減ります。"},
        {ParameterId::ScaleDenominator, "scale_denominator", "縮尺の分母(1/◯)",
            geometry::QuantityKind::Scalar, 87.0, 1.0, 1000.0,
            "1/1 から 1/1000 まで。HO は 87、N は 150 です。"},
        {ParameterId::MaxDeviationMm, "max_deviation", "展開で許すずれ(mm)",
            geometry::QuantityKind::Length, 0.1, 0.001, 5.0,
            "曲がった面を平らにするときに、どこまでのずれなら許すか。ここが通るか通らないかを決めます。"},
        {ParameterId::RealSizeMm, "real_size", "実寸(mm)",
            geometry::QuantityKind::Length, 20000.0, 0.0, 1000000.0,
            "実物の寸法です。縮尺で割った値が下に出ます。"},
        {ParameterId::RevolveAngleDeg, "revolve_angle", "回転体の角度(度)",
            geometry::QuantityKind::Scalar, 360.0, 0.1, 360.0,
            "断面を回す角度です。360 で一周。0 では回りません。"},
        {ParameterId::RevolveSections, "revolve_sections", "回転体の断面の数",
            geometry::QuantityKind::Scalar, 12.0, 2.0, 72.0,
            "回した写しの数です。多いほど丸く、面が重くなります。2〜72。"},
    };
    return table;
}

const ParameterDefinition* FindParameter(ParameterId id) noexcept
{
    for (const ParameterDefinition& definition : ParameterDefinitions()) {
        if (definition.id == id) {
            return &definition;
        }
    }
    return nullptr;
}

ParameterSet DefaultParameters()
{
    ParameterSet set;
    for (const ParameterDefinition& definition : ParameterDefinitions()) {
        ParameterValue value;
        value.value = definition.defaultValue;
        value.expression = Fixed(definition.defaultValue);
        set.values.push_back(std::move(value));
    }
    return set;
}

double ParameterValueOf(const ParameterSet& set, ParameterId id) noexcept
{
    const auto& definitions = ParameterDefinitions();
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        if (definitions[index].id != id) {
            continue;
        }
        if (index < set.values.size()) {
            return set.values[index].value;
        }
        return definitions[index].defaultValue;
    }
    return 0.0;
}

std::string ParameterTextOf(const ParameterSet& set, ParameterId id)
{
    const auto& definitions = ParameterDefinitions();
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        if (definitions[index].id != id) {
            continue;
        }
        if (index < set.values.size()) {
            return set.values[index].expression;
        }
        return Fixed(definitions[index].defaultValue);
    }
    return std::string();
}

base::Result<ParameterSet> SetParameter(const ParameterSet& set, ParameterId id,
    std::string_view text)
{
    using Out = base::Result<ParameterSet>;
    const ParameterDefinition* definition = FindParameter(id);
    if (definition == nullptr) {
        return Out::Failure(MakeError("UI-P001", "その数はありません。", {}));
    }
    // 式のまま受ける。電卓を出して打ち直すと、打ち間違いが混ざる。
    const auto evaluated = geometry::EvaluateExpression(text, definition->kind);
    if (!evaluated.HasValue()) {
        return Out::Failure(evaluated.Diagnostics());
    }
    const double value = evaluated.Value().value;
    if (value < definition->minimum || value > definition->maximum) {
        // 黙って近い値へ寄せない。寄せると、頼んだ値と違う物が出来る。
        return Out::Failure(MakeError("UI-P002", "その値は範囲の外です。",
            std::string(definition->nameJa) + " は " + Fixed(definition->minimum)
                + " から " + Fixed(definition->maximum) + " mm までです("
                + Fixed(value) + " mm が入りました)。"
                + std::string(definition->reasonJa)));
    }
    ParameterSet next = set;
    const auto& definitions = ParameterDefinitions();
    if (next.values.size() < definitions.size()) {
        next = DefaultParameters();
    }
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        if (definitions[index].id != id) {
            continue;
        }
        next.values[index].value = value;
        // 書いたものをそのまま残す。あとで打ち直すときに、式が出る。
        next.values[index].expression = std::string(text);
        break;
    }
    return Out::Success(std::move(next));
}

} // namespace kachakacha::v2::app

namespace kachakacha::v2::app {

double ScaledSizeMm(const ParameterSet& set) noexcept
{
    const double denominator = ParameterValueOf(set, ParameterId::ScaleDenominator);
    if (!(denominator > 0.0)) {
        return 0.0;
    }
    return ParameterValueOf(set, ParameterId::RealSizeMm) / denominator;
}

std::string ScaledSizeTextJa(const ParameterSet& set)
{
    const double denominator = ParameterValueOf(set, ParameterId::ScaleDenominator);
    if (!(denominator > 0.0)) {
        return "縮尺が決まっていません。";
    }
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%.4f", ScaledSizeMm(set));
    char scale[64];
    std::snprintf(scale, sizeof(scale), "%.0f", denominator);
    return std::string("1/") + scale + " で " + buffer + " mm";
}

} // namespace kachakacha::v2::app
