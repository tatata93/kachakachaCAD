#include "kachakacha/app/SurfaceInputState.h"

#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/SurfaceCardinality.h"

#include <algorithm>

namespace kachakacha::v2::app {

using modeling::ChainRole;
using modeling::GuideSurfaceMethod;

namespace {

//! 空の並び。無い役割を聞かれたときに返す。
[[nodiscard]] const std::vector<base::EntityId>& NoEntries()
{
    static const std::vector<base::EntityId> empty;
    return empty;
}

} // namespace

const std::vector<GuideSurfaceMethod>& MainSurfaceMethods()
{
    // UI の正本「1. 作り方」の6枚。
    // 2026-09-22: 「案内付きロフト」は「ロフト」へ統合した(ガイド 0 本=通常のロフト、
    // 1 本以上=ガイドで形を決める)。空いた場所へ「四辺面」を入れる。
    // 案内付きロフトは互換のため「その他」に残す(古い文書が開ける・同じ意味で作れる)。
    static const std::vector<GuideSurfaceMethod> methods{
        GuideSurfaceMethod::PlanarBoundary, GuideSurfaceMethod::RuledSections,
        GuideSurfaceMethod::LoftSections, GuideSurfaceMethod::BoundaryFill,
        GuideSurfaceMethod::FourEdgePatch, GuideSurfaceMethod::GordonNetwork};
    return methods;
}

const std::vector<GuideSurfaceMethod>& OtherSurfaceMethods()
{
    // 主要6方式に入らないもの。**既存機能は消さない。**
    static const std::vector<GuideSurfaceMethod> methods{
        GuideSurfaceMethod::OffsetGuide, GuideSurfaceMethod::Revolve,
        GuideSurfaceMethod::GuidedLoft};
    return methods;
}

std::string_view SurfaceSlotNameJa(ChainRole role) noexcept
{
    // 画面の欄は「断面 / ガイド / 境界」の3つ。内部の役割はもう少し細かいので、
    // ここで画面の言葉へ寄せる。
    switch (role) {
    case ChainRole::Section:       return "断面";
    case ChainRole::GuideU:        return "ガイド";
    case ChainRole::GuideV:        return "ガイド(V)";
    case ChainRole::BoundarySide:  return "境界";
    case ChainRole::OuterBoundary: return "外形";
    case ChainRole::HoleBoundary:  return "穴";
    case ChainRole::SourceSurface: return "元の面";
    case ChainRole::Centerline:    return "中心線";
    }
    return "不明";
}

GuideSurfaceMethod RecommendSurfaceMethod(const SurfaceSelectionFacts& facts) noexcept
{
    // 道具を先に押した直後。最も単純な「閉じた輪郭の内側を押す」入口を出す。
    // 断面も面も無いのにロフト待ちにすると、輪郭領域の hover 自体が始まらない。
    if (facts.guideSurfaces == 0 && facts.closedWires == 0 && facts.openWires == 0) {
        return GuideSurfaceMethod::PlanarBoundary;
    }
    // 形状ガイドの面を選んでいるなら、それを離す。
    if (facts.guideSurfaces >= 1 && facts.closedWires == 0 && facts.openWires == 0) {
        return GuideSurfaceMethod::OffsetGuide;
    }
    // **閉じた同一平面の輪郭1本は平面。**
    // ここが「線を2本以上選んでください」で断られていた。
    const std::size_t wires = facts.closedWires + facts.openWires;
    if (facts.closedWires == 1 && wires == 1) {
        return facts.closedPlanarWires == 1 ? GuideSurfaceMethod::PlanarBoundary
                                            : GuideSurfaceMethod::BoundaryFill;
    }
    if (wires >= 3) {
        return GuideSurfaceMethod::LoftSections;
    }
    if (wires == 2) {
        return GuideSurfaceMethod::RuledSections;
    }
    // 1本だけで、閉じた平面でもない。渡す相手がいないので、ロフトのまま待つ。
    return GuideSurfaceMethod::LoftSections;
}

const std::vector<base::EntityId>& SurfaceSlotEntries(const SurfaceInputState& state,
    ChainRole role)
{
    switch (role) {
    case ChainRole::Section:
        return state.sections;
    case ChainRole::GuideU:
    case ChainRole::GuideV:
        return state.guides;
    case ChainRole::BoundarySide:
    case ChainRole::OuterBoundary:
    case ChainRole::HoleBoundary:
        return state.boundaries;
    case ChainRole::SourceSurface:
        return state.sourceSurfaces;
    case ChainRole::Centerline:
        return state.centerlines;
    }
    return NoEntries();
}

SurfaceInputState WithSurfaceEntries(const SurfaceInputState& state, ChainRole role,
    const std::vector<base::EntityId>& ids, bool replace)
{
    SurfaceInputState next = state;
    std::vector<base::EntityId>* slot = nullptr;
    switch (role) {
    case ChainRole::Section:
        slot = &next.sections;
        break;
    case ChainRole::GuideU:
    case ChainRole::GuideV:
        slot = &next.guides;
        break;
    case ChainRole::BoundarySide:
    case ChainRole::OuterBoundary:
    case ChainRole::HoleBoundary:
        slot = &next.boundaries;
        break;
    case ChainRole::SourceSurface:
        slot = &next.sourceSurfaces;
        break;
    case ChainRole::Centerline:
        slot = &next.centerlines;
        break;
    }
    if (slot == nullptr) {
        return next;
    }
    if (replace) {
        slot->clear();
    }
    for (const base::EntityId& id : ids) {
        if (std::find(slot->begin(), slot->end(), id) == slot->end()) {
            slot->push_back(id);
        }
    }
    return next;
}

namespace {

//! 欄の鍵を、入れ物へ寄せる。GuideU/GuideV は同じ入れ物、境界の3つも同じ入れ物。
[[nodiscard]] ChainRole SlotKeyOf(ChainRole role) noexcept
{
    switch (role) {
    case ChainRole::GuideU:
    case ChainRole::GuideV:
        return ChainRole::GuideU;
    case ChainRole::BoundarySide:
    case ChainRole::OuterBoundary:
    case ChainRole::HoleBoundary:
        return ChainRole::BoundarySide;
    case ChainRole::Section:
    case ChainRole::SourceSurface:
    case ChainRole::Centerline:
        break;
    }
    return role;
}

void EraseId(std::vector<base::EntityId>& list, const base::EntityId& id)
{
    list.erase(std::remove(list.begin(), list.end(), id), list.end());
}

} // namespace

SurfaceInputState WithoutSurfaceEntries(const SurfaceInputState& state,
    const std::vector<base::EntityId>& ids)
{
    SurfaceInputState next = state;
    for (const base::EntityId& id : ids) {
        EraseId(next.sections, id);
        EraseId(next.guides, id);
        EraseId(next.boundaries, id);
        EraseId(next.sourceSurfaces, id);
        EraseId(next.centerlines, id);
        // 手動固定の並びからも、採用順からも外す。**古い入力を残さない。**
        EraseId(next.explicitOrder, id);
        EraseId(next.adoptedOrder, id);
    }
    return next;
}

SurfaceInputState WithSurfaceEntriesToggled(const SurfaceInputState& state, ChainRole slot,
    const std::vector<base::EntityId>& ids)
{
    SurfaceInputState next = state;
    const auto& current = SurfaceSlotEntries(state, slot);
    for (const base::EntityId& id : ids) {
        const bool held = std::find(current.begin(), current.end(), id) != current.end();
        if (held) {
            next = WithoutSurfaceEntries(next, {id});   // もう入っている → 外す
            continue;
        }
        // 別の欄に入っているなら、そこから外す。1つのものが2つの役割を持たない。
        next = WithoutSurfaceEntries(next, {id});
        next = WithSurfaceEntries(next, slot, {id}, false);
    }
    return next;
}

SurfaceInputState WithSurfaceSlotCleared(const SurfaceInputState& state, ChainRole slot)
{
    return WithoutSurfaceEntries(state, SurfaceSlotEntries(state, slot));
}

bool CanActivateSurfaceSlot(const SurfaceInputState& state, ChainRole slot) noexcept
{
    const ChainRole key = SlotKeyOf(slot);
    if (key == ChainRole::SourceSurface) {
        return state.method == GuideSurfaceMethod::OffsetGuide;
    }
    ChainRole role = key;
    return RoleForSurfaceSlot(state.method, key, role);
}

SurfaceInputState WithActiveSurfaceSlot(const SurfaceInputState& state, ChainRole slot)
{
    if (!CanActivateSurfaceSlot(state, slot)) {
        return state;   // その作り方では使わない欄。黙って変えない。
    }
    SurfaceInputState next = state;
    next.activeSlot = SlotKeyOf(slot);
    return next;
}

SurfaceInputState WithActiveSlotSettled(const SurfaceInputState& state)
{
    if (CanActivateSurfaceSlot(state, state.activeSlot)) {
        return state;
    }
    SurfaceInputState next = state;
    next.activeSlot = DefaultSurfaceIntakeSlot(state.method);
    return next;
}

SurfaceInputState WithSurfaceSlotAdvanced(const SurfaceInputState& state)
{
    if (state.method != GuideSurfaceMethod::Revolve) {
        return state;
    }
    SurfaceInputState next = state;
    if (next.sections.empty()) {
        next.activeSlot = ChainRole::Section;
    } else if (next.guides.empty()) {
        next.activeSlot = ChainRole::GuideU;   // 軸
    }
    return next;
}

std::vector<base::EntityId> AllSurfaceEntries(const SurfaceInputState& state)
{
    std::vector<base::EntityId> all;
    for (const auto* list : {&state.sections, &state.guides, &state.centerlines,
             &state.boundaries, &state.sourceSurfaces}) {
        for (const base::EntityId& id : *list) {
            if (std::find(all.begin(), all.end(), id) == all.end()) {
                all.push_back(id);
            }
        }
    }
    return all;
}

bool SurfaceSlotHolding(const SurfaceInputState& state, const base::EntityId& id,
    ChainRole& slot) noexcept
{
    for (const ChainRole key : {ChainRole::Section, ChainRole::GuideU,
             ChainRole::Centerline, ChainRole::BoundarySide, ChainRole::SourceSurface}) {
        const auto& list = SurfaceSlotEntries(state, key);
        if (std::find(list.begin(), list.end(), id) != list.end()) {
            slot = key;
            return true;
        }
    }
    return false;
}

std::string_view SurfaceSlotNameJa(GuideSurfaceMethod method, ChainRole role) noexcept
{
    if (method == GuideSurfaceMethod::Revolve && role == ChainRole::GuideU) {
        return "軸";
    }
    if ((method == GuideSurfaceMethod::BoundaryFill
            || method == GuideSurfaceMethod::FourEdgePatch)
        && role == ChainRole::GuideU) {
        return "通る線";
    }
    if (method == GuideSurfaceMethod::FourEdgePatch && role == ChainRole::BoundarySide) {
        return "辺";
    }
    if (method == GuideSurfaceMethod::GordonNetwork) {
        // 曲線網は「断面」の欄が U、「ガイド」の欄が V(header の UI_DEVIATION_REQUEST)。
        if (role == ChainRole::Section) {
            return "U方向の線";
        }
        if (role == ChainRole::GuideU) {
            return "V方向の線";
        }
    }
    return SurfaceSlotNameJa(role);
}

std::string SurfaceActiveSlotHintJa(const SurfaceInputState& state)
{
    const std::size_t count = SurfaceSlotEntries(state, state.activeSlot).size();
    return "次のクリック → " + std::string(SurfaceSlotNameJa(state.method, state.activeSlot))
        + "(" + std::to_string(count + 1) + "本目)";
}

bool RoleForSurfaceSlot(GuideSurfaceMethod method, ChainRole slot, ChainRole& role) noexcept
{
    // 画面の欄と内部の役割は、名前が同じでも一致しない。
    // ここを飛ばすと、平面(外形)も曲線網(外形U/V)も **欄から入れられない。**
    switch (slot) {
    case ChainRole::Section:
        if (method == GuideSurfaceMethod::GordonNetwork) {
            role = ChainRole::GuideU;   // U/V ネットワークの U
            return true;
        }
        role = ChainRole::Section;
        return modeling::RoleUsedByMethod(method, ChainRole::Section);
    case ChainRole::GuideU:
        if (method == GuideSurfaceMethod::GordonNetwork) {
            role = ChainRole::GuideV;   // 同じく V
            return true;
        }
        if (method == GuideSurfaceMethod::Revolve) {
            role = ChainRole::GuideU;   // 回転体の「軸」。表の行にはせず、軸の点と向きへ直す
            return true;
        }
        role = ChainRole::GuideU;
        return modeling::RoleUsedByMethod(method, ChainRole::GuideU);
    case ChainRole::BoundarySide:
        if (method == GuideSurfaceMethod::PlanarBoundary) {
            role = ChainRole::OuterBoundary;   // 平面の「境界」は外形のこと
            return true;
        }
        role = ChainRole::BoundarySide;
        return modeling::RoleUsedByMethod(method, ChainRole::BoundarySide);
    case ChainRole::Centerline:
        role = ChainRole::Centerline;
        return modeling::RoleUsedByMethod(method, ChainRole::Centerline);
    default:
        break;
    }
    return false;
}

ChainRole DefaultSurfaceIntakeSlot(GuideSurfaceMethod method) noexcept
{
    switch (method) {
    case GuideSurfaceMethod::PlanarBoundary:
    case GuideSurfaceMethod::BoundaryFill:
    case GuideSurfaceMethod::FourEdgePatch:
        return ChainRole::BoundarySide;
    case GuideSurfaceMethod::OffsetGuide:
        return ChainRole::SourceSurface;
    default:
        break;
    }
    return ChainRole::Section;
}

std::vector<SurfaceSlotView> SurfaceSlotsFor(const SurfaceInputState& state)
{
    // 画面に出す欄は「断面 / ガイド / 境界」の3つで固定する(UI の正本「2. 入力」)。
    // 作り方で並びが入れ替わると、どこを見ればよいのか分からなくなる。
    std::vector<SurfaceSlotView> views;
    for (int index = 0; index < kSurfaceSlotCount; ++index) {
        const ChainRole slot = SurfaceSlotKey(index);
        SurfaceSlotView view;
        view.role = slot;
        view.count = SurfaceSlotEntries(state, slot).size();
        ChainRole role = slot;
        if (!RoleForSurfaceSlot(state.method, slot, role)) {
            // **この作り方では使わない。入っていても捨てない。**
            view.state = SurfaceSlotState::NotUsedByMethod;
        } else {
            // 下限 0 の役割(ロフトのガイド・中心線、境界面の通る線)は任意。
            const auto* cardinality = modeling::SurfaceCardinalityOf(state.method, role);
            const bool optional = cardinality != nullptr && cardinality->Optional();
            view.state = view.count > 0 ? SurfaceSlotState::Used
                : optional              ? SurfaceSlotState::Optional
                                        : SurfaceSlotState::Missing;
        }
        views.push_back(view);
    }
    return views;
}

std::vector<base::EntityId> SurfaceSectionOrder(const SurfaceInputState& state)
{
    const bool manual = state.ordering == SurfaceOrdering::ManualLock;
    const std::vector<base::EntityId>& preferred =
        manual ? state.explicitOrder : state.adoptedOrder;
    if (!preferred.empty()) {
        // 手動固定: **画面の並びをそのまま生成順にする。**カーネルに並べ替えさせない。
        // 自動: 検査が採用した並びを出す。押した順ではない。
        std::vector<base::EntityId> ordered;
        for (const base::EntityId& id : preferred) {
            if (std::find(state.sections.begin(), state.sections.end(), id)
                != state.sections.end()) {
                ordered.push_back(id);
            }
        }
        // 並びに入っていない断面は、後ろへそのまま足す。落とさない。
        for (const base::EntityId& id : state.sections) {
            if (std::find(ordered.begin(), ordered.end(), id) == ordered.end()) {
                ordered.push_back(id);
            }
        }
        return ordered;
    }
    return state.sections;
}

namespace {

//! 欄ごとの個数の約束。回転体の「軸」は表の行ではない(軸の点と向きへ直す)ので、
//! 定義上 1 本として別に持つ。ほかは SurfaceCardinality.h の表を読む。
struct SlotCount {
    ChainRole slot = ChainRole::Section;
    ChainRole role = ChainRole::Section;
    std::size_t minimum = 0;
    std::size_t maximum = modeling::kUnlimitedCount;
};

[[nodiscard]] std::vector<SlotCount> SlotCountsFor(GuideSurfaceMethod method)
{
    std::vector<SlotCount> counts;
    const ChainRole keys[] = {ChainRole::Section, ChainRole::GuideU, ChainRole::Centerline,
        ChainRole::BoundarySide, ChainRole::SourceSurface};
    for (const ChainRole key : keys) {
        ChainRole role = key;
        if (key == ChainRole::SourceSurface) {
            if (method != GuideSurfaceMethod::OffsetGuide) {
                continue;
            }
        } else if (!RoleForSurfaceSlot(method, key, role)) {
            continue;
        }
        SlotCount count;
        count.slot = key;
        count.role = role;
        if (method == GuideSurfaceMethod::Revolve && key == ChainRole::GuideU) {
            count.minimum = 1;   // 軸。定義上 1 本
            count.maximum = 1;
        } else if (const auto* cardinality =
                       modeling::SurfaceCardinalityOf(method, role)) {
            count.minimum = cardinality->minimum;
            count.maximum = cardinality->maximum;
        }
        counts.push_back(count);
    }
    return counts;
}

//! ロフトの断面の条件(役割をまたぐ)。
[[nodiscard]] std::string LoftRuleProblemJa(const SurfaceInputState& state)
{
    if (state.method != GuideSurfaceMethod::LoftSections
        && state.method != GuideSurfaceMethod::GuidedLoft) {
        return {};
    }
    return modeling::LoftSectionRuleProblemJa(state.sections.size(), state.guides.size());
}

} // namespace

bool SurfaceReadyToBuild(const SurfaceInputState& state)
{
    // 本数の判定は個数の約束(SurfaceCardinality.h)だけを読む。
    // ここに「2本ちょうど」などを書くと、検査・核と食い違う(実際に食い違っていた)。
    for (const SlotCount& count : SlotCountsFor(state.method)) {
        const std::size_t have = SurfaceSlotEntries(state, count.slot).size();
        if (have < count.minimum || have > count.maximum) {
            return false;
        }
    }
    return LoftRuleProblemJa(state).empty();
}

std::string SurfaceCountProblemJa(const SurfaceInputState& state)
{
    if (const std::string rule = LoftRuleProblemJa(state); !rule.empty()) {
        return rule;
    }
    for (const SlotCount& count : SlotCountsFor(state.method)) {
        const std::size_t have = SurfaceSlotEntries(state, count.slot).size();
        const std::string name(SurfaceSlotNameJa(state.method, count.slot));
        if (have >= count.minimum && have <= count.maximum) {
            continue;
        }
        if (have == 0 && count.slot != DefaultSurfaceIntakeSlot(state.method)) {
            // 最初の欄は揃った。次にすることを、そのまま言う(欄の明示遷移)。
            return name + "が要ります。" + name + "の「ここへ選ぶ」を押してから、3D で線を押してください";
        }
        if (state.method == GuideSurfaceMethod::Revolve && count.slot == ChainRole::GuideU) {
            return "回転体の軸は1本です(いま" + std::to_string(have) + "本)";
        }
        std::string problem = have < count.minimum
            ? name + "は" + std::to_string(count.minimum) + "本以上です(いま"
                + std::to_string(have) + "本)"
            : name + "は" + std::to_string(count.maximum) + "本までです(いま"
                + std::to_string(have) + "本)";
        if (count.minimum == count.maximum) {
            problem = name + "はちょうど" + std::to_string(count.minimum) + "本です(いま"
                + std::to_string(have) + "本)";
        }
        return problem;
    }
    return {};
}

std::vector<std::string> SurfaceStatusLinesJa(const SurfaceInputState& state,
    bool previewShown, const std::string& deviationNoteJa)
{
    std::vector<std::string> lines;
    // **今どこへ入るのか**を、いちばん上に出す。人に推測させない。
    lines.push_back("▶ " + SurfaceActiveSlotHintJa(state)
        + "。もう一度押すと外れます");
    for (const SurfaceSlotView& view : SurfaceSlotsFor(state)) {
        const std::string name(SurfaceSlotNameJa(state.method, view.role));
        switch (view.state) {
        case SurfaceSlotState::Used:
            lines.push_back("✓ " + name + " " + std::to_string(view.count) + "本");
            break;
        case SurfaceSlotState::Missing:
            lines.push_back("… " + name + "を選んでください");
            break;
        case SurfaceSlotState::Optional:
            lines.push_back("… " + name + "(任意。無くても作れます)");
            break;
        case SurfaceSlotState::NotUsedByMethod:
            if (view.count > 0) {
                // 入れたものが消えていないことを言う。方式を戻せば、また使える。
                lines.push_back("― " + name + " " + std::to_string(view.count)
                    + "本(この作り方では使いません。消してはいません)");
            }
            break;
        }
    }
    if (!SurfaceReadyToBuild(state)) {
        const std::string why = SurfaceCountProblemJa(state);
        lines.push_back(why.empty() ? std::string("× まだ作れません") : "× " + why);
        return lines;
    }
    lines.push_back("✓ 生成可能");
    // 入力は揃っているのに下見が出ていない = カーネルが断った、ということ。
    // 「作れます」と出したまま黙らない(できないことを、できたことにしない)。
    lines.push_back(previewShown
            ? "✓ 下見を表示中(まだ文書へ保存していません)"
            : "× 下見が作れませんでした。確定を押すと理由が出ます");
    if (!deviationNoteJa.empty()) {
        lines.push_back("! " + deviationNoteJa);
    }
    if (state.ordering == SurfaceOrdering::ManualLock) {
        lines.push_back("断面順: 手動固定(画面の並びのまま作ります)");
    } else {
        lines.push_back("断面順: 自動(幾何の位置から並べます)");
    }
    return lines;
}

} // namespace kachakacha::v2::app
