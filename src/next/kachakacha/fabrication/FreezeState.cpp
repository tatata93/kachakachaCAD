#include "kachakacha/fabrication/FreezeState.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace kachakacha::v2::fabrication {

using base::Diagnostic;
using base::MakeError;
using base::MakeWarning;
using base::Result;
using geometry::Vector3;

namespace {

constexpr const char* kBadInput = "FAB-F001";
constexpr const char* kNothingSelected = "FAB-F002";
constexpr const char* kThicknessRequired = "FAB-E001";

//! 3次元の点列を、直線の並びとして曲線にする。
//! 元が直線でできた輪郭なので、ここで曲線種類が落ちることはない。
[[nodiscard]] std::vector<CurveSegment> PolylineToSegments(
    const std::vector<Vector3>& points, bool closed)
{
    std::vector<CurveSegment> segments;
    if (points.size() < 2) {
        return segments;
    }
    for (std::size_t index = 0; index + 1 < points.size(); ++index) {
        auto made = CurveSegment::MakeLine(points[index], points[index + 1]);
        if (made.HasValue()) {
            segments.push_back(made.Value());
        }
    }
    if (closed && points.size() >= 3) {
        auto made = CurveSegment::MakeLine(points.back(), points.front());
        if (made.HasValue()) {
            segments.push_back(made.Value());
        }
    }
    return segments;
}

[[nodiscard]] double TotalLength(const std::vector<CurveSegment>& segments)
{
    double total = 0.0;
    for (const CurveSegment& segment : segments) {
        total += segment.TotalLength(1.0e-6);
    }
    return total;
}

[[nodiscard]] double DiagonalOf(const std::vector<FrozenWire>& wires)
{
    bool any = false;
    Vector3 minimum{};
    Vector3 maximum{};
    for (const FrozenWire& wire : wires) {
        for (const CurveSegment& segment : wire.segments) {
            for (const Vector3& point : {segment.StartPoint(), segment.EndPoint()}) {
                if (!any) {
                    minimum = point;
                    maximum = point;
                    any = true;
                    continue;
                }
                minimum.x = std::min(minimum.x, point.x);
                minimum.y = std::min(minimum.y, point.y);
                minimum.z = std::min(minimum.z, point.z);
                maximum.x = std::max(maximum.x, point.x);
                maximum.y = std::max(maximum.y, point.y);
                maximum.z = std::max(maximum.z, point.z);
            }
        }
    }
    return any ? (maximum - minimum).Length() : 0.0;
}

//! 折り線の両端を、置いた板の上で見つける。
//! 型紙の折り線の端点に一番近い輪郭の頂点を使う。
[[nodiscard]] bool FoldEndsInSpace(const AssemblyPanel& flat, const PlacedPanel& placed,
    const geometry::Point2& from, const geometry::Point2& to, Vector3& fromOut,
    Vector3& toOut)
{
    if (flat.flatOutline.size() != placed.outline.size() || flat.flatOutline.empty()) {
        return false;
    }
    const auto nearest = [&](const geometry::Point2& target) {
        std::size_t best = 0;
        double bestDistance = -1.0;
        for (std::size_t index = 0; index < flat.flatOutline.size(); ++index) {
            const double du = flat.flatOutline[index].u - target.u;
            const double dv = flat.flatOutline[index].v - target.v;
            const double distance = du * du + dv * dv;
            if (bestDistance < 0.0 || distance < bestDistance) {
                bestDistance = distance;
                best = index;
            }
        }
        return best;
    };
    fromOut = placed.outline[nearest(from)];
    toOut = placed.outline[nearest(to)];
    return true;
}

} // namespace

std::string_view FreezeOutputNameJa(FreezeOutput value) noexcept
{
    switch (value) {
    case FreezeOutput::WiresOnly: return "ワイヤーのみ";
    case FreezeOutput::PartsOnly: return "部品のみ";
    case FreezeOutput::Both:      return "両方";
    }
    return "不明";
}

std::string_view FrozenWireKindNameJa(FrozenWireKind value) noexcept
{
    switch (value) {
    case FrozenWireKind::PanelBoundary: return "部材の境界";
    case FrozenWireKind::FoldLine:      return "折り線";
    case FrozenWireKind::ReliefCut:     return "切れ目";
    case FrozenWireKind::Opening:       return "開口";
    }
    return "不明";
}

