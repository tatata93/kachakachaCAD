#include "kachakacha/fabrication/FreezeMaterialize.h"

#include <cmath>
#include <cstdio>

namespace kachakacha::v2::fabrication {
namespace {

using base::MakeError;
using base::Result;

[[nodiscard]] std::string WireName(std::string_view baseNameJa, const FrozenWire& wire,
    double percent)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g%%", percent);
    return std::string(baseNameJa) + " " + std::string(FrozenWireKindNameJa(wire.kind))
        + " " + wire.sourceId + " (" + buffer + ")";
}

[[nodiscard]] std::string PartName(std::string_view baseNameJa,
    const PanelSolidRequest& part, double percent)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g%%", percent);
    return std::string(baseNameJa) + " 部材 " + part.panelId + " (" + buffer + ")";
}

} // namespace

bool IsDirectlyEditable(domain::EditPolicy policy) noexcept
{
    // 派生物は直接編集しない(PRD-003)。固定して独立させてから編集する。
    return policy != domain::EditPolicy::Derived;
}

Result<MaterializeResult> MaterializeFrozenState(const FreezeBundle& bundle,
    std::string_view baseNameJa)
{
    using Out = Result<MaterializeResult>;
    if (baseNameJa.empty()) {
        return Out::Failure(MakeError("FAB-E003",
            "固定するもとの部品が見つかりません。", "名前が空です。"));
    }
    const bool wantsWires = bundle.output == FreezeOutput::WiresOnly
        || bundle.output == FreezeOutput::Both;
    const bool wantsParts = bundle.output == FreezeOutput::PartsOnly
        || bundle.output == FreezeOutput::Both;
    if (wantsWires && bundle.wires.empty()) {
        return Out::Failure(MakeError("FAB-E003",
            "固定するもとの部品が見つかりません。",
            "ワイヤーを固定するよう指定されていますが、束にワイヤーがありません。"));
    }
    if (wantsParts && bundle.parts.empty()) {
        return Out::Failure(MakeError("FAB-E003",
            "固定するもとの部品が見つかりません。",
            "部品を固定するよう指定されていますが、束に部材がありません。"));
    }

    MaterializeResult result;
    result.percent = bundle.percent;
    result.output = bundle.output;
    if (wantsWires) {
        for (const FrozenWire& wire : bundle.wires) {
            if (wire.segments.empty()) {
                return Out::Failure(MakeError("FAB-E002",
                    "折った立体が正しく作れませんでした。",
                    "線の入っていないワイヤーがあります: " + wire.sourceId));
            }
            MaterializedEntity made;
            made.kind = domain::EntityKind::Wire;
            // 固定したものは元から独立する。派生のままにしない。
            made.editPolicy = domain::EditPolicy::Frozen;
            made.displayName = WireName(baseNameJa, wire, bundle.percent);
            made.segments = wire.segments;   // 値でコピーする。参照で持たない。
            made.sourceId = wire.sourceId;
            result.entities.push_back(std::move(made));
        }
    }
    if (wantsParts) {
        for (const PanelSolidRequest& part : bundle.parts) {
            if (part.outline.size() < 3) {
                return Out::Failure(MakeError("FAB-E002",
                    "折った立体が正しく作れませんでした。",
                    "輪郭の点が足りない部材があります: " + part.panelId));
            }
            if (!(part.thicknessMm > 0.0)) {
                return Out::Failure(MakeError("FAB-E002",
                    "折った立体が正しく作れませんでした。",
                    "厚みが正の数でない部材があります: " + part.panelId));
            }
            MaterializedEntity made;
            made.kind = domain::EntityKind::Part;
            made.editPolicy = domain::EditPolicy::Frozen;
            made.displayName = PartName(baseNameJa, part, bundle.percent);
            made.solidRequest = part;
            made.sourceId = part.panelId;
            result.entities.push_back(std::move(made));
        }
    }
    return Out::Success(std::move(result));
}

bool IsIndependentOfSource(const MaterializeResult& frozen,
    const FreezeBundle& changedSource)
{
    // 固定したものは値のコピーである。元の束が変わっても、
    // 固定したものの中身は変わらない。ここでは「元と一致しなくなっていること」
    // ではなく「固定したものが元を参照していないこと」を、
    // 元を変えたあとの値と突き合わせて確かめる。
    for (const MaterializedEntity& entity : frozen.entities) {
        if (entity.kind != domain::EntityKind::Wire) {
            continue;
        }
        for (const FrozenWire& wire : changedSource.wires) {
            if (wire.sourceId != entity.sourceId) {
                continue;
            }
            if (wire.segments.size() != entity.segments.size()) {
                return true;   // 元が変わっても固定側は変わっていない。
            }
            for (std::size_t at = 0; at < wire.segments.size(); ++at) {
                const double moved =
                    (wire.segments[at].StartPoint() - entity.segments[at].StartPoint())
                        .Length();
                if (moved > 1.0e-9) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace kachakacha::v2::fabrication
