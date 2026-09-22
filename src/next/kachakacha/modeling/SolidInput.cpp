#include "kachakacha/modeling/SolidInput.h"

#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/WireEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace kachakacha::v2::modeling {

using base::MakeError;
using base::Result;
using geometry::Cross;
using geometry::Dot;
using geometry::Normalized;
using geometry::Point2;

namespace {

constexpr double kTwoPi = 6.283185307179586;

[[nodiscard]] std::string Mm(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return buffer;
}

[[nodiscard]] double SamplingToleranceMm(const GeometryTolerance& tolerance)
{
    return std::max(tolerance.modelLinearMm * 10.0, 1.0e-5);
}

//! 輪郭の読み方は押し出しと同じ(閉じているか・同じ平面か・外周と穴)。
[[nodiscard]] Result<ExtrudeAnalysis> AnalyzeProfiles(const std::vector<ExtrudeProfile>& profiles,
    const GeometryTolerance& tolerance)
{
    ExtrudeRequest request;
    request.profiles = profiles;
    request.directionMode = ExtrudeDirectionMode::ProfileNormal;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 1.0;
    request.outputs.part = true;
    return AnalyzeExtrudeRequest(request, tolerance);
}

//! 平面上の多角形の面積と重心(符号つき面積で重みを付ける)。
struct AreaMoment {
    double area = 0.0;
    Point2 centroid{};
};

[[nodiscard]] AreaMoment PolygonMoment(const std::vector<Point2>& loop)
{
    AreaMoment moment;
    double twiceArea = 0.0;
    double cu = 0.0;
    double cv = 0.0;
    for (std::size_t index = 0; index < loop.size(); ++index) {
        const Point2& a = loop[index];
        const Point2& b = loop[(index + 1) % loop.size()];
        const double cross = a.u * b.v - b.u * a.v;
        twiceArea += cross;
        cu += (a.u + b.u) * cross;
        cv += (a.v + b.v) * cross;
    }
    moment.area = std::abs(twiceArea) * 0.5;
    if (std::abs(twiceArea) > 1.0e-18) {
        moment.centroid = Point2{cu / (3.0 * twiceArea), cv / (3.0 * twiceArea)};
    }
    return moment;
}

//! 軸から、平面の中で直交する向き(輪郭がどちら側にあるかを測る物差し)。
[[nodiscard]] Vector3 InPlanePerpendicular(const Vector3& planeNormal, const Vector3& axis)
{
    return Normalized(Cross(planeNormal, axis));
}

//! 回転軸が輪郭の平面に載り、輪郭の内側を通っていないか。
[[nodiscard]] std::vector<Diagnostic> CheckAxis(const std::vector<ExtrudeProfile>& profiles,
    const geometry::PlaneFit& plane, const Vector3& axisPoint, const Vector3& axis,
    const GeometryTolerance& tolerance)
{
    const double join = std::max(tolerance.interactiveJoinMm, tolerance.modelLinearMm);
    if (std::abs(Dot(axis, plane.normal)) > 1.0e-6
        || std::abs(Dot(axisPoint - plane.origin, plane.normal)) > join) {
        return {MakeError(kSolidAxisOffPlane, "回転軸が輪郭の平面に載っていません。",
            "輪郭と同じ作業平面の上に、軸にする直線を引いてください。")};
    }
    const Vector3 across = InPlanePerpendicular(plane.normal, axis);
    double lowest = 0.0;
    double highest = 0.0;
    bool first = true;
    for (const auto& profile : profiles) {
        for (const Vector3& point :
            geometry::SampleChain(profile.segments, SamplingToleranceMm(tolerance))) {
            const double side = Dot(point - axisPoint, across);
            lowest = first ? side : std::min(lowest, side);
            highest = first ? side : std::max(highest, side);
            first = false;
        }
    }
    if (lowest < -join && highest > join) {
        return {MakeError(kSolidAxisCrossesProfile, "回転軸が輪郭の内側を通っています。",
            "回すと輪郭が自分と重なります。軸は輪郭の外側か、輪郭の縁に沿わせてください"
            "(片側 " + Mm(-lowest) + " mm、反対側 " + Mm(highest) + " mm)。")};
    }
    if (std::max(-lowest, highest) <= join) {
        return {MakeError(kSolidBadInput, "輪郭が回転軸の上に載っているだけで、回しても厚みが出ません。",
            {})};
    }
    return {};
}

//! Pappus: 体積 = 角度 × Σ(面積 × 重心から軸までの距離)。穴は引く。
[[nodiscard]] double PappusVolume(const std::vector<ExtrudeProfile>& profiles,
    const ExtrudeAnalysis& profile, const Vector3& axisPoint, const Vector3& axis,
    double angleRad, const GeometryTolerance& tolerance)
{
    const geometry::PlanarFrame frame = geometry::MakeFrame(profile.profilePlane);
    const Vector3 across = InPlanePerpendicular(profile.profilePlane.normal, axis);
    const auto momentOf = [&](std::size_t index) {
        auto points = geometry::SampleChain(profiles[index].segments, SamplingToleranceMm(tolerance));
        geometry::RemoveClosingDuplicate(points, tolerance.modelLinearMm);
        const AreaMoment moment = PolygonMoment(geometry::ProjectToFrame(points, frame));
        const Vector3 centroid = frame.origin + frame.uDirection * moment.centroid.u
            + frame.vDirection * moment.centroid.v;
        return moment.area * std::abs(Dot(centroid - axisPoint, across));
    };
    double firstMoment = 0.0;
    for (const ExtrudeLoop& loop : profile.loops) {
        if (loop.isHole) {
            continue;
        }
        firstMoment += momentOf(loop.profileIndex);
        for (const std::size_t hole : loop.holes) {
            firstMoment -= momentOf(hole);
        }
    }
    return angleRad * firstMoment;
}

//! 経路の線を 1 本につなぐ(順と向きは検査が決める)。枝分かれ・切れ目は断る。
[[nodiscard]] Result<std::vector<CurveSegment>> ChainPath(const std::vector<CurveSegment>& path,
    const GeometryTolerance& tolerance, bool& closed)
{
    using Out = Result<std::vector<CurveSegment>>;
    std::vector<geometry::ChainInput> inputs;
    base::DeterministicIdGenerator ids{77};
    for (const CurveSegment& segment : path) {
        inputs.push_back(geometry::ChainInput{ids.NextTyped<base::IdKind::Entity>(),
            ids.NextTyped<base::IdKind::Segment>(), segment});
    }
    const auto chain = geometry::AnalyzeChain(inputs, tolerance);
    if (!chain.HasValue()) {
        return Out::Failure(MakeError(kSolidPathBroken, "スイープの経路が 1 本につながっていません。",
            chain.FirstSummaryJa() + " 経路は、端どうしがつながった 1 本の線にしてください。"));
    }
    std::vector<CurveSegment> ordered;
    for (const auto& oriented : chain.Value().order.segments) {
        for (const auto& input : inputs) {
            if (input.segmentId == oriented.segmentId) {
                if (!oriented.reversed) {
                    ordered.push_back(input.segment);
                    break;
                }
                const auto reversed = geometry::ReverseCurve(input.segment);
                if (!reversed.HasValue()) {
                    return Out::Failure(reversed.Diagnostics());
                }
                ordered.push_back(reversed.Value());
                break;
            }
        }
    }
    closed = chain.Value().order.closed;
    return Out::Success(std::move(ordered));
}

[[nodiscard]] Result<std::vector<CurveSegment>> ReversedChain(const std::vector<CurveSegment>& chain)
{
    std::vector<CurveSegment> reversed;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const auto one = geometry::ReverseCurve(*it);
        if (!one.HasValue()) {
            return Result<std::vector<CurveSegment>>::Failure(one.Diagnostics());
        }
        reversed.push_back(one.Value());
    }
    return Result<std::vector<CurveSegment>>::Success(std::move(reversed));
}

} // namespace

