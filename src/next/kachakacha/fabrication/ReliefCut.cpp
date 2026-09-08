#include "kachakacha/fabrication/ReliefCut.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

namespace kachakacha::v2::fabrication {

base::Result<ReliefDirectionChoice> ChooseReliefDirection(
    const FabricationSettings& settings, const PanelCurvature& curvature)
{
    using Out = base::Result<ReliefDirectionChoice>;
    const auto finite = [](double value) {
        return geometry::IsFinite(value) && value >= 0.0;
    };
    if (!finite(curvature.alongUPerMm) || !finite(curvature.alongVPerMm)
        || !finite(curvature.widthUMm) || !finite(curvature.widthVMm)) {
        return Out::Failure(base::MakeError("FAB-C007",
            "切れ目の向きを決められません。",
            "部材の曲がり具合か大きさに、数値でない値か負の値が入っています。"));
    }
    ReliefDirectionChoice choice;
    switch (settings.reliefDirection) {
    case BendDirection::U:
        choice.direction = BendDirection::U;
        choice.reasonJa = "利用者が U 方向を指定しました。";
        return Out::Success(std::move(choice));
    case BendDirection::V:
        choice.direction = BendDirection::V;
        choice.reasonJa = "利用者が V 方向を指定しました。";
        return Out::Success(std::move(choice));
    case BendDirection::Both:
        choice.direction = BendDirection::Both;
        choice.evaluatesBoth = true;
        choice.reasonJa = "利用者が U と V の両方を指定しました。両方を候補にします。";
        return Out::Success(std::move(choice));
    case BendDirection::Auto:
        break;
    }

    // ここから自動。曲がっている向きに沿って切っても、そこは開かない。
    // 切れ目は「曲率の大きい向きへ直交して」進めると、いちばんよく開く。
    const double curveU = curvature.alongUPerMm;
    const double curveV = curvature.alongVPerMm;
    const double total = curveU + curveV;
    if (total <= 0.0) {
        // どちらにも曲がっていない。平らな部材に切れ目は要らないが、
        // 決めろと言われたら、細くない方(千切れにくい方)を返す。
        choice.direction = curvature.widthUMm >= curvature.widthVMm ? BendDirection::U
                                                                   : BendDirection::V;
        choice.reasonJa = "どちらにも曲がっていないので、幅の広い方向を選びました。";
        return Out::Success(std::move(choice));
    }
    // 二重曲率。両方が同じくらいなら、片方に決め打ちしない。
    const double larger = std::max(curveU, curveV);
    const double smaller = std::min(curveU, curveV);
    if (smaller > 0.0 && smaller / larger >= 0.6) {
        choice.direction = BendDirection::Both;
        choice.evaluatesBoth = true;
        choice.reasonJa = "U と V の曲がりが同じくらい("
            + std::to_string(curveU) + " と " + std::to_string(curveV)
            + " /mm)なので、両方を候補にします。";
        return Out::Success(std::move(choice));
    }
    // 曲率の大きい向きへ直交して切る。
    if (curveU > curveV) {
        choice.direction = BendDirection::V;
        choice.reasonJa = "U 方向のほうが強く曲がっている("
            + std::to_string(curveU) + " > " + std::to_string(curveV)
            + " /mm)ので、それに直交する V 方向へ切れ目を進めます。";
    } else {
        choice.direction = BendDirection::U;
        choice.reasonJa = "V 方向のほうが強く曲がっている("
            + std::to_string(curveV) + " > " + std::to_string(curveU)
            + " /mm)ので、それに直交する U 方向へ切れ目を進めます。";
    }
    return Out::Success(std::move(choice));
}

base::Result<std::vector<Point2>> ReliefAdvanceDirections(
    const ReliefDirectionChoice& choice)
{
    using Out = base::Result<std::vector<Point2>>;
    switch (choice.direction) {
    case BendDirection::U:
        return Out::Success(std::vector<Point2>{Point2{1.0, 0.0}});
    case BendDirection::V:
        return Out::Success(std::vector<Point2>{Point2{0.0, 1.0}});
    case BendDirection::Both:
        // 片方へ寄せない。両方をそのまま候補として返す。
        return Out::Success(std::vector<Point2>{Point2{1.0, 0.0}, Point2{0.0, 1.0}});
    case BendDirection::Auto:
        break;
    }
    return Out::Failure(base::MakeError("FAB-C007",
        "切れ目の向きを決められません。",
        "自動のまま渡されました。先に ChooseReliefDirection を通してください。"));
}

} // namespace kachakacha::v2::fabrication

