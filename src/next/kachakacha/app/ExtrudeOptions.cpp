#include "kachakacha/app/ExtrudeOptions.h"

#include <cmath>

namespace kachakacha::v2::app {
namespace {

using base::MakeError;
using base::Result;
using modeling::ExtrudeBooleanMode;
using modeling::ExtrudeDirectionMode;
using modeling::ExtrudeExtentMode;

[[nodiscard]] Result<ExtrudeChoice> Refuse(const char* code, const char* summaryJa,
    const char* detailJa)
{
    return Result<ExtrudeChoice>::Failure({MakeError(code, summaryJa, detailJa)});
}

[[nodiscard]] std::string Rounded(double value)
{
    const double snapped = std::round(value * 1000.0) / 1000.0;
    std::string text = std::to_string(snapped);
    while (text.size() > 1 && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

} // namespace

std::string_view ExtrudeDirectionNameJa(ExtrudeDirectionMode mode) noexcept
{
    switch (mode) {
    case ExtrudeDirectionMode::ProfileNormal:   return "輪郭に垂直";
    case ExtrudeDirectionMode::WorkPlaneNormal: return "作業平面に垂直";
    case ExtrudeDirectionMode::WorldX:          return "X方向";
    case ExtrudeDirectionMode::WorldY:          return "Y方向";
    case ExtrudeDirectionMode::WorldZ:          return "Z方向";
    case ExtrudeDirectionMode::SelectedVector:  return "選んだ線の向き";
    case ExtrudeDirectionMode::CustomXYZ:       return "数値で決める";
    }
    return "不明";
}

std::string_view ExtrudeExtentNameJa(ExtrudeExtentMode mode) noexcept
{
    switch (mode) {
    case ExtrudeExtentMode::Distance:          return "距離";
    case ExtrudeExtentMode::SymmetricDistance: return "左右対称";
    case ExtrudeExtentMode::TwoDistances:      return "両方向に別々の距離";
    case ExtrudeExtentMode::ToTarget:          return "選んだ面まで";
    case ExtrudeExtentMode::ThroughAll:        return "全部貫く";
    }
    return "不明";
}

std::string_view ExtrudeBooleanNameJa(ExtrudeBooleanMode mode) noexcept
{
    switch (mode) {
    case ExtrudeBooleanMode::NewPart:          return "新しい部品";
    case ExtrudeBooleanMode::AddToPart:        return "選んだ部品へ足す";
    case ExtrudeBooleanMode::SubtractFromPart: return "選んだ部品から引く";
    }
    return "不明";
}

const std::vector<ExtrudeDirectionMode>& ExtrudeDirections()
{
    static const std::vector<ExtrudeDirectionMode> modes{
        ExtrudeDirectionMode::WorkPlaneNormal, ExtrudeDirectionMode::ProfileNormal,
        ExtrudeDirectionMode::WorldX, ExtrudeDirectionMode::WorldY,
        ExtrudeDirectionMode::WorldZ, ExtrudeDirectionMode::SelectedVector,
        ExtrudeDirectionMode::CustomXYZ};
    return modes;
}

const std::vector<ExtrudeExtentMode>& ExtrudeExtents()
{
    static const std::vector<ExtrudeExtentMode> modes{ExtrudeExtentMode::Distance,
        ExtrudeExtentMode::SymmetricDistance, ExtrudeExtentMode::TwoDistances,
        ExtrudeExtentMode::ToTarget, ExtrudeExtentMode::ThroughAll};
    return modes;
}

const std::vector<ExtrudeBooleanMode>& ExtrudeBooleans()
{
    static const std::vector<ExtrudeBooleanMode> modes{ExtrudeBooleanMode::NewPart,
        ExtrudeBooleanMode::AddToPart, ExtrudeBooleanMode::SubtractFromPart};
    return modes;
}

bool ExtentUsesDistance(ExtrudeExtentMode mode) noexcept
{
    return mode == ExtrudeExtentMode::Distance
        || mode == ExtrudeExtentMode::SymmetricDistance
        || mode == ExtrudeExtentMode::TwoDistances;
}

bool ExtentUsesSecondDistance(ExtrudeExtentMode mode) noexcept
{
    return mode == ExtrudeExtentMode::TwoDistances;
}

bool ExtentUsesTarget(ExtrudeExtentMode mode) noexcept
{
    return mode == ExtrudeExtentMode::ToTarget;
}

Result<ExtrudeChoice> ValidateExtrudeChoice(const ExtrudeChoice& choice,
    const ExtrudeFacts& facts)
{
    if (!choice.makePart && !choice.makeEndProfileWire && !choice.makeSideBoundaryWires) {
        return Refuse("EXT-U001", "何を作るかが決まっていません。",
            "部品・押し出し先の輪郭・側面の境界のうち、少なくとも1つを選んでください。");
    }
    if (choice.makePart && facts.closedProfiles == 0) {
        // 開いた輪郭からは立体にならない。ワイヤーだけなら開いていてもよい。
        return Refuse("EXT-U002", "部品を作るには、閉じた輪郭が要ります。",
            "選んだ輪郭が閉じていません。閉じた輪郭を選ぶか、"
            "作るものを「ワイヤーだけ」にしてください。");
    }
    if (ExtentUsesTarget(choice.extent) && !choice.targetEntityId.has_value()) {
        return Refuse("EXT-U003", "届かせる相手が決まっていません。",
            "「選んだ面まで」にしたときは、相手の作業平面も選んでください。");
    }
    if (choice.booleanMode != ExtrudeBooleanMode::NewPart && !choice.hasSelectedPart) {
        // 近い部品を勝手に選ばない。どれに足したのか分からなくなる。
        return Refuse("EXT-U004", "足す・引く相手の部品が選ばれていません。",
            "相手の部品を選んでから、もう一度押してください。");
    }
    if (ExtentUsesDistance(choice.extent) && !(choice.distanceMm > 0.0)) {
        if (choice.makePart || !choice.zeroDistanceConfirmed) {
            return Refuse("EXT-U005", "押し出す距離が0以下です。",
                "距離を正の数にしてください。"
                "距離0のままワイヤーだけ作るときは、その旨を確かめてから行います。");
        }
    }
    if (choice.extent == ExtrudeExtentMode::ThroughAll
        && choice.booleanMode != ExtrudeBooleanMode::SubtractFromPart) {
        // 貫く先が無ければ、どこまで押すのか決まらない。
        return Refuse("EXT-U006", "「全部貫く」は引くときだけ使えます。",
            "貫く相手が無いと、どこまで押すのかが決まりません。");
    }
    return Result<ExtrudeChoice>::Success(choice);
}

std::string ExtrudeSummaryJa(const ExtrudeChoice& choice)
{
    std::string text(ExtrudeDirectionNameJa(choice.direction));
    if (choice.reversed) {
        text += "(逆向き)";
    }
    text += " / ";
    text += ExtrudeExtentNameJa(choice.extent);
    if (ExtentUsesDistance(choice.extent)) {
        text += " " + Rounded(choice.distanceMm) + "mm";
        if (ExtentUsesSecondDistance(choice.extent)) {
            text += " と " + Rounded(choice.secondDistanceMm) + "mm";
        }
    }
    text += " / 作るもの:";
    if (choice.makePart) {
        text += "部品";
    }
    if (choice.makeEndProfileWire) {
        text += choice.makePart ? "・先の輪郭" : "先の輪郭";
    }
    if (choice.makeSideBoundaryWires) {
        text += (choice.makePart || choice.makeEndProfileWire) ? "・側面" : "側面";
    }
    if (choice.booleanMode != ExtrudeBooleanMode::NewPart) {
        text += " / ";
        text += ExtrudeBooleanNameJa(choice.booleanMode);
    }
    return text;
}

modeling::ExtrudeRequest ToExtrudeRequest(const ExtrudeChoice& choice,
    std::vector<modeling::ExtrudeProfile> profiles,
    const modeling::WorkPlaneFrame& workPlane,
    const std::optional<modeling::WorkPlaneFrame>& targetPlane)
{
    modeling::ExtrudeRequest request;
    request.profiles = std::move(profiles);
    request.directionMode = choice.direction;
    request.customDirection = choice.customDirection;
    request.workPlane = workPlane;
    request.reversed = choice.reversed;
    request.extent = choice.extent;
    request.distanceMm = choice.distanceMm;
    request.secondDistanceMm = choice.secondDistanceMm;
    if (ExtentUsesTarget(choice.extent) && targetPlane.has_value()) {
        request.targetKind = modeling::ExtrudeTargetKind::Plane;
        request.targetPlane = *targetPlane;
    }
    request.outputs.part = choice.makePart;
    request.outputs.endProfileWire = choice.makeEndProfileWire;
    request.outputs.sideBoundaryWires = choice.makeSideBoundaryWires;
    request.booleanMode = choice.booleanMode;
    request.hasSelectedPart = choice.hasSelectedPart;
    request.zeroDistanceConfirmed = choice.zeroDistanceConfirmed;
    return request;
}

} // namespace kachakacha::v2::app
