#include "kachakacha/app/SurfaceAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace kachakacha::v2::app {

namespace {

using geometry::Vector3;

[[nodiscard]] std::string Format(double value, int digits)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    return buffer;
}

[[nodiscard]] std::string Scientific(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2e", value);
    return buffer;
}

[[nodiscard]] std::uint8_t Mix(std::uint8_t a, std::uint8_t b, double t) noexcept
{
    return static_cast<std::uint8_t>(std::lround(a + (b - a) * std::clamp(t, 0.0, 1.0)));
}

//! 角の値をまとめた絶対値の並びの、上から 5 % を外した最大(頂点の特異点に引きずられない)。
[[nodiscard]] double RobustMaximum(std::vector<double> values)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t at = static_cast<std::size_t>(0.95 * static_cast<double>(values.size() - 1));
    return values[at];
}

[[nodiscard]] std::vector<double> AbsoluteValues(const modeling::SurfaceAnalysisData& data,
    bool gaussian)
{
    std::vector<double> values;
    values.reserve(data.triangles.size() * 3);
    for (const auto& triangle : data.triangles) {
        for (int corner = 0; corner < 3; ++corner) {
            const double value = gaussian ? triangle.gaussian[static_cast<std::size_t>(corner)]
                                          : triangle.mean[static_cast<std::size_t>(corner)];
            if (std::isfinite(value)) {
                values.push_back(std::abs(value));
            }
        }
    }
    return values;
}

} // namespace

std::string_view SurfaceAnalysisModeLabelJa(SurfaceAnalysisMode mode) noexcept
{
    switch (mode) {
    case SurfaceAnalysisMode::None:              return "なし";
    case SurfaceAnalysisMode::Zebra:             return "ゼブラ";
    case SurfaceAnalysisMode::MeanCurvature:     return "平均曲率";
    case SurfaceAnalysisMode::GaussianCurvature: return "ガウス曲率(可展性)";
    case SurfaceAnalysisMode::IsoCurves:         return "U/V 線";
    case SurfaceAnalysisMode::CurvatureComb:     return "曲率コーム";
    case SurfaceAnalysisMode::Continuity:        return "境目の連続";
    case SurfaceAnalysisMode::Deviation:         return "入力線からのずれ";
    }
    return "なし";
}

const std::vector<SurfaceAnalysisMode>& SurfaceAnalysisModes()
{
    static const std::vector<SurfaceAnalysisMode> modes{SurfaceAnalysisMode::None,
        SurfaceAnalysisMode::Zebra, SurfaceAnalysisMode::MeanCurvature,
        SurfaceAnalysisMode::GaussianCurvature, SurfaceAnalysisMode::IsoCurves,
        SurfaceAnalysisMode::CurvatureComb, SurfaceAnalysisMode::Continuity,
        SurfaceAnalysisMode::Deviation};
    return modes;
}

bool AnalysisPaintsSurface(SurfaceAnalysisMode mode) noexcept
{
    return mode == SurfaceAnalysisMode::Zebra || mode == SurfaceAnalysisMode::MeanCurvature
        || mode == SurfaceAnalysisMode::GaussianCurvature;
}

Rgb DivergingColor(double value, double scale) noexcept
{
    const Rgb white{245, 245, 245};
    const Rgb blue{40, 90, 220};
    const Rgb red{220, 50, 40};
    if (!(scale > 0.0) || !std::isfinite(value)) {
        return white;
    }
    const double t = std::clamp(value / scale, -1.0, 1.0);
    const Rgb& end = t < 0.0 ? blue : red;
    const double k = std::abs(t);
    return Rgb{Mix(white.r, end.r, k), Mix(white.g, end.g, k), Mix(white.b, end.b, k)};
}

bool ZebraDark(const Vector3& normal, const Vector3& viewDirection, const Vector3& upDirection,
    int stripes) noexcept
{
    // 反射の縞: 見る向き v を法線 n で映した r = v − 2(v・n)n の、上の向きの成分で縞を決める。
    const double vn = viewDirection.x * normal.x + viewDirection.y * normal.y
        + viewDirection.z * normal.z;
    const Vector3 reflected = viewDirection - normal * (2.0 * vn);
    const double up = std::clamp(reflected.x * upDirection.x + reflected.y * upDirection.y
            + reflected.z * upDirection.z, -1.0, 1.0);
    const double angle = std::asin(up) / 3.14159265358979323846 + 0.5;   // 0..1
    const int band = static_cast<int>(std::floor(angle * std::max(2, stripes)));
    return band % 2 == 1;
}

