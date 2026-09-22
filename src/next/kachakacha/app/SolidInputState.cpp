#include "kachakacha/app/SolidInputState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kachakacha::v2::app {

using modeling::SolidMethod;

namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] bool Contains(const std::vector<base::EntityId>& list, const base::EntityId& id)
{
    return std::find(list.begin(), list.end(), id) != list.end();
}

void Remove(std::vector<base::EntityId>& list, const base::EntityId& id)
{
    list.erase(std::remove(list.begin(), list.end(), id), list.end());
}

[[nodiscard]] bool SlotFilled(const SolidInputState& state, SolidSlot slot)
{
    switch (slot) {
    case SolidSlot::Profiles: return state.method == SolidMethod::Loft ? state.profiles.size() >= 2
                                                                       : !state.profiles.empty();
    case SolidSlot::Axis:     return !state.axis.IsNil();
    case SolidSlot::Path:     return !state.path.empty();
    case SolidSlot::Target:   return !state.target.IsNil();
    }
    return false;
}

//! その種類のものが、その欄に入れられるか。
[[nodiscard]] bool Fits(SolidSlot slot, SolidPickKind kind)
{
    switch (slot) {
    case SolidSlot::Profiles: return kind == SolidPickKind::ClosedWire;
    case SolidSlot::Axis:     return kind == SolidPickKind::LineWire;
    case SolidSlot::Path:     return kind == SolidPickKind::LineWire || kind == SolidPickKind::OpenWire;
    case SolidSlot::Target:   return kind == SolidPickKind::Part;
    }
    return false;
}

[[nodiscard]] std::string Degrees(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f°", value);
    return buffer;
}

[[nodiscard]] std::string Cubic(double value)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.4f mm3", value);
    return buffer;
}

} // namespace

std::string_view SolidSlotKey(SolidSlot slot) noexcept
{
    switch (slot) {
    case SolidSlot::Profiles: return "PROFILE";
    case SolidSlot::Axis:     return "AXIS";
    case SolidSlot::Path:     return "PATH";
    case SolidSlot::Target:   return "TARGET";
    }
    return "PROFILE";
}

std::string_view SolidSlotNameJa(SolidSlot slot, SolidMethod method) noexcept
{
    switch (slot) {
    case SolidSlot::Profiles: return method == SolidMethod::Loft ? "断面" : "輪郭";
    case SolidSlot::Axis:     return "回転軸";
    case SolidSlot::Path:     return "経路";
    case SolidSlot::Target:   return "相手";
    }
    return "輪郭";
}

std::string_view RevolveModeNameJa(RevolveMode mode) noexcept
{
    switch (mode) {
    case RevolveMode::Full:      return "全回転";
    case RevolveMode::Angle:     return "角度指定";
    case RevolveMode::Symmetric: return "対称回転";
    }
    return "全回転";
}

std::string_view SolidBooleanNameJa(int booleanMode) noexcept
{
    return booleanMode == 1 ? "足す" : booleanMode == 2 ? "引く" : "新しい部品";
}

const std::array<SolidMethodCard, 3>& SolidMethodCards(SolidMethod method)
{
    static const std::array<SolidMethodCard, 3> revolve{{
        {"全回転", "輪郭を回転軸のまわりに 360° 回して立体にします。", true},
        {"角度指定", "輪郭を回転軸のまわりに、決めた角度だけ回します(0° より大きく 360° まで)。", true},
        {"対称回転", "輪郭の面を中心に、両側へ角度の半分ずつ回します。", true},
    }};
    static const std::array<SolidMethodCard, 3> loft{{
        {"断面のみ", "閉じた断面を押した順に通して立体にします(断面は 2 つ以上、何個でも)。", true},
        {"ガイド付き",
            "まだ作れません。立体のロフトにガイド線を通す作り方は、この版にまだ入っていません。"
            "面作成の「ロフト面」(ガイド 0〜任意)で面を作り、「厚み」で立体にしてください。",
            false},
        {"中心線付き",
            "まだ作れません。立体のロフトを中心線に沿わせる作り方は、この版にまだ入っていません。",
            false},
    }};
    static const std::array<SolidMethodCard, 3> sweep{{
        {"一定断面", "輪郭の形を保ったまま、経路に沿って動かして立体にします(姿勢は経路に追従)。", true},
        {"ねじれ指定",
            "まだ作れません。経路に沿ってねじる作り方は、この版にまだ入っていません。", false},
        {"ガイド付き",
            "まだ作れません。ガイド線で姿勢・大きさを補う作り方は、この版にまだ入っていません。",
            false},
    }};
    switch (method) {
    case SolidMethod::Revolve: return revolve;
    case SolidMethod::Loft:    return loft;
    case SolidMethod::Sweep:   return sweep;
    }
    return revolve;
}

