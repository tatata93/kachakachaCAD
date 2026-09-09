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
        needs.edges = 1;
        needs.points = 1;
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
    static const std::vector<WorkPlaneMethod> methods{WorkPlaneMethod::Standard,
        WorkPlaneMethod::OffsetFromPlane, WorkPlaneMethod::ThroughPointParallel,
        WorkPlaneMethod::MidBetweenPlanes, WorkPlaneMethod::ThreePoints,
        WorkPlaneMethod::NormalToCurveAtPoint, WorkPlaneMethod::AngleAboutEdge,
        WorkPlaneMethod::TwoEdges, WorkPlaneMethod::CylinderAxis,
        WorkPlaneMethod::TangentThroughEdge, WorkPlaneMethod::TangentThroughPoint};
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

} // namespace kachakacha::v2::app