std::string_view SolidMethodNameJa(SolidMethod method) noexcept
{
    switch (method) {
    case SolidMethod::Revolve: return "回転体";
    case SolidMethod::Loft:    return "ロフト立体";
    case SolidMethod::Sweep:   return "スイープ";
    }
    return "回転体";
}

Result<RevolveSolidAnalysis> AnalyzeRevolveSolid(const RevolveSolidRequest& request,
    const GeometryTolerance& tolerance)
{
    using Out = Result<RevolveSolidAnalysis>;
    if (request.profiles.empty()) {
        return Out::Failure(MakeError(kSolidBadInput, "回す輪郭がありません。",
            "閉じた輪郭(外周と穴)を 1 つ以上選んでください。"));
    }
    if (!std::isfinite(request.angleRad) || request.angleRad <= 1.0e-9
        || request.angleRad > kTwoPi + 1.0e-9) {
        return Out::Failure(MakeError(kSolidBadAngle, "回す角度は 0° より大きく 360° 以下にしてください。",
            {}));
    }
    const Vector3 axis = Normalized(request.axisDirection);
    if (!request.axisPoint.IsFinite() || axis == Vector3{}) {
        return Out::Failure(MakeError(kSolidBadInput, "回転軸の向きが決まっていません。",
            "軸にする直線を 1 本選んでください。"));
    }
    auto profile = AnalyzeProfiles(request.profiles, tolerance);
    if (!profile.HasValue()) {
        return Out::Failure(profile.Diagnostics());
    }
    const auto axisProblems = CheckAxis(request.profiles, profile.Value().profilePlane,
        request.axisPoint, axis, tolerance);
    if (!axisProblems.empty()) {
        return Out::Failure(axisProblems);
    }
    RevolveSolidAnalysis analysis;
    analysis.profile = std::move(profile).Value();
    analysis.axisPoint = request.axisPoint;
    analysis.axisDirection = axis;
    analysis.angleRad = std::min(request.angleRad, kTwoPi);
    analysis.startAngleRad = request.symmetric ? -analysis.angleRad * 0.5 : 0.0;
    analysis.predictedVolumeMm3 = PappusVolume(request.profiles, analysis.profile,
        request.axisPoint, axis, analysis.angleRad, tolerance);
    analysis.volumeIsApproximate = true;
    return Out::Success(std::move(analysis));
}

