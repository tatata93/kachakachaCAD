#include "kachakacha/app/FacePushPull.h"

#include <cmath>
#include <sstream>

namespace kachakacha::v2::app {

FacePushPullPlan PlanFacePushPull(double signedDistanceMm, double minimumDistanceMm)
{
    FacePushPullPlan plan;
    if (!std::isfinite(signedDistanceMm)) {
        plan.messageJa = "押し引きの量が数になっていません。";
        return plan;
    }
    const double magnitude = std::abs(signedDistanceMm);
    if (magnitude <= minimumDistanceMm) {
        plan.messageJa = "面を外へ引くと材料が増え、中へ押すと材料が減ります。"
                         "矢印を引くか、距離を打ってください。";
        return plan;
    }
    plan.distanceMm = magnitude;
    plan.reversed = signedDistanceMm < 0.0;
    plan.booleanMode = plan.reversed ? modeling::ExtrudeBooleanMode::SubtractFromPart
                                     : modeling::ExtrudeBooleanMode::AddToPart;
    plan.ready = true;
    plan.messageJa = DescribeFacePushPullJa(plan);
    return plan;
}

std::string DescribeFacePushPullJa(const FacePushPullPlan& plan)
{
    if (!plan.ready) {
        return plan.messageJa;
    }
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(2);
    text << (plan.reversed ? "面を中へ " : "面を外へ ") << plan.distanceMm << " mm "
         << (plan.reversed ? "押します(材料が減ります)。" : "引きます(材料が増えます)。");
    return text.str();
}

} // namespace kachakacha::v2::app
