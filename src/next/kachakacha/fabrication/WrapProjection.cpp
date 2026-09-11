#include "kachakacha/fabrication/WrapProjection.h"

#include "kachakacha/fabrication/SurfaceProjection.h"
#include "kachakacha/geometry/WireChain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace kachakacha::v2::fabrication {

using base::MakeError;
using base::Result;
using geometry::CurveSegment;
using geometry::Vector3;

namespace {

//! 標本の数。V1 と同じ 256。細かすぎると遅く、粗すぎると短い区間を取りこぼす。
constexpr int kSamples = 256;
//! 境目を詰める回数。V1 と同じ 12 回(1/4096 まで詰まる)。
constexpr int kRefineSteps = 12;
//! 面が見つからなかった印。
constexpr std::size_t kNoSurface = static_cast<std::size_t>(-1);

//! 線の並びを 0〜1 で読む。t は「何本目の途中か」で測る(線ごとに等分)。
[[nodiscard]] Vector3 ChainPointAt(const std::vector<CurveSegment>& curves, double t)
{
    const double count = static_cast<double>(curves.size());
    const double scaled = std::clamp(t, 0.0, 1.0) * count;
    std::size_t index = static_cast<std::size_t>(std::floor(scaled));
    if (index >= curves.size()) {
        index = curves.size() - 1;
    }
    const double local = std::clamp(scaled - static_cast<double>(index), 0.0, 1.0);
    return curves[index].Evaluate(local);
}

//! 巡回する t を 0〜1 へ畳む。
[[nodiscard]] double Wrapped(double t)
{
    double value = std::fmod(t, 1.0);
    if (value < 0.0) {
        value += 1.0;
    }
    return value;
}

//! その点から、向きに沿っていちばん近い面。見つからなければ kNoSurface。
[[nodiscard]] std::size_t NearestSurface(const std::vector<SurfacePatchSamples>& surfaces,
    const Vector3& point, const Vector3& direction)
{
    std::size_t best = kNoSurface;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < surfaces.size(); ++index) {
        const auto landed = ProjectPointOntoSampledSurface(surfaces[index], point, direction);
        if (!landed.HasValue()) {
            continue;
        }
        const double distance = std::abs(geometry::Dot(landed.Value() - point, direction));
        if (distance < bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }
    return best;
}

//! 面が変わる境目を二分探索で詰める。inside 側が surface に載っている。
//!
//! 返すのは **載っていると分かっている側** の値。真ん中を返すと、詰めた誤差のぶんだけ
//! 面の外へはみ出し、そこで「落ちない」と断ることになる。区間どうしの間には
//! 1/4096 ほどの隙間が残るが、面と面の間を勝手に橋渡しするよりはよい。
[[nodiscard]] double RefineBoundary(const std::vector<SurfacePatchSamples>& surfaces,
    const std::vector<CurveSegment>& curves, const Vector3& direction, double inside,
    double outside, std::size_t surface, bool closed)
{
    for (int step = 0; step < kRefineSteps; ++step) {
        const double middle = (inside + outside) * 0.5;
        const double at = closed ? Wrapped(middle) : middle;
        if (NearestSurface(surfaces, ChainPointAt(curves, at), direction) == surface) {
            inside = middle;
        } else {
            outside = middle;
        }
    }
    return inside;
}

//! 区間 [start, end] を、その面へ落とした折れ線にする。
[[nodiscard]] Result<std::vector<CurveSegment>> ProjectRange(
    const SurfacePatchSamples& surface, const std::vector<CurveSegment>& curves,
    const Vector3& direction, double start, double end, bool closed, double toleranceMm)
{
    using Out = Result<std::vector<CurveSegment>>;
    // 元の線の細かさに合わせて刻む。線1本あたり最低 16 点は取る。
    const int steps = std::max(8, static_cast<int>(std::ceil((end - start)
        * static_cast<double>(curves.size()) * 16.0)));
    std::vector<Vector3> landedPoints;
    for (int step = 0; step <= steps; ++step) {
        const double raw = start + (end - start) * static_cast<double>(step)
            / static_cast<double>(steps);
        const Vector3 point = ChainPointAt(curves, closed ? Wrapped(raw) : raw);
        const auto landed = ProjectPointOntoSampledSurface(surface, point, direction);
        if (!landed.HasValue()) {
            // 区間に分けても落ちないところがある。半分だけ落とした形を返さない。
            return Out::Failure(landed.Diagnostics());
        }
        if (landedPoints.empty()
            || (landed.Value() - landedPoints.back()).Length() > toleranceMm * 0.01) {
            landedPoints.push_back(landed.Value());
        }
    }
    std::vector<CurveSegment> made;
    for (std::size_t index = 0; index + 1 < landedPoints.size(); ++index) {
        const auto line = CurveSegment::MakeLine(landedPoints[index], landedPoints[index + 1]);
        if (line.HasValue()) {
            made.push_back(line.Value());
        }
    }
    if (made.empty()) {
        return Out::Failure(MakeError(kProjectionMisses, "その線は面の上に落ちません。",
            "落とした点が1か所に重なり、線になりません。"));
    }
    return Out::Success(std::move(made));
}

//! 標本ごとの面の割り当てから、続いている区間の切れ目(標本の番号)を集める。
struct SampleRun {
    std::size_t surface = kNoSurface;
    int firstSample = 0;
    int lastSample = 0;
};

[[nodiscard]] std::vector<SampleRun> RunsOf(const std::vector<std::size_t>& assignment,
    bool closed)
{
    std::vector<SampleRun> runs;
    const int count = static_cast<int>(assignment.size());
    int start = 0;
    if (closed) {
        // 巡回。切れ目のあるところから見はじめる。無ければ全部が1区間。
        while (start < count && assignment[static_cast<std::size_t>(start)]
                == assignment[static_cast<std::size_t>((start + count - 1) % count)]) {
            ++start;
        }
        if (start == count) {
            runs.push_back(SampleRun{assignment.front(), 0, count - 1});
            return runs;
        }
    }
    SampleRun current{assignment[static_cast<std::size_t>(start % count)], start, start};
    for (int step = 1; step < count; ++step) {
        const int sample = closed ? (start + step) % count : start + step;
        if (!closed && sample >= count) {
            break;
        }
        const std::size_t surface = assignment[static_cast<std::size_t>(sample)];
        if (surface == current.surface) {
            current.lastSample = closed ? start + step : sample;
            continue;
        }
        runs.push_back(current);
        current = SampleRun{surface, closed ? start + step : sample,
            closed ? start + step : sample};
    }
    runs.push_back(current);
    return runs;
}

} // namespace