Result<LoftSolidAnalysis> AnalyzeLoftSolid(const LoftSolidRequest& request,
    const GeometryTolerance& tolerance)
{
    using Out = Result<LoftSolidAnalysis>;
    if (request.sections.size() < 2) {
        return Out::Failure(MakeError(kSolidBadInput, "ロフト立体には閉じた断面が 2 つ以上要ります。",
            "いまは " + std::to_string(request.sections.size()) + " つです。"));
    }
    LoftSolidAnalysis analysis;
    std::vector<Vector3> centroids;
    const double join = std::max(tolerance.interactiveJoinMm, tolerance.modelLinearMm);
    for (std::size_t index = 0; index < request.sections.size(); ++index) {
        const auto& section = request.sections[index];
        const std::string label = std::to_string(index + 1) + " 番目の断面";
        if (section.segments.empty() || !geometry::SegmentsFormClosedLoop(section.segments, tolerance)) {
            return Out::Failure(MakeError(kSolidBadInput, label + "が閉じていません。",
                "ロフト立体の断面は、閉じた線にしてください(開いた断面は面のロフトで)。"));
        }
        auto points = geometry::SampleChain(section.segments, SamplingToleranceMm(tolerance));
        const geometry::PlaneFit plane = geometry::FitPlane(points);
        const bool end = index == 0 || index + 1 == request.sections.size();
        if (end && (!plane.valid || plane.maximumDeviationMm > join)) {
            return Out::Failure(MakeError(kSolidNotPlanar,
                label + "が平面に載っていないので、端の蓋ができません。",
                "両端の断面は平らな閉じた線にしてください(平面から " + Mm(plane.maximumDeviationMm)
                    + " mm 離れています)。"));
        }
        analysis.planes.push_back(plane);
        centroids.push_back(geometry::Centroid(points));
    }
    for (std::size_t index = 1; index < centroids.size(); ++index) {
        if (geometry::Distance(centroids[index - 1], centroids[index]) <= join
            && std::abs(Dot(analysis.planes[index].origin - analysis.planes[index - 1].origin,
                            analysis.planes[index - 1].normal)) <= join) {
            return Out::Failure(MakeError(kSolidSectionsOverlap,
                std::to_string(index) + " 番目と " + std::to_string(index + 1)
                    + " 番目の断面が重なっていて、立体になりません。",
                {}));
        }
    }
    return Out::Success(std::move(analysis));
}

