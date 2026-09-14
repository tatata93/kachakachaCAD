#include "kachakacha/app/RailwayNoseHoSample.h"

#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace kachakacha::v2::app {
namespace {

using geometry::CurveSegment;
using geometry::Vector3;

//! 前後位置を 0(前端)〜1(車体側)へ直す。
[[nodiscard]] double Along(double stationMm)
{
    return std::clamp(stationMm / kHoNoseDepthMm, 0.0, 1.0);
}

//! その位置での半幅(mm)。前端は絞られ、車体側で全幅になる。
//!
//! 前端をゼロにしない。ゼロにすると1点へ収束して、そこだけ極端に曲がる。
//! 実物の流線形前面も、前端に小さな平らな面を持つ。
[[nodiscard]] double HalfWidthAt(double stationMm)
{
    const double t = Along(stationMm);
    const double full = kHoNoseWidthMm * 0.5;
    // 前端で 46%、車体側で 100%。滑らかに立ち上げる。
    return full * (0.46 + 0.54 * std::sin(t * 1.5707963267948966));
}

//! その位置での屋根の高さ(mm)。前端は低く、車体側で全高になる。
[[nodiscard]] double RoofHeightAt(double stationMm)
{
    const double t = Along(stationMm);
    return kHoNoseHeightMm * (0.78 + 0.22 * std::sin(t * 1.5707963267948966));
}

//! その位置での中央の膨らみ(mm)。前へ張り出す量。前端がいちばん大きい。
[[nodiscard]] double CentreBulgeAt(double stationMm)
{
    const double t = Along(stationMm);
    return kHoNoseDepthMm * 0.18 * (1.0 - t) * (1.0 - t);
}

//! その位置での肩の丸み(0..1)。前端ほど丸く、車体側で角ばる。
[[nodiscard]] double ShoulderRoundAt(double stationMm)
{
    const double t = Along(stationMm);
    return 0.62 - 0.34 * t;
}

//! その位置での裾の絞り(0..1)。1 なら絞らない。
[[nodiscard]] double SkirtTuckAt(double stationMm)
{
    const double t = Along(stationMm);
    return 0.80 + 0.16 * t;
}

} // namespace

const std::vector<double>& HoNoseSectionStations()
{
    // §18 の例に合わせる。前端から車体側まで6枚。
    static const std::vector<double> stations{0.0, 3.0, 6.0, 10.0, 14.0, 18.0};
    return stations;
}

std::string HoNoseSectionName(double stationMm)
{
    std::ostringstream text;
    text << "NoseSection_X" << std::setfill('0') << std::setw(3)
         << static_cast<int>(std::lround(stationMm));
    return text.str();
}

std::string HoNoseWorkPlaneName(double stationMm)
{
    std::ostringstream text;
    text << "WP_X" << std::setfill('0') << std::setw(3)
         << static_cast<int>(std::lround(stationMm));
    return text.str();
}

Vector3 HoNosePoint(double u, double stationMm)
{
    // u は -1(左の裾)〜0(屋根の中央)〜+1(右の裾)。
    // 断面は屋根の中央から左右の裾へ降りる1本の線として作る。
    const double side = u < 0.0 ? -1.0 : 1.0;
    const double a = std::clamp(std::abs(u), 0.0, 1.0);
    const double halfWidth = HalfWidthAt(stationMm);
    const double roof = RoofHeightAt(stationMm);
    const double round = ShoulderRoundAt(stationMm);
    const double tuck = SkirtTuckAt(stationMm);

    // 屋根から肩を回って裾へ。丸みの強さで肩の張り方が変わる。
    const double angle = a * 1.5707963267948966;
    const double flat = std::pow(std::sin(angle), 1.0 + round);
    const double drop = std::pow(std::sin(angle), 2.0 - round);
    double y = halfWidth * flat;
    double z = roof - (roof * 0.62) * drop;
    // 裾を絞る。下へ行くほど内側へ入る。
    const double lower = std::clamp((roof - z) / std::max(roof, 1.0e-9), 0.0, 1.0);
    y *= 1.0 - (1.0 - tuck) * lower * lower;
    // 前面中央が前へ膨らむ。中央がいちばん出て、肩へ向かって収まる。
    const double bulge = CentreBulgeAt(stationMm) * std::cos(angle) * std::cos(angle);
    const double x = stationMm - bulge;
    return Vector3{x, side * y, z};
}

namespace {

//! 点の並びを、滑らかな3次ベジェの鎖にする。
//!
//! 折れ線にしない。折れ線で面を作ると、面にも折れ目が出る。
//! 接線は隣どうしの差から作る(Catmull-Rom を3次ベジェへ直す)。
[[nodiscard]] std::vector<CurveSegment> SmoothChain(const std::vector<Vector3>& points)
{
    std::vector<CurveSegment> segments;
    if (points.size() < 2) {
        return segments;
    }
    for (std::size_t index = 0; index + 1 < points.size(); ++index) {
        const Vector3& start = points[index];
        const Vector3& end = points[index + 1];
        const Vector3 before = index == 0 ? start : points[index - 1];
        const Vector3 after = index + 2 < points.size() ? points[index + 2] : end;
        const Vector3 firstTangent = (end - before) * (1.0 / 6.0);
        const Vector3 secondTangent = (after - start) * (1.0 / 6.0);
        auto made = CurveSegment::MakeCubicBezier(
            {start, start + firstTangent, end - secondTangent, end});
        if (made.HasValue()) {
            segments.push_back(made.Value());
        }
    }
    return segments;
}

constexpr std::size_t kSectionSamples = 13;

} // namespace

std::vector<CurveSegment> HoNoseSection(double stationMm)
{
    // 左の裾から屋根の中央を通って右の裾まで、1本の開いた線にする。
    std::vector<Vector3> points;
    points.reserve(kSectionSamples);
    for (std::size_t index = 0; index < kSectionSamples; ++index) {
        const double u = -1.0
            + 2.0 * static_cast<double>(index) / static_cast<double>(kSectionSamples - 1);
        points.push_back(HoNosePoint(u, stationMm));
    }
    return SmoothChain(points);
}

namespace {

//! 前後方向に station を刻んで、面の上の1本の線を作る。
[[nodiscard]] std::vector<CurveSegment> AlongNose(double u)
{
    std::vector<Vector3> points;
    constexpr std::size_t kSteps = 9;
    for (std::size_t index = 0; index < kSteps; ++index) {
        const double station = kHoNoseDepthMm * static_cast<double>(index)
            / static_cast<double>(kSteps - 1);
        points.push_back(HoNosePoint(u, station));
    }
    return SmoothChain(points);
}

} // namespace

std::vector<CurveSegment> HoNoseRoofCenterGuide()
{
    // 屋根の真ん中を前後に走る線。前面中央の膨らみはこの線に出る。
    return AlongNose(0.0);
}

std::vector<CurveSegment> HoNoseShoulderGuide(bool left)
{
    // 肩(屋根から側面へ回り込むところ)。曲率がいちばん変わる。
    return AlongNose(left ? -0.55 : 0.55);
}

std::vector<CurveSegment> HoNoseLowerGuide()
{
    // 裾。絞りの効き方はこの線に出る。左側を代表にする。
    return AlongNose(-0.95);
}

} // namespace kachakacha::v2::app