Result<std::vector<WrapProjectionRun>> WrapProjectOntoSurfaces(
    const std::vector<SurfacePatchSamples>& surfaces, const std::vector<CurveSegment>& curves,
    const Vector3& direction, double toleranceMm)
{
    using Out = Result<std::vector<WrapProjectionRun>>;
    if (curves.empty()) {
        return Out::Failure(MakeError(kProjectionBadInput, "落とす線がありません。", {}));
    }
    if (surfaces.size() < 2) {
        return Out::Failure(MakeError(kWrapNeedsTwoSurfaces,
            "回り込み投影には、落とす先の面が2枚以上要ります。",
            "1枚だけなら「曲面へ投影」を使ってください。"));
    }
    if (geometry::Normalized(direction) == Vector3{}) {
        return Out::Failure(MakeError(kProjectionBadInput, "落とす向きが決まりません。", {}));
    }
    const geometry::GeometryTolerance tolerance{};
    const bool closed = geometry::SegmentsFormClosedLoop(curves, tolerance);

    // 標本ごとに、いちばん近い面を選ぶ。
    const int sampleCount = closed ? kSamples : kSamples + 1;
    std::vector<std::size_t> assignment(static_cast<std::size_t>(sampleCount), kNoSurface);
    for (int sample = 0; sample < sampleCount; ++sample) {
        const double t = static_cast<double>(sample) / static_cast<double>(kSamples);
        assignment[static_cast<std::size_t>(sample)] = NearestSurface(surfaces,
            ChainPointAt(curves, closed ? Wrapped(t) : std::clamp(t, 0.0, 1.0)), direction);
    }
    if (std::all_of(assignment.begin(), assignment.end(),
            [](std::size_t value) { return value == kNoSurface; })) {
        return Out::Failure(MakeError(kProjectionMisses, "その線は面の上に落ちません。",
            "選んだどの面にも当たりません。落とす向きと面を確かめてください。"));
    }

    const std::vector<SampleRun> runs = RunsOf(assignment, closed);
    const double step = 1.0 / static_cast<double>(kSamples);
    std::vector<WrapProjectionRun> made;
    for (std::size_t index = 0; index < runs.size(); ++index) {
        const SampleRun& run = runs[index];
        if (run.surface == kNoSurface) {
            // どの面にも当たらない区間がある。黙って飛ばさない。
            return Out::Failure(MakeError(kProjectionMisses,
                "どの面にも当たらないところがあります。",
                "角をまたぐ線は、またぐ面を全部選んでください。"));
        }
        double start = static_cast<double>(run.firstSample) * step;
        double end = static_cast<double>(run.lastSample) * step;
        // 境目を詰める。前後の区間との境は、標本の粗さではなく二分探索で決める。
        if (runs.size() > 1) {
            const SampleRun& previous = runs[(index + runs.size() - 1) % runs.size()];
            const SampleRun& next = runs[(index + 1) % runs.size()];
            if (closed || index > 0) {
                start = RefineBoundary(surfaces, curves, direction, start, start - step,
                    run.surface, closed);
                (void)previous;
            }
            if (closed || index + 1 < runs.size()) {
                end = RefineBoundary(surfaces, curves, direction, end, end + step,
                    run.surface, closed);
                (void)next;
            }
        }
        auto projected = ProjectRange(surfaces[run.surface], curves, direction, start, end,
            closed, toleranceMm);
        if (!projected.HasValue()) {
            return Out::Failure(projected.Diagnostics());
        }
        WrapProjectionRun result;
        result.surfaceIndex = run.surface;
        result.startParameter = start;
        result.endParameter = end;
        result.curves = std::move(projected.Value());
        result.closed = closed && runs.size() == 1;
        made.push_back(std::move(result));
    }
    return Out::Success(std::move(made));
}

} // namespace kachakacha::v2::fabrication