int SolidMethodCardIndex(const SolidInputState& state) noexcept
{
    if (state.method != SolidMethod::Revolve) {
        return 0;
    }
    switch (state.revolveMode) {
    case RevolveMode::Full:      return 0;
    case RevolveMode::Angle:     return 1;
    case RevolveMode::Symmetric: return 2;
    }
    return 0;
}

RevolveMode RevolveModeOfCard(int index) noexcept
{
    return index == 1 ? RevolveMode::Angle : index == 2 ? RevolveMode::Symmetric : RevolveMode::Full;
}

bool SolidMethodForCommand(std::string_view commandId, SolidMethod& method) noexcept
{
    if (commandId == "part.revolve") {
        method = SolidMethod::Revolve;
        return true;
    }
    if (commandId == "part.loft_solid") {
        method = SolidMethod::Loft;
        return true;
    }
    if (commandId == "part.sweep") {
        method = SolidMethod::Sweep;
        return true;
    }
    return false;
}

SolidInputState WithSolidMethod(const SolidInputState& state, SolidMethod method)
{
    SolidInputState next = state;
    if (next.method == method) {
        return next;
    }
    next.method = method;
    // 軸は回転体だけ、経路はスイープだけの欄。持ち越すと、見えない欄に入ったまま残る。
    next.axis = base::EntityId{};
    next.path.clear();
    next.activeSlot.reset();
    return next;
}

SolidInputState WithSolidBoolean(const SolidInputState& state, int booleanMode)
{
    SolidInputState next = state;
    next.booleanMode = booleanMode == 1 || booleanMode == 2 ? booleanMode : 0;
    if (next.booleanMode == 0) {
        next.target = base::EntityId{};
        if (next.activeSlot == SolidSlot::Target) {
            next.activeSlot.reset();
        }
    }
    return next;
}

std::vector<SolidSlot> SolidSlotsFor(const SolidInputState& state)
{
    std::vector<SolidSlot> wanted{SolidSlot::Profiles};
    if (state.method == SolidMethod::Revolve) {
        wanted.push_back(SolidSlot::Axis);
    } else if (state.method == SolidMethod::Sweep) {
        wanted.push_back(SolidSlot::Path);
    }
    if (state.booleanMode != 0) {
        wanted.push_back(SolidSlot::Target);
    }
    return wanted;
}

SolidSlot NextSolidSlot(const SolidInputState& state)
{
    const auto wanted = SolidSlotsFor(state);
    if (state.activeSlot.has_value()
        && std::find(wanted.begin(), wanted.end(), *state.activeSlot) != wanted.end()) {
        return *state.activeSlot;
    }
    for (const SolidSlot slot : wanted) {
        if (!SlotFilled(state, slot)) {
            return slot;
        }
    }
    return SolidSlot::Profiles;
}