double CurvatureScale(const modeling::SurfaceAnalysisData& data, bool gaussian)
{
    const double robust = RobustMaximum(AbsoluteValues(data, gaussian));
    return robust > 1.0e-12 ? robust : (gaussian ? 1.0e-6 : 1.0e-3);
}

double CurvatureScale(const std::vector<const modeling::SurfaceAnalysisData*>& all, bool gaussian)
{
    double scale = 0.0;
    for (const modeling::SurfaceAnalysisData* data : all) {
        if (data != nullptr && !data->Empty()) {
            scale = std::max(scale, CurvatureScale(*data, gaussian));
        }
    }
    return scale > 0.0 ? scale : (gaussian ? 1.0e-6 : 1.0e-3);
}

std::string_view DevelopabilityLabelJa(DevelopabilityClass kind) noexcept
{
    switch (kind) {
    case DevelopabilityClass::NearlyDevelopable:    return "ほぼ可展";
    case DevelopabilityClass::DoubleCurved:         return "二重曲率あり";
    case DevelopabilityClass::StronglyDoubleCurved: return "強い二重曲率";
    }
    return "";
}

DevelopabilitySummary ClassifyDevelopability(const modeling::SurfaceAnalysisData& data,
    const DevelopabilityThresholds& thresholds)
{
    DevelopabilitySummary summary;
    summary.maximumAbsGaussian = RobustMaximum(AbsoluteValues(data, true));
    summary.sizeMm = data.sizeMm;
    const double half = data.sizeMm * 0.5;
    summary.strain = summary.maximumAbsGaussian * half * half / 6.0;
    summary.kind = summary.strain < thresholds.nearlyDevelopableStrain
        ? DevelopabilityClass::NearlyDevelopable
        : summary.strain < thresholds.stronglyDoubleCurvedStrain
            ? DevelopabilityClass::DoubleCurved
            : DevelopabilityClass::StronglyDoubleCurved;
    summary.labelJa = std::string(DevelopabilityLabelJa(summary.kind));
    summary.explanationJa = "平らに広げるのに要る伸び縮みの目安 "
        + Format(summary.strain * 100.0, 3) + " %(|K| 最大 "
        + Scientific(summary.maximumAbsGaussian) + " /mm²、大きさ " + Format(summary.sizeMm, 1)
        + " mm、ε ≈ |K|·(大きさ/2)²/6)。基準: " + Format(thresholds.nearlyDevelopableStrain * 100.0, 1)
        + " % 未満 = ほぼ可展、" + Format(thresholds.stronglyDoubleCurvedStrain * 100.0, 1)
        + " % 以上 = 強い二重曲率。板厚・近似・許容差・部材の分け方でも変わる、製作の診断材料です"
          "(K = 0 でも必ず作れるとは限りません)。";
    return summary;
}

DevelopabilitySummary WorstDevelopability(
    const std::vector<const modeling::SurfaceAnalysisData*>& all,
    const DevelopabilityThresholds& thresholds)
{
    DevelopabilitySummary worst;
    bool found = false;
    for (const modeling::SurfaceAnalysisData* data : all) {
        if (data == nullptr || data->Empty()) {
            continue;
        }
        DevelopabilitySummary one = ClassifyDevelopability(*data, thresholds);
        if (!found || one.strain > worst.strain) {
            worst = std::move(one);
            found = true;
        }
    }
    return worst;
}

ContinuityGrade GradeContinuity(const modeling::EdgeContinuitySample& sample) noexcept
{
    if (!sample.hasNeighbor) {
        return ContinuityGrade::Open;
    }
    if (sample.gapMm > 0.01) {
        return ContinuityGrade::Gap;
    }
    if (sample.angleDeg > 1.5) {
        return ContinuityGrade::G0;
    }
    if (sample.curvatureDifference > 0.1) {
        return ContinuityGrade::G1;
    }
    return ContinuityGrade::G2;
}

std::string_view ContinuityGradeLabelJa(ContinuityGrade grade) noexcept
{
    switch (grade) {
    case ContinuityGrade::Open: return "隣の面が無い縁";
    case ContinuityGrade::Gap:  return "離れている";
    case ContinuityGrade::G0:   return "G0(折れ目)";
    case ContinuityGrade::G1:   return "G1(接して滑らか)";
    case ContinuityGrade::G2:   return "G2(曲がり方までそろう)";
    }
    return "";
}

