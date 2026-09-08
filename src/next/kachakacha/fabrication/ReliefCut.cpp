#include "kachakacha/fabrication/ReliefCut.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::fabrication {

using base::Diagnostic;
using base::MakeError;
using base::Result;
using geometry::Vector3;

namespace {

constexpr const char* kDisabledButRequired = "FAB-C001";
constexpr const char* kTooDeep = "FAB-C002";
constexpr const char* kIntersectsOpening = "FAB-C003";
constexpr const char* kCutsIntersect = "FAB-C004";
constexpr const char* kLigamentTooSmall = "FAB-C005";
constexpr const char* kMateGapExceeded = "FAB-C006";
constexpr const char* kBadInput = "FAB-C009";

[[nodiscard]] std::vector<Vector3> Lift(const std::vector<Point2>& points)
{
    std::vector<Vector3> lifted;
    lifted.reserve(points.size());
    for (const Point2& point : points) {
        lifted.push_back(Vector3{point.u, point.v, 0.0});
    }
    return lifted;
}

//! 閉じた輪として持ち上げる(最後と最初を繋ぐ)。
[[nodiscard]] std::vector<Vector3> LiftClosed(const std::vector<Point2>& points)
{
    std::vector<Vector3> lifted = Lift(points);
    if (!lifted.empty()) {
        lifted.push_back(lifted.front());
    }
    return lifted;
}

[[nodiscard]] double PathLength(const std::vector<Point2>& points)
{
    double total = 0.0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        const double du = points[index].u - points[index - 1].u;
        const double dv = points[index].v - points[index - 1].v;
        total += std::sqrt(du * du + dv * dv);
    }
    return total;
}

//! 点から外周までの最短距離。
[[nodiscard]] double DistanceToOutline(const std::vector<Point2>& outline, const Point2& point)
{
    const geometry::PolylineApproach approach = geometry::ClosestApproachBetween(
        {Vector3{point.u, point.v, 0.0}}, LiftClosed(outline));
    return approach.valid ? approach.distanceMm : 0.0;
}

//! 切れ目が入る場所での部材の幅。入口の点から反対側までを測る。
[[nodiscard]] double LocalPanelWidth(const std::vector<Point2>& outline, const ReliefCut& cut)
{
    if (cut.centerPath.size() < 2) {
        return 0.0;
    }
    // 切れ目の向きに沿って、入口から反対側の縁まで。
    const Point2& entry = cut.centerPath.front();
    const Point2& tip = cut.centerPath.back();
    const double du = tip.u - entry.u;
    const double dv = tip.v - entry.v;
    const double length = std::sqrt(du * du + dv * dv);
    if (!(length > 0.0)) {
        return 0.0;
    }
    const Point2 direction{du / length, dv / length};
    // 入口から向きの方へ十分に伸ばした線が、外周と交わる最も遠い点までの距離。
    const double reach = 1.0e6;
    const Vector3 from{entry.u, entry.v, 0.0};
    const Vector3 to{entry.u + direction.u * reach, entry.v + direction.v * reach, 0.0};
    double farthest = 0.0;
    const std::vector<Vector3> loop = LiftClosed(outline);
    for (std::size_t index = 1; index < loop.size(); ++index) {
        const geometry::PolylineApproach approach =
            geometry::ClosestApproachBetween({from, to}, {loop[index - 1], loop[index]});
        if (!approach.valid || approach.distanceMm > 1.0e-6) {
            continue;
        }
        const double distance = (approach.firstPoint - from).Length();
        farthest = std::max(farthest, distance);
    }
    return farthest;
}

} // namespace

std::optional<Diagnostic> CheckReliefRequired(const FabricationSettings& settings,
    bool reliefWouldBeNeeded)
{
    if (reliefWouldBeNeeded && !settings.reliefCutsEnabled) {
        return MakeError(kDisabledButRequired,
            "この形は切れ目なしでは作れません。",
            "切れ目を使うか、部材をもっと細かく分けてください。");
    }
    return std::nullopt;
}

