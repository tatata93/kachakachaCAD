#include "kachakacha/app/RevolveSurface.h"

#include "kachakacha/geometry/Units.h"
#include "kachakacha/geometry/WireEdit.h"

#include <cmath>
#include <string>

namespace kachakacha::v2::app {

using base::MakeError;
using base::Result;
using geometry::CurveSegment;
using geometry::Vector3;

namespace {

constexpr const char* kNoProfile = "REV-E001";
constexpr const char* kBadAxis = "REV-E002";
constexpr const char* kBadAngle = "REV-E003";
constexpr const char* kBadSections = "REV-E004";
constexpr const char* kOnAxis = "REV-E005";
constexpr int kMinSections = 2;
constexpr int kMaxSections = 72;

//! 点から軸までの距離。
[[nodiscard]] double DistanceToAxis(const Vector3& point, const Vector3& axisPoint,
    const Vector3& axisUnit)
{
    const Vector3 delta = point - axisPoint;
    return (delta - axisUnit * geometry::Dot(delta, axisUnit)).Length();
}

} // namespace

Result<RevolveRequest> MakeRevolveRequest(const std::vector<CurveSegment>& profile,
    const std::vector<CurveSegment>& axis, double angleDeg, int sections)
{
    using Out = Result<RevolveRequest>;
    if (profile.empty()) {
        return Out::Failure(MakeError(kNoProfile, "断面の線がありません。",
            "回す断面の線を 1 本目に、軸の直線を 2 本目に選んでください。"));
    }
    if (axis.size() != 1 || axis.front().Kind() != geometry::CurveKind::Line) {
        return Out::Failure(MakeError(kBadAxis, "軸は直線 1 本にしてください。",
            axis.empty() ? std::string("軸の線が選ばれていません。")
                         : "選んだ軸は " + std::to_string(axis.size()) + " 本("
                               + std::string(geometry::CurveKindName(axis.front().Kind()))
                               + ")です。"));
    }
    if (!(angleDeg > 0.0) || !(angleDeg <= 360.0) || !std::isfinite(angleDeg)) {
        return Out::Failure(MakeError(kBadAngle,
            "回す角度は 0 より大きく 360 以下にしてください。",
            std::to_string(angleDeg) + " 度"));
    }
    if (sections < kMinSections || sections > kMaxSections) {
        return Out::Failure(MakeError(kBadSections, "断面の数は 2〜72 にしてください。",
            std::to_string(sections) + " 個"));
    }
    RevolveRequest request;
    request.profile = profile;
    request.axisPoint = axis.front().StartPoint();
    request.axisDirection = geometry::Normalized(axis.front().EndPoint()
        - axis.front().StartPoint());
    if (request.axisDirection == Vector3{}) {
        return Out::Failure(MakeError(kBadAxis, "軸は直線 1 本にしてください。",
            "軸の線の長さが 0 です。"));
    }
    request.angleDeg = angleDeg;
    request.sections = sections;
    return Out::Success(std::move(request));
}

Result<std::vector<RevolvedSection>> BuildRevolvedSections(const RevolveRequest& request,
    double toleranceMm)
{
    using Out = Result<std::vector<RevolvedSection>>;
    if (request.profile.empty()) {
        return Out::Failure(MakeError(kNoProfile, "断面の線がありません。", {}));
    }
    const Vector3 axisUnit = geometry::Normalized(request.axisDirection);
    if (axisUnit == Vector3{}) {
        return Out::Failure(MakeError(kBadAxis, "軸は直線 1 本にしてください。",
            "軸の向きが決まりません。"));
    }
    // 断面が軸の上に乗っていれば、回しても同じ線が重なるだけで面にならない。
    // 端と中点を見る(円弧やベジエは端だけでは軸上に見えることがある)。
    double farthest = 0.0;
    for (const auto& segment : request.profile) {
        for (const double t : {0.0, 0.5, 1.0}) {
            farthest = std::max(farthest,
                DistanceToAxis(segment.Evaluate(t), request.axisPoint, axisUnit));
        }
    }
    if (farthest <= toleranceMm) {
        return Out::Failure(MakeError(kOnAxis, "断面が軸の上にあります(回しても面になりません)。",
            "断面を軸から離すか、別の線を軸にしてください。"));
    }
    std::vector<RevolvedSection> sections;
    const double angleRad = request.angleDeg * geometry::kPi / 180.0;
    for (int index = 1; index <= request.sections; ++index) {
        RevolvedSection section;
        section.angleRad = angleRad * static_cast<double>(index)
            / static_cast<double>(request.sections);
        for (const auto& segment : request.profile) {
            auto rotated = geometry::RotateCurve(segment, request.axisPoint, axisUnit,
                section.angleRad);
            if (!rotated.HasValue()) {
                return Out::Failure(rotated.Diagnostics());
            }
            section.segments.push_back(rotated.Value());
        }
        sections.push_back(std::move(section));
    }
    return Out::Success(std::move(sections));
}

} // namespace kachakacha::v2::app