namespace kachakacha::v2::fabrication {
namespace {

//! 点列の、端から測った長さ。
[[nodiscard]] std::vector<double> ArcLengths(const std::vector<Point2>& path)
{
    std::vector<double> lengths(path.size(), 0.0);
    for (std::size_t at = 1; at < path.size(); ++at) {
        const double du = path[at].u - path[at - 1].u;
        const double dv = path[at].v - path[at - 1].v;
        lengths[at] = lengths[at - 1] + std::hypot(du, dv);
    }
    return lengths;
}

//! 正規化した弧長 t のところの、実際の長さ。
[[nodiscard]] double LengthAt(const std::vector<double>& lengths, double t)
{
    if (lengths.size() < 2 || lengths.back() <= 0.0) {
        return 0.0;
    }
    return lengths.back() * t;
}

} // namespace

base::Result<ReliefMateCorrespondence> CorrespondReliefSides(const ReliefCut& cut,
    double targetMaxGapMm)
{
    using Out = base::Result<ReliefMateCorrespondence>;
    if (!(targetMaxGapMm >= 0.0) || !geometry::IsFinite(targetMaxGapMm)) {
        return Out::Failure(base::MakeError("FAB-C006",
            "組み立てたときの隙間が目標を超えます。",
            "目標の隙間が正しい数ではありません。"));
    }
    ReliefMateCorrespondence result;
    result.shape = cut.shape;

    // 直線の切れ目は、左右が同じ線である。閉じれば隙間は残らない。
    std::vector<Point2> left = cut.leftSide;
    std::vector<Point2> right = cut.rightSide;
    if (cut.shape == ReliefShape::StraightSlit) {
        if (!left.empty() || !right.empty()) {
            return Out::Failure(base::MakeError("FAB-C006",
                "組み立てたときの隙間が目標を超えます。",
                "直線の切れ目に左右の側辺が入っています。"));
        }
        left = cut.centerPath;
        right = cut.centerPath;
    }
    if (left.size() < 2 || right.size() < 2) {
        return Out::Failure(base::MakeError("FAB-C006",
            "組み立てたときの隙間が目標を超えます。",
            "左右の辺の点が足りません。それぞれ2点以上要ります。"));
    }
    for (const std::vector<Point2>* side : {&left, &right}) {
        for (const Point2& point : *side) {
            if (!geometry::IsFinite(point.u) || !geometry::IsFinite(point.v)) {
                return Out::Failure(base::MakeError("FAB-C006",
                    "組み立てたときの隙間が目標を超えます。",
                    "側辺に数値でない点があります。"));
            }
        }
    }

    const std::vector<double> leftLengths = ArcLengths(left);
    const std::vector<double> rightLengths = ArcLengths(right);
    result.leftLengthMm = leftLengths.back();
    result.rightLengthMm = rightLengths.back();
    if (result.leftLengthMm <= 0.0 || result.rightLengthMm <= 0.0) {
        return Out::Failure(base::MakeError("FAB-C006",
            "組み立てたときの隙間が目標を超えます。", "長さの無い側辺があります。"));
    }

    // 正規化弧長で対応を取る。左右の点数が違っても同じ決め方で済む。
    constexpr std::size_t kSamples = 33;
    for (std::size_t at = 0; at < kSamples; ++at) {
        const double t = static_cast<double>(at) / static_cast<double>(kSamples - 1);
        result.leftParameters.push_back(t);
        result.rightParameters.push_back(t);
        // 組立100%で合わさるとき、対応する点までの道のりの差がそのまま隙間になる。
        // 片方を縮めて合わせない。差が出るなら、それが本当の差である。
        const double gap =
            std::abs(LengthAt(leftLengths, t) - LengthAt(rightLengths, t));
        result.maximumGapMm = std::max(result.maximumGapMm, gap);
    }

    if (result.maximumGapMm > targetMaxGapMm) {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer),
            "左辺 %.4f mm、右辺 %.4f mm。組み立てると最大 %.4f mm 離れます"
            "(目標は %.4f mm)。片方の辺だけを縮めて合わせることはしません。",
            result.leftLengthMm, result.rightLengthMm, result.maximumGapMm,
            targetMaxGapMm);
        return Out::Failure(base::MakeError("FAB-C006",
            "組み立てたときの隙間が目標を超えます。", buffer));
    }
    return Out::Success(std::move(result));
}

} // namespace kachakacha::v2::fabrication