Result<FreezeBundle> FreezeAssemblyState(const std::vector<AssemblyPanel>& panels,
    const std::vector<AssemblyFold>& folds, const AssemblyState& state,
    FreezeOutput output, const FabricationSettings& settings)
{
    if (panels.empty()) {
        return Result<FreezeBundle>::Failure(MakeError(kBadInput,
            "固定する板がありません。", {}));
    }
    if (output == FreezeOutput::PartsOnly || output == FreezeOutput::Both) {
        if (!(settings.outputThicknessMm > 0.0)) {
            return Result<FreezeBundle>::Failure(MakeError(kThicknessRequired,
                "厚みが指定されていません。",
                "部品を作るには厚みが要ります。ワイヤーだけなら厚みは要りません。"));
        }
    }

    // ここが要。組立の評価は1回だけ行い、ワイヤーも部品もこの結果から作る。
    auto placed = EvaluateAssembly(panels, folds, state);
    if (!placed.HasValue()) {
        return Result<FreezeBundle>::Failure(placed.Diagnostics());
    }

    FreezeBundle bundle;
    bundle.percent = state.masterPercent;
    bundle.output = output;

    std::map<std::string, const AssemblyPanel*> flatById;
    for (const AssemblyPanel& panel : panels) {
        flatById[panel.panelId] = &panel;
    }

    const bool wantWires =
        output == FreezeOutput::WiresOnly || output == FreezeOutput::Both;
    const bool wantParts =
        output == FreezeOutput::PartsOnly || output == FreezeOutput::Both;

    for (const PlacedPanel& panel : placed.Value().panels) {
        if (wantWires) {
            FrozenWire wire;
            wire.sourceId = panel.panelId;
            wire.kind = FrozenWireKind::PanelBoundary;
            wire.segments = PolylineToSegments(panel.outline, true);
            wire.lengthMm = TotalLength(wire.segments);
            bundle.wires.push_back(std::move(wire));
        }
        if (wantParts) {
            PanelSolidRequest request;
            request.panelId = panel.panelId;
            // 部品の輪郭は、ワイヤーと同じ点から作る。別々に計算しない。
            request.outline = panel.outline;
            request.thicknessMm = settings.outputThicknessMm;
            request.placement = settings.thicknessPlacement;
            bundle.parts.push_back(std::move(request));
        }
    }

    if (wantWires) {
        for (const AssemblyFold& fold : folds) {
            const auto flat = flatById.find(fold.childPanelId);
            if (flat == flatById.end()) {
                continue;
            }
            const auto found = std::find_if(placed.Value().panels.begin(),
                placed.Value().panels.end(), [&fold](const PlacedPanel& item) {
                    return item.panelId == fold.childPanelId;
                });
            if (found == placed.Value().panels.end()) {
                continue;
            }
            Vector3 from{};
            Vector3 to{};
            if (!FoldEndsInSpace(*flat->second, *found, fold.hingeFrom, fold.hingeTo,
                    from, to)) {
                continue;
            }
            auto made = CurveSegment::MakeLine(from, to);
            if (!made.HasValue()) {
                continue;
            }
            FrozenWire wire;
            wire.sourceId = fold.foldId;
            wire.kind = FrozenWireKind::FoldLine;
            wire.segments = {made.Value()};
            wire.lengthMm = TotalLength(wire.segments);
            bundle.wires.push_back(std::move(wire));
        }
    }

    if (bundle.wires.empty() && bundle.parts.empty()) {
        return Result<FreezeBundle>::Failure(MakeError(kNothingSelected,
            "固定するものがありません。",
            "ワイヤー・部品のどちらかを選んでください。"));
    }

    std::vector<Diagnostic> warnings = bundle.notes;
    return Result<FreezeBundle>::Success(std::move(bundle), std::move(warnings));
}

FreezeComparison CompareFrozenStates(const FreezeBundle& first,
    const FreezeBundle& second, double toleranceMm)
{
    FreezeComparison comparison;
    std::map<std::string, double> lengths;
    for (const FrozenWire& wire : first.wires) {
        lengths[wire.sourceId] = wire.lengthMm;
    }
    for (const FrozenWire& wire : second.wires) {
        const auto found = lengths.find(wire.sourceId);
        if (found == lengths.end()) {
            continue;
        }
        const double difference = std::abs(found->second - wire.lengthMm);
        if (difference > comparison.maximumLengthDifferenceMm) {
            comparison.maximumLengthDifferenceMm = difference;
            comparison.worstSourceId = wire.sourceId;
        }
    }
    comparison.lengthsMatch =
        comparison.maximumLengthDifferenceMm <= std::max(toleranceMm, 1.0e-9);
    comparison.firstDiagonalMm = DiagonalOf(first.wires);
    comparison.secondDiagonalMm = DiagonalOf(second.wires);
    comparison.shapesDiffer =
        std::abs(comparison.firstDiagonalMm - comparison.secondDiagonalMm)
        > std::max(toleranceMm, 1.0e-9);
    return comparison;
}

BoundaryAgreement CheckBoundaryAgreement(const FreezeBundle& bundle, double toleranceMm)
{
    BoundaryAgreement agreement;
    std::map<std::string, const FrozenWire*> boundaries;
    for (const FrozenWire& wire : bundle.wires) {
        if (wire.kind == FrozenWireKind::PanelBoundary) {
            boundaries[wire.sourceId] = &wire;
        }
    }
    for (const PanelSolidRequest& part : bundle.parts) {
        const auto found = boundaries.find(part.panelId);
        if (found == boundaries.end()) {
            continue;
        }
        // ワイヤーの端点と、部品の輪郭の点を突き合わせる。
        // 同じ束から作っているので、ぴったり同じでなければならない。
        std::vector<Vector3> wirePoints;
        for (const CurveSegment& segment : found->second->segments) {
            wirePoints.push_back(segment.StartPoint());
        }
        const std::size_t count = std::min(wirePoints.size(), part.outline.size());
        for (std::size_t index = 0; index < count; ++index) {
            const double distance = (wirePoints[index] - part.outline[index]).Length();
            if (distance > agreement.maximumDeviationMm) {
                agreement.maximumDeviationMm = distance;
                agreement.worstPanelId = part.panelId;
            }
        }
    }
    agreement.agrees = agreement.maximumDeviationMm <= std::max(toleranceMm, 1.0e-12);
    return agreement;
}

} // namespace kachakacha::v2::fabrication