Result<SweepSolidAnalysis> AnalyzeSweepSolid(const SweepSolidRequest& request,
    const GeometryTolerance& tolerance)
{
    using Out = Result<SweepSolidAnalysis>;
    if (request.profiles.empty()) {
        return Out::Failure(MakeError(kSolidBadInput, "掃く輪郭がありません。",
            "閉じた輪郭を 1 つ以上選んでください。"));
    }
    if (request.path.empty()) {
        return Out::Failure(MakeError(kSolidBadInput, "スイープの経路がありません。",
            "輪郭を運ぶ線(経路)を選んでください。"));
    }
    auto profile = AnalyzeProfiles(request.profiles, tolerance);
    if (!profile.HasValue()) {
        return Out::Failure(profile.Diagnostics());
    }
    bool closed = false;
    auto chain = ChainPath(request.path, tolerance, closed);
    if (!chain.HasValue()) {
        return Out::Failure(chain.Diagnostics());
    }
    const geometry::PlaneFit& plane = profile.Value().profilePlane;
    const double reach = std::max(0.1, tolerance.interactiveJoinMm);
    const auto onPlane = [&](const Vector3& point) {
        return std::abs(Dot(point - plane.origin, plane.normal)) <= reach;
    };
    std::vector<CurveSegment> ordered = std::move(chain).Value();
    if (!onPlane(ordered.front().StartPoint())) {
        if (!onPlane(ordered.back().EndPoint())) {
            return Out::Failure(MakeError(kSolidPathOffProfile, "スイープの経路が輪郭の平面から始まっていません。",
                "経路の端を、輪郭と同じ作業平面の上(輪郭の中心など)に置いてください。"));
        }
        auto reversed = ReversedChain(ordered);
        if (!reversed.HasValue()) {
            return Out::Failure(reversed.Diagnostics());
        }
        ordered = std::move(reversed).Value();
    }
    const Vector3 tangent = Normalized(ordered.front().FirstDerivative(0.0));
    if (tangent == Vector3{} || std::abs(Dot(tangent, plane.normal)) < 0.17) {
        return Out::Failure(MakeError(kSolidPathOffProfile, "スイープの経路が輪郭の面に沿っています。",
            "経路は輪郭の面から離れる向き(面にほぼ垂直)に引いてください。"));
    }
    SweepSolidAnalysis analysis;
    analysis.profile = std::move(profile).Value();
    analysis.orderedPath = std::move(ordered);
    analysis.pathClosed = closed;
    return Out::Success(std::move(analysis));
}

bool RevolveVolumeMatches(const RevolveSolidAnalysis& analysis, double actualVolumeMm3)
{
    const double expected = analysis.predictedVolumeMm3;
    if (!(expected > 0.0) || !std::isfinite(actualVolumeMm3)) {
        return false;
    }
    const double limit = analysis.volumeIsApproximate ? 5.0e-3 : 1.0e-6;
    return std::abs(actualVolumeMm3 - expected) / expected <= limit;
}

} // namespace kachakacha::v2::modeling
