#include "kachakacha/model/Surface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kachakacha::model {

using geometry::Cross;
using geometry::Dot;
using geometry::Vector3;

namespace {

constexpr double kParameterStep = 1.0e-5;

Vector3 DerivativeU(const Surface& surface, double u, double v)
{
    const double before = std::max(0.0, u - kParameterStep);
    const double after = std::min(1.0, u + kParameterStep);
    return (surface.Evaluate(after, v) - surface.Evaluate(before, v)) / (after - before);
}

Vector3 DerivativeV(const Surface& surface, double u, double v)
{
    const double before = std::max(0.0, v - kParameterStep);
    const double after = std::min(1.0, v + kParameterStep);
    return (surface.Evaluate(u, after) - surface.Evaluate(u, before)) / (after - before);
}

std::optional<SurfaceProjection> RefineProjection(
    const Surface& surface,
    Vector3 sourcePoint,
    Vector3 direction,
    double initialU,
    double initialV,
    double tolerance)
{
    double u = initialU;
    double v = initialV;
    double distance = Dot(surface.Evaluate(u, v) - sourcePoint, direction) / direction.LengthSquared();
    for (int iteration = 0; iteration < 32; ++iteration) {
        const Vector3 surfacePoint = surface.Evaluate(u, v);
        const Vector3 residual = surfacePoint - sourcePoint - direction * distance;
        if (residual.Length() <= tolerance) {
            if (u >= -1.0e-7 && u <= 1.0 + 1.0e-7 && v >= -1.0e-7 && v <= 1.0 + 1.0e-7) {
                const double clampedU = std::clamp(u, 0.0, 1.0);
                const double clampedV = std::clamp(v, 0.0, 1.0);
                return SurfaceProjection{surface.Evaluate(clampedU, clampedV), clampedU, clampedV, distance};
            }
            return std::nullopt;
        }

        const Vector3 uDerivative = DerivativeU(surface, u, v);
        const Vector3 vDerivative = DerivativeV(surface, u, v);
        const Vector3 distanceDerivative = direction * -1.0;
        const double determinant = Dot(uDerivative, Cross(vDerivative, distanceDerivative));
        if (std::abs(determinant) <= 1.0e-13) {
            return std::nullopt;
        }
        const Vector3 rightHandSide = residual * -1.0;
        double deltaU = Dot(rightHandSide, Cross(vDerivative, distanceDerivative)) / determinant;
        double deltaV = Dot(uDerivative, Cross(rightHandSide, distanceDerivative)) / determinant;
        double deltaDistance = Dot(uDerivative, Cross(vDerivative, rightHandSide)) / determinant;
        deltaU = std::clamp(deltaU, -0.25, 0.25);
        deltaV = std::clamp(deltaV, -0.25, 0.25);
        deltaDistance = std::clamp(deltaDistance, -1000.0, 1000.0);
        u += deltaU;
        v += deltaV;
        distance += deltaDistance;
        if (u < -0.5 || u > 1.5 || v < -0.5 || v > 1.5 || !std::isfinite(distance)) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

int ProjectionSampleCount(const Wire& wire, int requested)
{
    const int minimum = wire.Kind() == WireKind::Line ? 24
        : wire.Kind() == WireKind::Polyline ? static_cast<int>((wire.ControlPoints().size() - 1) * 16)
        : wire.Kind() == WireKind::Circle ? 192
                                         : 96;
    return std::clamp(std::max(requested, minimum), 8, 2048);
}

std::vector<Wire> PrepareSections(std::vector<Wire> sections, std::size_t requiredCount)
{
    if (sections.size() < requiredCount) {
        throw std::invalid_argument(requiredCount == 2
                ? "Ruled surface requires two sections."
                : "Loft surface requires at least three sections.");
    }
    const bool closed = sections.front().IsClosed();
    for (std::size_t index = 1; index < sections.size(); ++index) {
        if (sections[index].IsClosed() != closed) {
            throw std::invalid_argument("Surface sections must all be open or all be closed.");
        }
        const double sameDirection = (sections[index - 1].Start() - sections[index].Start()).LengthSquared()
            + (sections[index - 1].End() - sections[index].End()).LengthSquared();
        const double reversedDirection = (sections[index - 1].Start() - sections[index].End()).LengthSquared()
            + (sections[index - 1].End() - sections[index].Start()).LengthSquared();
        if (reversedDirection + 1.0e-12 < sameDirection) {
            sections[index] = sections[index].Reversed();
        }

        double separation = 0.0;
        for (int sample = 0; sample <= 32; ++sample) {
            const double u = static_cast<double>(sample) / 32.0;
            separation = std::max(separation, (sections[index - 1].Evaluate(u) - sections[index].Evaluate(u)).Length());
        }
        if (separation <= 1.0e-8) {
            throw std::invalid_argument("Adjacent surface sections must be separated.");
        }
    }
    return sections;
}

Vector3 EvaluateSmoothLoft(
    const std::vector<Wire>& sections,
    double u,
    double v)
{
    const double scaledV = v * static_cast<double>(sections.size() - 1);
    const std::size_t segment = static_cast<std::size_t>(std::min(
        scaledV,
        static_cast<double>(sections.size() - 2)));
    const double t = scaledV - static_cast<double>(segment);
    const Vector3 p1 = sections[segment].Evaluate(u);
    const Vector3 p2 = sections[segment + 1].Evaluate(u);
    const Vector3 p0 = segment > 0
        ? sections[segment - 1].Evaluate(u)
        : p1 * 2.0 - p2;
    const Vector3 p3 = segment + 2 < sections.size()
        ? sections[segment + 2].Evaluate(u)
        : p2 * 2.0 - p1;
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (p1 * 2.0
        + (p2 - p0) * t
        + (p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3) * t2
        + (-p0 + p1 * 3.0 - p2 * 3.0 + p3) * t3) * 0.5;
}

//! 一様Catmull-Rom補間。EvaluateSmoothLoftと同じ端点処理(鏡映ゴースト点)で、
//! 与えた点列 values[i] を t = i/(n-1) で厳密に通る。
Vector3 CatmullRomPoints(const std::vector<Vector3>& values, double t)
{
    if (values.size() == 1) {
        return values.front();
    }
    const double scaled = std::clamp(t, 0.0, 1.0) * static_cast<double>(values.size() - 1);
    const std::size_t segment = static_cast<std::size_t>(std::min(
        scaled,
        static_cast<double>(values.size() - 2)));
    const double local = scaled - static_cast<double>(segment);
    const Vector3 p1 = values[segment];
    const Vector3 p2 = values[segment + 1];
    const Vector3 p0 = segment > 0 ? values[segment - 1] : p1 * 2.0 - p2;
    const Vector3 p3 = segment + 2 < values.size() ? values[segment + 2] : p2 * 2.0 - p1;
    const double local2 = local * local;
    const double local3 = local2 * local;
    return (p1 * 2.0
        + (p2 - p0) * local
        + (p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3) * local2
        + (-p0 + p1 * 3.0 - p2 * 3.0 + p3) * local3) * 0.5;
}

//! 非一様ノット上のカーディナル補間(有限差分接線の3次Hermite)。
//! knots[k] で values[k] を厳密に通る。
Vector3 NonUniformCardinal(
    const std::vector<double>& knots,
    const std::vector<Vector3>& values,
    double u)
{
    const std::size_t count = knots.size();
    if (count == 1) {
        return values.front();
    }
    const double clamped = std::clamp(u, knots.front(), knots.back());
    std::size_t segment = 0;
    while (segment + 2 < count && clamped >= knots[segment + 1]) {
        ++segment;
    }
    const double width = knots[segment + 1] - knots[segment];
    const double s = (clamped - knots[segment]) / width;
    const auto tangent = [&](std::size_t k) {
        if (k == 0) {
            return (values[1] - values[0]) / (knots[1] - knots[0]);
        }
        if (k + 1 == count) {
            return (values[count - 1] - values[count - 2]) / (knots[count - 1] - knots[count - 2]);
        }
        return (values[k + 1] - values[k - 1]) / (knots[k + 1] - knots[k - 1]);
    };
    const Vector3 m0 = tangent(segment) * width;
    const Vector3 m1 = tangent(segment + 1) * width;
    const double s2 = s * s;
    const double s3 = s2 * s;
    return values[segment] * (2.0 * s3 - 3.0 * s2 + 1.0)
        + m0 * (s3 - 2.0 * s2 + s)
        + values[segment + 1] * (-2.0 * s3 + 3.0 * s2)
        + m1 * (s3 - s2);
}

//! 単調な折れ線マップ。pairs は (入力, 出力) の列で入力昇順。範囲外はクランプ。
double PiecewiseLinearMap(const std::vector<std::pair<double, double>>& pairs, double input)
{
    if (input <= pairs.front().first) {
        return pairs.front().second;
    }
    if (input >= pairs.back().first) {
        return pairs.back().second;
    }
    std::size_t segment = 0;
    while (segment + 2 < pairs.size() && input >= pairs[segment + 1].first) {
        ++segment;
    }
    const double span = pairs[segment + 1].first - pairs[segment].first;
    const double local = (input - pairs[segment].first) / span;
    return pairs[segment].second + (pairs[segment + 1].second - pairs[segment].second) * local;
}

struct CurveClosestPair {
    double firstParam = 0.0;
    double secondParam = 0.0;
    double distance = 0.0;
};

//! 2本のワイヤーの最近接点対(粗探索+交互1次元黄金分割の反復)。
CurveClosestPair ClosestPairBetweenWires(const Wire& first, const Wire& second)
{
    constexpr int kCoarse = 64;
    CurveClosestPair best;
    best.distance = std::numeric_limits<double>::infinity();
    std::array<Vector3, kCoarse + 1> secondSamples;
    for (int b = 0; b <= kCoarse; ++b) {
        secondSamples[static_cast<std::size_t>(b)] = second.Evaluate(static_cast<double>(b) / kCoarse);
    }
    for (int a = 0; a <= kCoarse; ++a) {
        const Vector3 firstPoint = first.Evaluate(static_cast<double>(a) / kCoarse);
        for (int b = 0; b <= kCoarse; ++b) {
            const double distance = (firstPoint - secondSamples[static_cast<std::size_t>(b)]).Length();
            if (distance < best.distance) {
                best = {static_cast<double>(a) / kCoarse, static_cast<double>(b) / kCoarse, distance};
            }
        }
    }

    const auto goldenMinimize = [](const auto& evaluateDistance, double center, double halfWidth) {
        double low = std::clamp(center - halfWidth, 0.0, 1.0);
        double high = std::clamp(center + halfWidth, 0.0, 1.0);
        constexpr double kRatio = 0.6180339887498949;
        double x1 = high - (high - low) * kRatio;
        double x2 = low + (high - low) * kRatio;
        double f1 = evaluateDistance(x1);
        double f2 = evaluateDistance(x2);
        for (int iteration = 0; iteration < 48; ++iteration) {
            if (f1 <= f2) {
                high = x2;
                x2 = x1;
                f2 = f1;
                x1 = high - (high - low) * kRatio;
                f1 = evaluateDistance(x1);
            } else {
                low = x1;
                x1 = x2;
                f1 = f2;
                x2 = low + (high - low) * kRatio;
                f2 = evaluateDistance(x2);
            }
        }
        return (low + high) * 0.5;
    };

    double halfWidth = 1.5 / kCoarse;
    for (int iteration = 0; iteration < 12; ++iteration) {
        const Vector3 secondPoint = second.Evaluate(best.secondParam);
        best.firstParam = goldenMinimize(
            [&](double a) { return (first.Evaluate(a) - secondPoint).Length(); },
            best.firstParam, halfWidth);
        const Vector3 firstPoint = first.Evaluate(best.firstParam);
        best.secondParam = goldenMinimize(
            [&](double b) { return (second.Evaluate(b) - firstPoint).Length(); },
            best.secondParam, halfWidth);
        halfWidth = std::max(halfWidth * 0.5, 1.0e-6);
    }
    best.distance = (first.Evaluate(best.firstParam) - second.Evaluate(best.secondParam)).Length();
    return best;
}

} // namespace

Surface::Surface(
    SurfaceKind kind,
    std::vector<Wire> boundaries,
    std::optional<WorkPlane> planarWorkPlane,
    double minimumU,
    double minimumV,
    double maximumU,
    double maximumV)
    : kind_(kind),
      boundaries_(std::move(boundaries)),
      planarWorkPlane_(std::move(planarWorkPlane)),
      minimumU_(minimumU),
      minimumV_(minimumV),
      maximumU_(maximumU),
      maximumV_(maximumV)
{
}

Surface Surface::Planar(Wire closedBoundary, double tolerance)
{
    if (!closedBoundary.IsClosed(tolerance)) {
        throw std::invalid_argument("Planar surface requires a closed boundary wire.");
    }

    WorkPlane plane = [&] {
        if (closedBoundary.Kind() == WireKind::Circle) {
            const auto arc = closedBoundary.ArcData();
            return WorkPlane::FromOriginAxes(arc.center, arc.uAxis, Cross(arc.uAxis, arc.vAxis));
        }
        const auto& points = closedBoundary.ControlPoints();
        for (std::size_t second = 1; second < points.size(); ++second) {
            for (std::size_t third = second + 1; third < points.size(); ++third) {
                if (Cross(points[second] - points[0], points[third] - points[0]).LengthSquared() > 1.0e-18) {
                    return WorkPlane::FromThreePoints(points[0], points[second], points[third]);
                }
            }
        }
        throw std::invalid_argument("Planar surface boundary must enclose an area.");
    }();

    double minimumU = std::numeric_limits<double>::infinity();
    double minimumV = std::numeric_limits<double>::infinity();
    double maximumU = -std::numeric_limits<double>::infinity();
    double maximumV = -std::numeric_limits<double>::infinity();
    for (int sample = 0; sample <= 256; ++sample) {
        const auto coordinates = plane.Project(closedBoundary.Evaluate(static_cast<double>(sample) / 256.0));
        if (std::abs(coordinates.w) > tolerance) {
            throw std::invalid_argument("Planar surface boundary is not planar.");
        }
        minimumU = std::min(minimumU, coordinates.u);
        minimumV = std::min(minimumV, coordinates.v);
        maximumU = std::max(maximumU, coordinates.u);
        maximumV = std::max(maximumV, coordinates.v);
    }
    if (maximumU - minimumU <= tolerance || maximumV - minimumV <= tolerance) {
        throw std::invalid_argument("Planar surface boundary must enclose an area.");
    }
    std::vector<Wire> boundaries;
    boundaries.push_back(std::move(closedBoundary));
    return {SurfaceKind::Planar, std::move(boundaries), plane, minimumU, minimumV, maximumU, maximumV};
}

Surface Surface::Ruled(Wire firstSection, Wire secondSection)
{
    std::vector<Wire> sections;
    sections.push_back(std::move(firstSection));
    sections.push_back(std::move(secondSection));
    return {SurfaceKind::Ruled, PrepareSections(std::move(sections), 2), std::nullopt, 0.0, 0.0, 1.0, 1.0};
}

Surface Surface::Loft(std::vector<Wire> sections)
{
    return {SurfaceKind::Loft, PrepareSections(std::move(sections), 3), std::nullopt, 0.0, 0.0, 1.0, 1.0};
}

Surface Surface::Gordon(std::vector<Wire> sections, std::vector<Wire> guides, double intersectionTolerance)
{
    if (sections.size() < 2) {
        throw std::invalid_argument("Gordon surface requires at least two section wires.");
    }
    if (guides.empty()) {
        throw std::invalid_argument("Gordon surface requires at least one guide wire.");
    }
    if (!std::isfinite(intersectionTolerance) || intersectionTolerance <= 0.0) {
        throw std::invalid_argument("Gordon surface intersection tolerance must be positive.");
    }
    for (const Wire& section : sections) {
        if (section.IsClosed()) {
            throw std::invalid_argument("Gordon surface currently supports open section wires only.");
        }
    }
    for (const Wire& guide : guides) {
        if (guide.IsClosed()) {
            throw std::invalid_argument("Gordon surface guide wires must be open.");
        }
    }

    // 断面の向きを揃え、隣接断面の分離を検査(PrepareSectionsと同等、2断面から許可)。
    for (std::size_t index = 1; index < sections.size(); ++index) {
        const double sameDirection = (sections[index - 1].Start() - sections[index].Start()).LengthSquared()
            + (sections[index - 1].End() - sections[index].End()).LengthSquared();
        const double reversedDirection = (sections[index - 1].Start() - sections[index].End()).LengthSquared()
            + (sections[index - 1].End() - sections[index].Start()).LengthSquared();
        if (reversedDirection + 1.0e-12 < sameDirection) {
            sections[index] = sections[index].Reversed();
        }
        double separation = 0.0;
        for (int sample = 0; sample <= 32; ++sample) {
            const double u = static_cast<double>(sample) / 32.0;
            separation = std::max(separation, (sections[index - 1].Evaluate(u) - sections[index].Evaluate(u)).Length());
        }
        if (separation <= 1.0e-8) {
            throw std::invalid_argument("Adjacent surface sections must be separated.");
        }
    }

    const std::size_t sectionCount = sections.size();
    std::vector<GordonGuideData> guideData;
    guideData.reserve(guides.size());
    double maximumGap = 0.0;
    for (std::size_t guideIndex = 0; guideIndex < guides.size(); ++guideIndex) {
        Wire guide = guides[guideIndex];
        std::vector<double> sectionU(sectionCount, 0.0);
        std::vector<double> guideT(sectionCount, 0.0);
        for (std::size_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
            const CurveClosestPair pair = ClosestPairBetweenWires(sections[sectionIndex], guide);
            if (pair.distance > intersectionTolerance) {
                std::ostringstream message;
                message << "Guide wire " << (guideIndex + 1) << " does not touch section "
                        << (sectionIndex + 1) << " (gap " << pair.distance
                        << " mm exceeds tolerance " << intersectionTolerance << " mm).";
                throw std::invalid_argument(message.str());
            }
            sectionU[sectionIndex] = pair.firstParam;
            guideT[sectionIndex] = pair.secondParam;
            maximumGap = std::max(maximumGap, pair.distance);
        }
        // ガイドの向きを断面順に揃える。
        if (sectionCount >= 2 && guideT.back() < guideT.front()) {
            guide = guide.Reversed();
            for (double& t : guideT) {
                t = 1.0 - t;
            }
        }
        for (std::size_t sectionIndex = 1; sectionIndex < sectionCount; ++sectionIndex) {
            if (guideT[sectionIndex] <= guideT[sectionIndex - 1] + 1.0e-9) {
                std::ostringstream message;
                message << "Guide wire " << (guideIndex + 1)
                        << " must cross the sections one by one in order (crossing parameters are not increasing).";
                throw std::invalid_argument(message.str());
            }
        }
        double knotSum = 0.0;
        for (const double u : sectionU) {
            knotSum += u;
        }
        guideData.push_back(GordonGuideData{
            std::move(guide),
            knotSum / static_cast<double>(sectionCount),
            std::move(sectionU),
            std::move(guideT),
        });
    }

    // ガイドを共通uで昇順に並べ、同一位置・順序矛盾を検査。
    std::sort(guideData.begin(), guideData.end(), [](const GordonGuideData& a, const GordonGuideData& b) {
        return a.knotU < b.knotU;
    });
    for (std::size_t guideIndex = 1; guideIndex < guideData.size(); ++guideIndex) {
        if (guideData[guideIndex].knotU <= guideData[guideIndex - 1].knotU + 1.0e-6) {
            throw std::invalid_argument("Two guide wires cross the sections at the same position.");
        }
    }
    for (std::size_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
        for (std::size_t guideIndex = 1; guideIndex < guideData.size(); ++guideIndex) {
            if (guideData[guideIndex].sectionU[sectionIndex]
                <= guideData[guideIndex - 1].sectionU[sectionIndex] + 1.0e-9) {
                throw std::invalid_argument("Guide wires must cross every section in the same order.");
            }
        }
    }

    Surface surface{SurfaceKind::Gordon, std::move(sections), std::nullopt, 0.0, 0.0, 1.0, 1.0};
    surface.guides_ = std::move(guides);
    surface.gordonGuides_ = std::move(guideData);
    surface.maximumGuideGap_ = maximumGap;
    return surface;
}

Vector3 Surface::Evaluate(double u, double v) const
{
    if (!std::isfinite(u) || !std::isfinite(v)) {
        throw std::invalid_argument("Surface parameters must be finite.");
    }
    const double clampedU = std::clamp(u, 0.0, 1.0);
    const double clampedV = std::clamp(v, 0.0, 1.0);
    if (kind_ == SurfaceKind::Planar) {
        return planarWorkPlane_->ToWorld(
            minimumU_ + (maximumU_ - minimumU_) * clampedU,
            minimumV_ + (maximumV_ - minimumV_) * clampedV);
    }
    if (kind_ == SurfaceKind::Loft) {
        return EvaluateSmoothLoft(boundaries_, clampedU, clampedV);
    }
    if (kind_ == SurfaceKind::Gordon) {
        return EvaluateGordon(clampedU, clampedV);
    }
    const double scaledV = clampedV * static_cast<double>(boundaries_.size() - 1);
    const std::size_t segment = static_cast<std::size_t>(std::min(
        scaledV,
        static_cast<double>(boundaries_.size() - 2)));
    const double localV = scaledV - static_cast<double>(segment);
    const Vector3 first = boundaries_[segment].Evaluate(clampedU);
    const Vector3 second = boundaries_[segment + 1].Evaluate(clampedU);
    return first * (1.0 - localV) + second * localV;
}

Vector3 Surface::EvaluateGordon(double u, double v) const
{
    const std::size_t sectionCount = boundaries_.size();
    const std::size_t guideCount = gordonGuides_.size();

    // 各断面を「ガイドの共通u → その断面での交点パラメータ」で再パラメータ化して評価する。
    // これにより u = knotU の線上で全断面の交点が縦に揃う。
    std::vector<Vector3> sectionPoints(sectionCount);
    std::vector<std::pair<double, double>> parameterMap;
    for (std::size_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
        parameterMap.clear();
        parameterMap.reserve(guideCount + 2);
        parameterMap.emplace_back(0.0, 0.0);
        for (const GordonGuideData& data : gordonGuides_) {
            if (parameterMap.back().first + 1.0e-9 < data.knotU) {
                parameterMap.emplace_back(data.knotU, data.sectionU[sectionIndex]);
            }
        }
        if (parameterMap.back().first + 1.0e-9 < 1.0) {
            parameterMap.emplace_back(1.0, 1.0);
        }
        sectionPoints[sectionIndex] = boundaries_[sectionIndex].Evaluate(PiecewiseLinearMap(parameterMap, u));
    }
    const Vector3 loftPoint = CatmullRomPoints(sectionPoints, v);

    // 各ガイドの補正量 D_j(v) = ガイド上の点 − 断面交点列のCatmull-Rom補間。
    // 断面上(v = i/(n-1))では厳密に0になるため、断面は常に厳密に通る。
    std::vector<double> knots;
    std::vector<Vector3> corrections;
    knots.reserve(guideCount + 2);
    corrections.reserve(guideCount + 2);
    if (gordonGuides_.front().knotU > 1.0e-9) {
        knots.push_back(0.0);
        corrections.push_back({0.0, 0.0, 0.0});
    }
    std::vector<Vector3> crossings(sectionCount);
    std::vector<std::pair<double, double>> guideMap(sectionCount);
    for (const GordonGuideData& data : gordonGuides_) {
        for (std::size_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
            guideMap[sectionIndex] = {
                sectionCount == 1 ? 0.0 : static_cast<double>(sectionIndex) / static_cast<double>(sectionCount - 1),
                data.guideT[sectionIndex],
            };
            crossings[sectionIndex] = data.guide.Evaluate(data.guideT[sectionIndex]);
        }
        const Vector3 guidePoint = data.guide.Evaluate(PiecewiseLinearMap(guideMap, v));
        knots.push_back(data.knotU);
        corrections.push_back(guidePoint - CatmullRomPoints(crossings, v));
    }
    if (gordonGuides_.back().knotU < 1.0 - 1.0e-9) {
        knots.push_back(1.0);
        corrections.push_back({0.0, 0.0, 0.0});
    }
    return loftPoint + NonUniformCardinal(knots, corrections, u);
}

Vector3 Surface::Normal(double u, double v) const
{
    const Vector3 normal = Cross(DerivativeU(*this, std::clamp(u, 0.0, 1.0), std::clamp(v, 0.0, 1.0)),
        DerivativeV(*this, std::clamp(u, 0.0, 1.0), std::clamp(v, 0.0, 1.0)));
    if (normal.LengthSquared() <= 1.0e-18) {
        throw std::invalid_argument("Surface normal is undefined at this position.");
    }
    return normal.Normalized();
}

bool Surface::ContainsPlanarPoint(double u, double v, double tolerance) const
{
    const auto point = planarWorkPlane_->ToWorld(u, v);
    bool inside = false;
    auto previous = planarWorkPlane_->Project(boundaries_.front().Evaluate(1.0));
    for (int sample = 0; sample <= 256; ++sample) {
        const auto current = planarWorkPlane_->Project(boundaries_.front().Evaluate(static_cast<double>(sample) / 256.0));
        const Vector3 segmentStart = planarWorkPlane_->ToWorld(previous.u, previous.v);
        const Vector3 segmentEnd = planarWorkPlane_->ToWorld(current.u, current.v);
        const Vector3 segment = segmentEnd - segmentStart;
        if (segment.LengthSquared() > 1.0e-24) {
            const double parameter = std::clamp(Dot(point - segmentStart, segment) / segment.LengthSquared(), 0.0, 1.0);
            if ((point - (segmentStart + segment * parameter)).Length() <= tolerance) {
                return true;
            }
        }
        if ((current.v > v) != (previous.v > v)) {
            const double crossingU = (previous.u - current.u) * (v - current.v) / (previous.v - current.v) + current.u;
            if (u < crossingU) {
                inside = !inside;
            }
        }
        previous = current;
    }
    return inside;
}

SurfaceProjection Surface::ProjectPointAlongDirection(Vector3 sourcePoint, Vector3 direction, double tolerance) const
{
    if (!sourcePoint.IsFinite() || !direction.IsFinite() || direction.LengthSquared() <= 1.0e-18
        || !std::isfinite(tolerance) || tolerance <= 0.0) {
        throw std::invalid_argument("Projection point, direction, and tolerance must be valid.");
    }
    direction = direction.Normalized();
    if (kind_ == SurfaceKind::Planar) {
        const double denominator = Dot(direction, planarWorkPlane_->Normal());
        if (std::abs(denominator) <= 1.0e-12) {
            throw std::invalid_argument("Projection direction is parallel to the planar surface.");
        }
        const double distance = Dot(planarWorkPlane_->Origin() - sourcePoint, planarWorkPlane_->Normal()) / denominator;
        const Vector3 projected = sourcePoint + direction * distance;
        const auto coordinates = planarWorkPlane_->Project(projected);
        if (!ContainsPlanarPoint(coordinates.u, coordinates.v, tolerance)) {
            throw std::invalid_argument("Projected point is outside the planar surface boundary.");
        }
        return {
            projected,
            (coordinates.u - minimumU_) / (maximumU_ - minimumU_),
            (coordinates.v - minimumV_) / (maximumV_ - minimumV_),
            distance,
        };
    }

    std::optional<SurfaceProjection> best;
    for (int uIndex = 0; uIndex <= 16; ++uIndex) {
        for (int vIndex = 0; vIndex <= 10; ++vIndex) {
            const double u = static_cast<double>(uIndex) / 16.0;
            const double v = static_cast<double>(vIndex) / 10.0;
            const auto candidate = RefineProjection(*this, sourcePoint, direction, u, v, tolerance);
            if (candidate.has_value()
                && (!best.has_value()
                    || std::abs(candidate->distanceAlongDirection) < std::abs(best->distanceAlongDirection))) {
                best = candidate;
            }
        }
    }
    if (!best.has_value()) {
        throw std::invalid_argument("Projection line does not intersect the surface.");
    }
    return *best;
}

Wire Surface::ProjectWireAlongDirection(const Wire& sourceWire, Vector3 direction, int samples, double tolerance) const
{
    const int sampleCount = ProjectionSampleCount(sourceWire, samples);
    std::vector<Vector3> points;
    points.reserve(static_cast<std::size_t>(sampleCount) + 1);
    std::optional<SurfaceProjection> previousProjection;
    for (int sample = 0; sample <= sampleCount; ++sample) {
        const Vector3 sourcePoint = sourceWire.Evaluate(static_cast<double>(sample) / sampleCount);
        std::optional<SurfaceProjection> projection;
        if (kind_ != SurfaceKind::Planar && previousProjection.has_value()) {
            projection = RefineProjection(
                *this,
                sourcePoint,
                direction.Normalized(),
                previousProjection->u,
                previousProjection->v,
                tolerance);
        }
        if (!projection.has_value()) {
            projection = ProjectPointAlongDirection(sourcePoint, direction, tolerance);
        }
        previousProjection = projection;
        const Vector3 projected = projection->point;
        if (points.empty() || (projected - points.back()).LengthSquared() > tolerance * tolerance) {
            points.push_back(projected);
        }
    }
    if (points.size() < 2) {
        throw std::invalid_argument("Projected wire collapsed to a point.");
    }
    if (sourceWire.IsClosed()) {
        if ((points.front() - points.back()).LengthSquared() > tolerance * tolerance) {
            points.push_back(points.front());
        } else {
            points.back() = points.front();
        }
    }
    return Wire::Polyline(std::move(points));
}

} // namespace kachakacha::model
