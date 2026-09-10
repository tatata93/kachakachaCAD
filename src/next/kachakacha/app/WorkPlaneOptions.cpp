#include "kachakacha/app/WorkPlaneOptions.h"

namespace kachakacha::v2::app {
namespace {

using base::MakeError;
using base::Result;
using modeling::WorkPlaneMethod;

} // namespace

WorkPlaneNeeds NeedsOf(WorkPlaneMethod method) noexcept
{
    WorkPlaneNeeds needs;
    switch (method) {
    case WorkPlaneMethod::Standard:
        needs.usesStandardKind = true;
        break;
    case WorkPlaneMethod::OffsetFromPlane:
        needs.planes = 1;
        needs.usesOffset = true;
        break;
    case WorkPlaneMethod::ThroughPointParallel:
        needs.planes = 1;
        needs.points = 1;
        break;
    case WorkPlaneMethod::MidBetweenPlanes:
        needs.planes = 2;
        break;
    case WorkPlaneMethod::CylinderAxis:
        needs.edges = 1;
        break;
    case WorkPlaneMethod::AngleAboutEdge:
        needs.planes = 1;
        needs.edges = 1;
        needs.usesAngle = true;
        break;
    case WorkPlaneMethod::ThreePoints:
        needs.points = 3;
        break;
    case WorkPlaneMethod::TwoEdges:
        needs.edges = 2;
        break;
    case WorkPlaneMethod::TangentThroughEdge:
        needs.edges = 1;
        break;
    case WorkPlaneMethod::TangentThroughPoint:
        needs.points = 1;
        break;
    case WorkPlaneMethod::NormalToCurveAtPoint:
        // 位置は線上位置の欄(0〜1)で決める。点を選んでいれば、その最寄りの位置にする。
        needs.edges = 1;
        break;
    case WorkPlaneMethod::PointNormal:
        // 数値だけで作る。選ぶものは無い。
        break;
    }
    return needs;
}

std::string WorkPlaneNeedsJa(WorkPlaneMethod method)
{
    const WorkPlaneNeeds needs = NeedsOf(method);
    std::string text;
    const auto append = [&text](int count, const char* what) {
        if (count <= 0) {
            return;
        }
        if (!text.empty()) {
            text += "と";
        }
        text += what;
        text += std::to_string(count);
        text += "つ";
    };
    append(needs.planes, "作業平面を");
    append(needs.points, "点を");
    append(needs.edges, "線を");
    if (text.empty()) {
        return "選ぶものはありません。";
    }
    return text + "選んでください。";
}

const std::vector<WorkPlaneMethod>& WorkPlaneMethods()
{
    // V1 の右パネルと同じ並び: 基準面 → 数値指定 → 3点 → 平行距離 → 傾ける → 点を通る平行面
    // → 2面の中央 → 2直線 → 直角面 → 線上の直角面 → 接平面。V2 だけの2つは末尾。
    static const std::vector<WorkPlaneMethod> methods{WorkPlaneMethod::Standard,
        WorkPlaneMethod::PointNormal, WorkPlaneMethod::ThreePoints,
        WorkPlaneMethod::OffsetFromPlane, WorkPlaneMethod::AngleAboutEdge,
        WorkPlaneMethod::ThroughPointParallel,
        WorkPlaneMethod::MidBetweenPlanes, WorkPlaneMethod::TwoEdges,
        WorkPlaneMethod::NormalToCurveAtPoint, WorkPlaneMethod::TangentThroughPoint,
        WorkPlaneMethod::CylinderAxis, WorkPlaneMethod::TangentThroughEdge};
    return methods;
}

Result<WorkPlaneMethod> ValidateWorkPlaneChoice(WorkPlaneMethod method,
    const WorkPlaneFacts& facts)
{
    const WorkPlaneNeeds needs = NeedsOf(method);
    // 足りないものを黙って補わない。補うと、思っていない平面が出来る。
    if (facts.planes < needs.planes || facts.points < needs.points
        || facts.edges < needs.edges) {
        return Result<WorkPlaneMethod>::Failure({MakeError("UI-W001",
            "選んでいるものが足りません。",
            std::string(modeling::WorkPlaneMethodNameJa(method)) + "には、"
                + WorkPlaneNeedsJa(method))});
    }
    return Result<WorkPlaneMethod>::Success(method);
}

Result<modeling::WorkPlaneRequest> BuildWorkPlaneRequest(const WorkPlaneChoice& choice,
    const WorkPlaneMaterials& materials)
{
    using Out = Result<modeling::WorkPlaneRequest>;
    using modeling::WorkPlaneMethod;
    constexpr double kPi = 3.14159265358979323846;
    modeling::WorkPlaneRequest request;
    request.method = choice.method;
    request.standard = choice.standard;
    request.offsetMm = choice.offsetMm;
    request.angleRad = choice.angleDeg * kPi / 180.0;
    request.origin = choice.origin;
    request.normal = choice.normal;
    request.uHint = choice.uAxis;
    request.curveParameter = choice.curveParameter;
    request.points = materials.points;
    request.edges = materials.edges;
    // 基準平面: コンボ > 選択の1つ目。相手の平面: コンボ > 選択の2つ目。
    if (materials.referencePlane.has_value()) {
        request.referencePlane = *materials.referencePlane;
    } else if (!materials.selectedPlanes.empty()) {
        request.referencePlane = materials.selectedPlanes.front();
    }
    if (materials.secondPlane.has_value()) {
        request.secondPlane = *materials.secondPlane;
    } else if (materials.selectedPlanes.size() >= 2) {
        request.secondPlane = materials.selectedPlanes[1];
    }
    const int planes = static_cast<int>(materials.selectedPlanes.size())
        + (materials.referencePlane.has_value() ? 1 : 0)
        + (materials.secondPlane.has_value() ? 1 : 0);
    WorkPlaneFacts facts;
    facts.points = static_cast<int>(materials.points.size());
    facts.edges = static_cast<int>(materials.edges.size());
    facts.planes = planes;
    // 数の欄で代えられるもの。選んでいなければ欄の値を材料にする。
    if (choice.method == WorkPlaneMethod::ThreePoints && facts.points < 3) {
        request.points.assign(choice.threePoints.begin(), choice.threePoints.end());
        facts.points = 3;
    }
    if (choice.method == WorkPlaneMethod::AngleAboutEdge && facts.edges < 1) {
        const auto axis = geometry::CurveSegment::MakeLine(choice.axisPoint,
            choice.axisPoint + choice.axisDirection);
        if (!axis.HasValue()) {
            return Out::Failure(MakeError("UI-W002", "回転軸の向きが決まりません。",
                "軸の向きの長さが 0 です。"));
        }
        request.edges.push_back(axis.Value());
        facts.edges = 1;
    }
    if (choice.method == WorkPlaneMethod::NormalToCurveAtPoint && !materials.points.empty()
        && !materials.edges.empty()) {
        // 点を選んでいれば、線の上のいちばん近い位置を使う。欄の値より選んだ点を優先する。
        request.curveParameter =
            materials.edges.front().ClosestPoint(materials.points.front()).parameter;
    }
    const auto checked = ValidateWorkPlaneChoice(choice.method, facts);
    if (!checked.HasValue()) {
        return Out::Failure(checked.Diagnostics());
    }
    return Out::Success(std::move(request));
}

std::string WorkPlaneDisplayName(const WorkPlaneChoice& choice)
{
    if (!choice.name.empty()) {
        return choice.name;
    }
    if (choice.method == modeling::WorkPlaneMethod::Standard) {
        return std::string(modeling::StandardPlaneNameJa(choice.standard));
    }
    return std::string(modeling::WorkPlaneMethodNameJa(choice.method));
}

} // namespace kachakacha::v2::app