SolidInputState WithSolidPick(const SolidInputState& state, const base::EntityId& id,
    SolidPickKind kind, std::string* whyJa)
{
    SolidInputState next = state;
    if (id.IsNil()) {
        return next;
    }
    // 入っているものを押せば外れる(どの欄でも)。
    if (Contains(next.profiles, id)) {
        Remove(next.profiles, id);
        next.activeSlot = SolidSlot::Profiles;
        return next;
    }
    if (next.axis == id) {
        next.axis = base::EntityId{};
        next.activeSlot = SolidSlot::Axis;
        return next;
    }
    if (Contains(next.path, id)) {
        Remove(next.path, id);
        next.activeSlot = SolidSlot::Path;
        return next;
    }
    if (next.target == id) {
        next.target = base::EntityId{};
        next.activeSlot = SolidSlot::Target;
        return next;
    }
    const auto wanted = SolidSlotsFor(next);
    const auto allowed = [&wanted](SolidSlot slot) {
        return std::find(wanted.begin(), wanted.end(), slot) != wanted.end();
    };
    // 明示した欄が種類に合えばそこへ。合わなければ種類で決める。
    std::optional<SolidSlot> slot;
    if (next.activeSlot.has_value() && allowed(*next.activeSlot) && Fits(*next.activeSlot, kind)) {
        slot = next.activeSlot;
    } else if (kind == SolidPickKind::ClosedWire) {
        slot = SolidSlot::Profiles;
    } else if (kind == SolidPickKind::Part && allowed(SolidSlot::Target)) {
        slot = SolidSlot::Target;
    } else if (kind == SolidPickKind::LineWire && allowed(SolidSlot::Axis)) {
        slot = SolidSlot::Axis;
    } else if ((kind == SolidPickKind::LineWire || kind == SolidPickKind::OpenWire)
        && allowed(SolidSlot::Path)) {
        slot = SolidSlot::Path;
    }
    if (!slot.has_value()) {
        if (whyJa != nullptr) {
            *whyJa = kind == SolidPickKind::Part
                ? "部品は「足す」「引く」のときだけ相手に入ります(いまは新しい部品を作ります)。"
                : state.method == SolidMethod::Revolve
                ? "回転体に入れられるのは、閉じた輪郭と、軸にする直線 1 本です。"
                : state.method == SolidMethod::Loft ? "ロフト立体の断面は、閉じた線にしてください。"
                                                    : "スイープに入れられるのは、閉じた輪郭と経路の線です。";
        }
        return next;
    }
    switch (*slot) {
    case SolidSlot::Profiles: next.profiles.push_back(id); break;
    case SolidSlot::Axis:     next.axis = id; break;
    case SolidSlot::Path:     next.path.push_back(id); break;
    case SolidSlot::Target:   next.target = id; break;
    }
    next.activeSlot.reset();   // 入れたら明示は解ける
    return next;
}

SolidInputState WithoutSolidEntries(const SolidInputState& state,
    const std::vector<base::EntityId>& ids)
{
    SolidInputState next = state;
    for (const auto& id : ids) {
        Remove(next.profiles, id);
        Remove(next.path, id);
        if (next.axis == id) {
            next.axis = base::EntityId{};
        }
        if (next.target == id) {
            next.target = base::EntityId{};
        }
    }
    return next;
}

SolidInputState WithSolidSlotCleared(const SolidInputState& state, SolidSlot slot)
{
    SolidInputState next = state;
    switch (slot) {
    case SolidSlot::Profiles: next.profiles.clear(); break;
    case SolidSlot::Axis:     next.axis = base::EntityId{}; break;
    case SolidSlot::Path:     next.path.clear(); break;
    case SolidSlot::Target:   next.target = base::EntityId{}; break;
    }
    next.activeSlot = slot;
    return next;
}

SolidInputState WithActiveSolidSlot(const SolidInputState& state, SolidSlot slot)
{
    SolidInputState next = state;
    next.activeSlot = slot;
    return next;
}

std::vector<base::EntityId> SolidEntries(const SolidInputState& state)
{
    std::vector<base::EntityId> entries = state.profiles;
    if (state.method == SolidMethod::Revolve && !state.axis.IsNil()) {
        entries.push_back(state.axis);
    }
    if (state.method == SolidMethod::Sweep) {
        entries.insert(entries.end(), state.path.begin(), state.path.end());
    }
    if (state.booleanMode != 0 && !state.target.IsNil()) {
        entries.push_back(state.target);
    }
    return entries;
}

bool SolidReady(const SolidInputState& state) noexcept
{
    for (const SolidSlot slot : SolidSlotsFor(state)) {
        if (!SlotFilled(state, slot)) {
            return false;
        }
    }
    return true;
}

