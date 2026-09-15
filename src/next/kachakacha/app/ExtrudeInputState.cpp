#include "kachakacha/app/ExtrudeInputState.h"

#include <algorithm>

namespace kachakacha::v2::app {

ExtrudeOutputs OutputsForPreset(ExtrudeOutputPreset preset) noexcept
{
    ExtrudeOutputs outputs;
    switch (preset) {
    case ExtrudeOutputPreset::SolidOnly:
        outputs.body = true;
        outputs.startWire = false;
        outputs.endWire = false;
        outputs.sideWires = false;
        return outputs;
    case ExtrudeOutputPreset::WiresOnly:
        outputs.body = false;
        outputs.startWire = true;
        outputs.endWire = true;
        outputs.sideWires = true;
        return outputs;
    case ExtrudeOutputPreset::WiresAndSolid:
        outputs.body = true;
        outputs.startWire = true;
        outputs.endWire = true;
        outputs.sideWires = true;
        return outputs;
    case ExtrudeOutputPreset::EndWireOnly:
        outputs.body = false;
        outputs.startWire = false;
        outputs.endWire = true;
        outputs.sideWires = false;
        return outputs;
    case ExtrudeOutputPreset::Custom:
        break;
    }
    return outputs;   // 自由選択。既定はソリッドのみと同じ形から始める。
}

ExtrudeOutputPreset PresetForOutputs(const ExtrudeOutputs& outputs) noexcept
{
    for (const ExtrudeOutputPreset preset : ExtrudeOutputPresets()) {
        if (preset == ExtrudeOutputPreset::Custom) {
            continue;
        }
        if (OutputsForPreset(preset) == outputs) {
            return preset;
        }
    }
    return ExtrudeOutputPreset::Custom;
}

std::string_view ExtrudeOutputPresetNameJa(ExtrudeOutputPreset preset) noexcept
{
    switch (preset) {
    case ExtrudeOutputPreset::SolidOnly:     return "ソリッドのみ";
    case ExtrudeOutputPreset::WiresOnly:     return "ワイヤーのみ";
    case ExtrudeOutputPreset::WiresAndSolid: return "ワイヤー + ソリッド";
    case ExtrudeOutputPreset::EndWireOnly:   return "押し出し先ワイヤーのみ";
    case ExtrudeOutputPreset::Custom:        return "カスタム";
    }
    return "不明";
}

const std::vector<ExtrudeOutputPreset>& ExtrudeOutputPresets()
{
    static const std::vector<ExtrudeOutputPreset> presets{
        ExtrudeOutputPreset::SolidOnly, ExtrudeOutputPreset::WiresOnly,
        ExtrudeOutputPreset::WiresAndSolid, ExtrudeOutputPreset::EndWireOnly,
        ExtrudeOutputPreset::Custom};
    return presets;
}

std::string ExtrudeOutputsTextJa(const ExtrudeOutputs& outputs)
{
    if (!outputs.Any()) {
        return "何も作りません";
    }
    std::string text;
    const auto add = [&text](const char* word) {
        if (!text.empty()) {
            text += " + ";
        }
        text += word;
    };
    if (outputs.body) { add("ソリッド"); }
    if (outputs.startWire) { add("開始側の輪郭"); }
    if (outputs.endWire) { add("押し出し先の輪郭"); }
    if (outputs.sideWires) { add("側面"); }
    return text;
}

std::string_view ExtrudeSlotNameJa(ExtrudeSlot slot) noexcept
{
    switch (slot) {
    case ExtrudeSlot::None:    return "";
    case ExtrudeSlot::Target:  return "対象";
    case ExtrudeSlot::Profile: return "輪郭";
    }
    return "";
}

ExtrudeSlot NextNeededSlot(const ExtrudeInputState& state) noexcept
{
    if (!state.HasProfile()) {
        return ExtrudeSlot::Profile;
    }
    // 足す・引くなら相手が要る。相手を勝手に選ばない。
    const bool needsTarget = state.operation != modeling::ExtrudeBooleanMode::NewPart;
    if (needsTarget && !state.HasTarget()) {
        return ExtrudeSlot::Target;
    }
    return ExtrudeSlot::None;
}

bool PickFitsSlot(ExtrudeSlot slot, PickedKind kind) noexcept
{
    switch (slot) {
    case ExtrudeSlot::Target:
        // 対象は立体だけ。面を拾ったときも、その立体を指しているので受ける。
        return kind == PickedKind::Solid || kind == PickedKind::SolidFace;
    case ExtrudeSlot::Profile:
        // 押せるのは閉じた輪郭か、立体の平らな面。
        return kind == PickedKind::ClosedWire || kind == PickedKind::SolidFace;
    case ExtrudeSlot::None:
        break;
    }
    return false;
}

ExtrudeInputState ApplyPick(const ExtrudeInputState& state, const PickedEntity& picked,
    bool addToProfiles)
{
    ExtrudeInputState next = state;
    if (picked.kind == PickedKind::None) {
        return next;
    }
    const ExtrudeSlot wanted = NextNeededSlot(state);
    // いま要求しているスロットに入るなら、そこへ入れる。
    // **入らないものを、黙って前の入力の代わりにしない。**
    if (wanted == ExtrudeSlot::Profile && PickFitsSlot(ExtrudeSlot::Profile, picked.kind)) {
        if (picked.kind == PickedKind::SolidFace) {
            next.profiles = {picked.entityId};
            next.profileIsFace = true;
            next.faceIndex = picked.faceIndex;
            // 面を押すときの相手は、その面を持つ立体そのものである。
            next.target = picked.entityId;
            return next;
        }
        next.profileIsFace = false;
        next.faceIndex.reset();
        if (addToProfiles) {
            if (std::find(next.profiles.begin(), next.profiles.end(), picked.entityId)
                == next.profiles.end()) {
                next.profiles.push_back(picked.entityId);
            }
            return next;
        }
        next.profiles = {picked.entityId};
        return next;
    }
    if (wanted == ExtrudeSlot::Target && PickFitsSlot(ExtrudeSlot::Target, picked.kind)) {
        next.target = picked.entityId;
        return next;
    }
    // 要求しているスロットには入らない。埋まっているほうへ入れ直せるなら入れる。
    // **立体を拾ったら対象、輪郭を拾ったら輪郭。**型を見れば決まる。
    if (picked.kind == PickedKind::Solid) {
        next.target = picked.entityId;
        return next;
    }
    if (picked.kind == PickedKind::ClosedWire) {
        next.profileIsFace = false;
        next.faceIndex.reset();
        if (addToProfiles) {
            if (std::find(next.profiles.begin(), next.profiles.end(), picked.entityId)
                == next.profiles.end()) {
                next.profiles.push_back(picked.entityId);
            }
            return next;
        }
        next.profiles = {picked.entityId};
        return next;
    }
    if (picked.kind == PickedKind::SolidFace) {
        next.profiles = {picked.entityId};
        next.profileIsFace = true;
        next.faceIndex = picked.faceIndex;
        next.target = picked.entityId;
        return next;
    }
    return next;   // 使えないものは、何も変えない。
}

bool ReadyForPreview(const ExtrudeInputState& state) noexcept
{
    return NextNeededSlot(state) == ExtrudeSlot::None && state.HasProfile();
}

bool OperationApplies(const ExtrudeInputState& state) noexcept
{
    // ソリッドを作らないなら、足す・引くは起きようがない。
    return state.outputs.body;
}

std::vector<std::string> ExtrudeStatusLinesJa(const ExtrudeInputState& state,
    const std::string& targetNameJa, const std::vector<std::string>& profileNamesJa,
    bool previewShown)
{
    std::vector<std::string> lines;
    if (!targetNameJa.empty()) {
        lines.push_back("✓ 対象: " + targetNameJa);
    }
    if (!profileNamesJa.empty()) {
        std::string joined;
        for (const std::string& name : profileNamesJa) {
            if (!joined.empty()) {
                joined += "、";
            }
            joined += name;
        }
        lines.push_back(std::string("✓ ") + (state.profileIsFace ? "面" : "輪郭") + ": "
            + joined);
    }
    const ExtrudeSlot needed = NextNeededSlot(state);
    if (needed != ExtrudeSlot::None) {
        lines.push_back(std::string("… ") + std::string(ExtrudeSlotNameJa(needed))
            + "を選んでください");
        return lines;
    }
    if (!state.outputs.Any()) {
        // 全部 OFF は確定できない。理由をここで言う。
        lines.push_back("× 作るものが1つも選ばれていません");
        lines.push_back("　 出力のどれか1つを入れてください");
        return lines;
    }
    lines.push_back("✓ 入力は有効です");
    if (previewShown) {
        lines.push_back("✓ 下見を表示中");
    }
    lines.push_back("出力: " + ExtrudeOutputsTextJa(state.outputs));
    if (!OperationApplies(state)) {
        // 演算が効かないことを、理由つきで言う。
        lines.push_back("演算: 適用なし(ソリッドを作らないため)");
        return lines;
    }
    if (state.operation == modeling::ExtrudeBooleanMode::NewPart || !state.HasTarget()) {
        lines.push_back("確定すると新しい部品を作ります");
        return lines;
    }
    const bool adding = state.operation == modeling::ExtrudeBooleanMode::AddToPart;
    lines.push_back("確定すると " + targetNameJa + " に「" + (adding ? "足す" : "引く")
        + "」で適用します");
    return lines;
}

} // namespace kachakacha::v2::app