Rgb ContinuityGradeColor(ContinuityGrade grade) noexcept
{
    switch (grade) {
    case ContinuityGrade::Open: return Rgb{150, 150, 150};
    case ContinuityGrade::Gap:  return Rgb{220, 40, 40};
    case ContinuityGrade::G0:   return Rgb{240, 140, 30};
    case ContinuityGrade::G1:   return Rgb{170, 210, 40};
    case ContinuityGrade::G2:   return Rgb{40, 170, 80};
    }
    return Rgb{};
}

Rgb DeviationColor(double distanceMm, double exactMm, double approximateMm) noexcept
{
    if (distanceMm <= exactMm) {
        return Rgb{40, 170, 80};
    }
    if (distanceMm <= approximateMm) {
        return Rgb{230, 190, 30};
    }
    return Rgb{220, 40, 40};
}

std::vector<std::string> AnalysisLegendJa(SurfaceAnalysisMode mode,
    const modeling::SurfaceAnalysisData& data)
{
    return AnalysisLegendJa(mode, std::vector<const modeling::SurfaceAnalysisData*>{&data});
}

std::vector<std::string> AnalysisLegendJa(SurfaceAnalysisMode mode,
    const std::vector<const modeling::SurfaceAnalysisData*>& all)
{
    std::vector<std::string> lines;
    std::size_t surfaces = 0;
    std::size_t isoLines = 0;
    for (const modeling::SurfaceAnalysisData* data : all) {
        if (data != nullptr && !data->Empty()) {
            ++surfaces;
            isoLines += data->isoLines.size();
        }
    }
    if (mode == SurfaceAnalysisMode::None) {
        lines.push_back("解析を出していません。");
        return lines;
    }
    if (surfaces == 0) {
        lines.push_back("解析できる面がありません。面を選ぶか、面を作ってください"
                        "(面を作る・面の編集の下見も塗れます)。");
        return lines;
    }
    switch (mode) {
    case SurfaceAnalysisMode::None:
        break;
    case SurfaceAnalysisMode::Zebra:
        lines.push_back("縞が切れる所は折れ目(G0)、縞が折れる所は曲がり方の段差(G1)、"
                        "なめらかにつながれば G2。見る向きを回すと縞が動きます。");
        break;
    case SurfaceAnalysisMode::MeanCurvature:
        lines.push_back("赤 = ふくらみ、青 = へこみ、白 = 平ら(平均曲率 H、目盛り ±"
            + Scientific(CurvatureScale(all, false)) + " /mm"
            + (surfaces > 1 ? "、" + std::to_string(surfaces) + " 枚で共通" : std::string())
            + ")。");
        break;
    case SurfaceAnalysisMode::GaussianCurvature: {
        lines.push_back("赤 = 椀形(K > 0)、青 = 鞍形(K < 0)、白 = K ≈ 0(可展)。目盛り ±"
            + Scientific(CurvatureScale(all, true)) + " /mm²"
            + (surfaces > 1 ? "(" + std::to_string(surfaces) + " 枚で共通)" : std::string())
            + "。");
        const DevelopabilitySummary summary = WorstDevelopability(all);
        lines.push_back("製作性の目安: " + summary.labelJa
            + (surfaces > 1 ? "(" + std::to_string(surfaces) + " 枚のうち、いちばん平らに広げにくい面)"
                            : std::string()));
        lines.push_back(summary.explanationJa);
        break;
    }
    case SurfaceAnalysisMode::IsoCurves:
        lines.push_back("U/V 線 " + std::to_string(isoLines) + " 本(面の内側だけ)。");
        break;
    case SurfaceAnalysisMode::CurvatureComb:
        lines.push_back("縁に沿った曲率。歯が長いほど強く曲がっています。"
                        "歯の長さが急に変わる所は、曲がり方の段差です。");
        break;
    case SurfaceAnalysisMode::Continuity:
        lines.push_back("緑 = G2、黄緑 = G1、橙 = G0(折れ目)、赤 = 離れている、灰 = 隣の面が無い縁。"
                        "許容: 離れ 0.01 mm、折れ目 1.5 度、曲率の差 0.1 /mm。");
        break;
    case SurfaceAnalysisMode::Deviation:
        lines.push_back("面を作った線ごとの、面からの離れ。緑 = 通す許容以内、黄 = 近づける許容"
                        "以内、赤 = 超えている。線から作っていない面には出ません。");
        break;
    }
    return lines;
}

} // namespace kachakacha::v2::app