double SolidAngleRad(const SolidInputState& state) noexcept
{
    if (state.revolveMode == RevolveMode::Full) {
        return 2.0 * kPi;
    }
    return state.angleDeg * kPi / 180.0;
}

std::string SolidHintJa(const SolidInputState& state)
{
    if (SolidReady(state) && !state.activeSlot.has_value()) {
        return "Enter で確定(3D で押し直すと外れます)";
    }
    const SolidSlot next = NextSolidSlot(state);
    std::string line = "次のクリック → " + std::string(SolidSlotNameJa(next, state.method));
    if (next == SolidSlot::Profiles && state.method == SolidMethod::Loft) {
        line += "(通す順に押してください。いま " + std::to_string(state.profiles.size()) + " つ)";
    }
    return line;
}

std::vector<std::string> SolidStatusLinesJa(const SolidInputState& state,
    const SolidPreviewOutcome& outcome, bool previewShown)
{
    std::vector<std::string> lines;
    lines.push_back("▶ " + SolidHintJa(state));
    for (const SolidSlot slot : SolidSlotsFor(state)) {
        const std::string name(SolidSlotNameJa(slot, state.method));
        std::string value;
        switch (slot) {
        case SolidSlot::Profiles:
            value = state.profiles.empty() ? "× まだ"
                : "✓ " + std::to_string(state.profiles.size()) + " つ"
                    + (state.method == SolidMethod::Loft && state.profiles.size() < 2
                            ? "(断面は 2 つ以上)" : "");
            break;
        case SolidSlot::Axis:   value = state.axis.IsNil() ? "× まだ" : "✓ 入っている"; break;
        case SolidSlot::Path:
            value = state.path.empty() ? "× まだ" : "✓ " + std::to_string(state.path.size()) + " 本";
            break;
        case SolidSlot::Target: value = state.target.IsNil() ? "× まだ" : "✓ 入っている"; break;
        }
        lines.push_back(name + ": " + value);
    }
    if (!SolidReady(state)) {
        return lines;
    }
    if (!outcome.evaluated) {
        return lines;
    }
    if (!outcome.available) {
        lines.push_back("× " + (outcome.refusalJa.empty() ? std::string("作れません") : outcome.refusalJa));
        return lines;
    }
    lines.push_back("✓ 生成可能: 体積 " + Cubic(outcome.volumeMm3));
    lines.push_back(previewShown ? "✓ 下見を表示中(まだ文書へ保存していません)"
                                 : "× 下見が作れませんでした");
    return lines;
}

std::string SolidFooterLine(const SolidInputState& state, const std::string& profileNames,
    const std::string& axisOrPathNames, const SolidPreviewOutcome& outcome, bool previewShown)
{
    std::string line = std::string(modeling::SolidMethodNameJa(state.method)) + ": ";
    line += "PROFILE=" + (state.profiles.empty() ? std::string("(なし)") : profileNames);
    if (state.method == SolidMethod::Revolve) {
        line += " / AXIS=" + (state.axis.IsNil() ? std::string("(なし)") : axisOrPathNames);
        line += " / " + (state.revolveMode == RevolveMode::Full ? std::string("360.0°")
                                                                : Degrees(state.angleDeg));
        if (state.revolveMode == RevolveMode::Symmetric) {
            line += " SYM";
        }
    } else if (state.method == SolidMethod::Sweep) {
        line += " / PATH=" + (state.path.empty() ? std::string("(なし)") : axisOrPathNames);
    }
    line += state.booleanMode == 1 ? " / ADD" : state.booleanMode == 2 ? " / CUT" : " / NEW";
    if (!SolidReady(state) || state.activeSlot.has_value()) {
        line += " / NEXT=" + std::string(SolidSlotKey(NextSolidSlot(state)));
    }
    if (outcome.evaluated && outcome.available) {
        line += " / VOLUME=" + Cubic(outcome.volumeMm3);
    }
    line += previewShown ? " / Preview only" : " / no preview";
    return line;
}

} // namespace kachakacha::v2::app
