#include "kachakacha/app/ExtrudePlan.h"

#include "kachakacha/app/ExtrudeOptions.h"

namespace kachakacha::v2::app {
namespace {

using modeling::ExtrudeBooleanMode;

//! 立体を相手にするときに選べる操作。
//! 「共通部分」はまだカーネルに無いので並べない。
//! 並べておいて押したら断る、をしない(できないことを、できたことにしない)。
[[nodiscard]] std::vector<ExtrudeBooleanMode> OperationsWithSolid()
{
    return {ExtrudeBooleanMode::AddToPart, ExtrudeBooleanMode::SubtractFromPart,
        ExtrudeBooleanMode::NewPart};
}

} // namespace

std::string_view ExtrudeInputKindNameJa(ExtrudeInputKind kind) noexcept
{
    switch (kind) {
    case ExtrudeInputKind::Nothing:         return "何も選んでいない";
    case ExtrudeInputKind::ProfileOnly:     return "輪郭だけ";
    case ExtrudeInputKind::FaceOnly:        return "面だけ";
    case ExtrudeInputKind::SolidOnly:       return "立体だけ";
    case ExtrudeInputKind::SolidAndProfile: return "立体と輪郭";
    case ExtrudeInputKind::SolidAndFace:    return "立体と面";
    case ExtrudeInputKind::Unusable:        return "押し出せない組み合わせ";
    }
    return "不明";
}

ExtrudePlan PlanExtrude(const ExtrudeSelectionFacts& facts,
    const std::vector<base::EntityId>& solidIds,
    const std::vector<base::EntityId>& profileIds, bool profilesAreFaces)
{
    ExtrudePlan plan;
    plan.profileIsFace = profilesAreFaces;

    const bool hasSolid = facts.solids > 0;
    const bool hasFace = facts.faces > 0;
    const bool hasClosed = facts.closedWires > 0;

    if (!hasSolid && !hasFace && !hasClosed) {
        if (facts.openWires > 0) {
            // 開いた輪郭は押し出しても立体にならない。何が足りないかを言う。
            plan.kind = ExtrudeInputKind::Unusable;
            plan.needsJa = "開いた輪郭は押し出しても立体になりません。"
                           "閉じた輪郭、面、立体のどれかを選んでください。";
            return plan;
        }
        if (facts.surfaces > 0) {
            plan.kind = ExtrudeInputKind::Unusable;
            plan.needsJa = "曲面は押し出せません。曲面に厚みを付けたいなら"
                           "「面に厚みを付ける」を使ってください。";
            return plan;
        }
        plan.kind = ExtrudeInputKind::Nothing;
        plan.needsJa = "押し出す輪郭か面を選んでください。"
                       "先に立体を選ぶと、その立体を加工します。";
        return plan;
    }

    // 面と輪郭の両方を選ぶと、どちらが形なのか決まらない。ここで断る。
    if (hasFace && hasClosed) {
        plan.kind = ExtrudeInputKind::Unusable;
        plan.needsJa = "面と輪郭の両方が選ばれています。どちらで押し出すかを"
                       "決められません。face か輪郭か、片方だけにしてください。";
        return plan;
    }

    if (hasSolid) {
        // 立体が2つ以上あると、どれを加工するのか決まらない。
        if (facts.solids > 1) {
            plan.kind = ExtrudeInputKind::Unusable;
            plan.needsJa = "立体が2つ以上選ばれています。加工する立体を1つにしてください。";
            return plan;
        }
        plan.targetSolid = solidIds.empty() ? base::EntityId{} : solidIds.front();
        plan.operations = OperationsWithSolid();
        if (hasFace) {
            plan.kind = ExtrudeInputKind::SolidAndFace;
            plan.profiles = profileIds;
            // 面を押し引きするなら、既定は「その立体に足す/削る」。
            // 新しい立体にしたい人のほうが少ない。
            plan.defaultOperation = ExtrudeBooleanMode::AddToPart;
            plan.readyToPreview = !plan.profiles.empty();
            if (!plan.readyToPreview) {
                plan.needsJa = "押し引きする面を選んでください。";
            }
            return plan;
        }
        if (hasClosed) {
            plan.kind = ExtrudeInputKind::SolidAndProfile;
            plan.profiles = profileIds;
            // 立体に輪郭を当てるのは、たいてい窓や穴を開けるためである。
            plan.defaultOperation = ExtrudeBooleanMode::SubtractFromPart;
            plan.readyToPreview = !plan.profiles.empty();
            return plan;
        }
        // 立体だけ。相手は決まった。形がまだ。
        plan.kind = ExtrudeInputKind::SolidOnly;
        plan.defaultOperation = ExtrudeBooleanMode::SubtractFromPart;
        plan.readyToPreview = false;
        plan.needsJa = "押し出す面または輪郭を選んでください。";
        return plan;
    }

    // 立体を選んでいない。新しい立体を作る。
    plan.operations = {ExtrudeBooleanMode::NewPart};
    plan.defaultOperation = ExtrudeBooleanMode::NewPart;
    plan.profiles = profileIds;
    plan.readyToPreview = !plan.profiles.empty();
    plan.kind = hasFace ? ExtrudeInputKind::FaceOnly : ExtrudeInputKind::ProfileOnly;
    if (!plan.readyToPreview) {
        plan.needsJa = hasFace ? "押し出す面を選んでください。"
                               : "押し出す輪郭を選んでください。";
    }
    return plan;
}

std::string ExplainExtrudePlanJa(const ExtrudePlan& plan, const std::string& targetNameJa,
    const std::vector<std::string>& profileNamesJa, ExtrudeBooleanMode operation)
{
    std::string text;
    const auto line = [&text](const std::string& label, const std::string& value) {
        if (value.empty()) {
            return;   // 空の欄は出さない。意味のない行を並べない。
        }
        if (!text.empty()) {
            text += "\n";
        }
        text += label + "：" + value;
    };
    if (!targetNameJa.empty()) {
        line("対象立体", targetNameJa);
    }
    if (!profileNamesJa.empty()) {
        std::string joined;
        for (const std::string& name : profileNamesJa) {
            if (!joined.empty()) {
                joined += "、";
            }
            joined += name;
        }
        line(plan.profileIsFace ? "面" : "輪郭", joined);
    }
    if (plan.readyToPreview) {
        line("操作", std::string(ExtrudeBooleanNameJa(operation)));
    }
    if (!plan.needsJa.empty()) {
        if (!text.empty()) {
            text += "\n";
        }
        text += plan.needsJa;
    }
    return text;
}

} // namespace kachakacha::v2::app