Result<ReliefValidation> ValidateReliefCuts(const ReliefPanel& panel,
    const std::vector<ReliefCut>& cuts, const FabricationSettings& settings,
    double targetMaxDeviationMm)
{
    if (panel.outline.size() < 3) {
        return Result<ReliefValidation>::Failure(MakeError(kBadInput,
            "部材の外周が足りません。", panel.panelId));
    }
    std::vector<Diagnostic> errors;
    ReliefValidation validation;
    validation.minimumLigamentMm = -1.0;

    for (const ReliefCut& cut : cuts) {
        if (cut.centerPath.size() < 2) {
            errors.push_back(MakeError(kBadInput, "切れ目の線が足りません。", cut.cutId));
            continue;
        }
        for (const Point2& point : cut.centerPath) {
            if (!geometry::IsFinite(point.u) || !geometry::IsFinite(point.v)) {
                errors.push_back(MakeError(kBadInput,
                    "切れ目に有限でない数が入っています。", cut.cutId));
            }
        }
        const double depth = PathLength(cut.centerPath);
        const double width = LocalPanelWidth(panel.outline, cut);
        if (width > 0.0) {
            const double ratio = depth / width;
            validation.maximumDepthRatio = std::max(validation.maximumDepthRatio, ratio);
            if (ratio > settings.maximumReliefDepthRatio) {
                errors.push_back(MakeError(kTooDeep, "切れ目が深すぎます。",
                    cut.cutId + ": 深さ " + std::to_string(depth) + " mm は幅 "
                        + std::to_string(width) + " mm の "
                        + std::to_string(ratio * 100.0) + "%。上限は "
                        + std::to_string(settings.maximumReliefDepthRatio * 100.0) + "%。"));
            }
        }

        // 先端から向こう側の縁まで、残す幅があるか。
        const double ligament = DistanceToOutline(panel.outline, cut.centerPath.back());
        if (validation.minimumLigamentMm < 0.0 || ligament < validation.minimumLigamentMm) {
            validation.minimumLigamentMm = ligament;
        }
        if (ligament < settings.minimumLigamentMm) {
            errors.push_back(MakeError(kLigamentTooSmall, "切れ目の先で材料が残りません。",
                cut.cutId + ": 残り " + std::to_string(ligament) + " mm、必要 "
                    + std::to_string(settings.minimumLigamentMm) + " mm。"));
        }

        // 開口へ入り込んでいないか。
        for (const std::vector<Point2>& opening : panel.openings) {
            if (opening.size() < 3) {
                continue;
            }
            const geometry::PolylineApproach approach =
                geometry::ClosestApproachBetween(Lift(cut.centerPath), LiftClosed(opening));
            const bool touches = approach.valid && approach.distanceMm <= 1.0e-9;
            const bool inside = geometry::ContainsPoint(opening, cut.centerPath.back());
            if (touches || inside) {
                errors.push_back(MakeError(kIntersectsOpening,
                    "切れ目が開口へ入り込んでいます。", cut.cutId));
                break;
            }
        }

        // 組立後に左右が合うか。片側だけ詰めて合わせない(§7.3)。
        validation.maximumMateGapMm = std::max(validation.maximumMateGapMm, cut.mateGapMm);
        if (cut.mateGapMm > targetMaxDeviationMm) {
            errors.push_back(MakeError(kMateGapExceeded,
                "組み立てたときに切れ目の左右が合いません。",
                cut.cutId + ": 隙間 " + std::to_string(cut.mateGapMm) + " mm、目標は "
                    + std::to_string(targetMaxDeviationMm) + " mm。"));
        }
        validation.acceptedCutIds.push_back(cut.cutId);
    }

    // 切れ目どうしが交わっていないか。交わると小片が落ちる。
    for (std::size_t a = 0; a < cuts.size(); ++a) {
        for (std::size_t b = a + 1; b < cuts.size(); ++b) {
            if (cuts[a].centerPath.size() < 2 || cuts[b].centerPath.size() < 2) {
                continue;
            }
            const geometry::PolylineApproach approach = geometry::ClosestApproachBetween(
                Lift(cuts[a].centerPath), Lift(cuts[b].centerPath));
            if (approach.valid && approach.distanceMm <= settings.minimumLigamentMm) {
                errors.push_back(MakeError(kCutsIntersect,
                    "切れ目どうしが近すぎます。",
                    cuts[a].cutId + " と " + cuts[b].cutId + ": 距離 "
                        + std::to_string(approach.distanceMm) + " mm、必要 "
                        + std::to_string(settings.minimumLigamentMm) + " mm。"));
            }
        }
    }

    if (validation.minimumLigamentMm < 0.0) {
        validation.minimumLigamentMm = 0.0;
    }
    if (!errors.empty()) {
        return Result<ReliefValidation>::Failure(std::move(errors));
    }
    return Result<ReliefValidation>::Success(std::move(validation));
}

} // namespace kachakacha::v2::fabrication
