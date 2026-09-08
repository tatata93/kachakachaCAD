#include "kachakacha/fabrication/Assembly.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace kachakacha::v2::fabrication {

using base::Diagnostic;
using base::MakeError;
using base::MakeWarning;
using base::Result;
using geometry::Normalized;

namespace {

constexpr const char* kSelfIntersection = "FAB-A002";
constexpr const char* kMetricDistortion = "FAB-A003";
constexpr const char* kBadInput = "FAB-A004";
constexpr const char* kNotConnected = "FAB-A005";

//! 剛体変換。回転行列と平行移動。伸縮もせん断も表せない形にしておく。
//! これが「辺の長さが変わりようがない」ことの根拠になる。
struct RigidTransform {
    Vector3 columnX{1.0, 0.0, 0.0};
    Vector3 columnY{0.0, 1.0, 0.0};
    Vector3 columnZ{0.0, 0.0, 1.0};
    Vector3 translation{};

    [[nodiscard]] Vector3 Apply(const Vector3& point) const
    {
        return columnX * point.x + columnY * point.y + columnZ * point.z + translation;
    }
    [[nodiscard]] Vector3 ApplyDirection(const Vector3& direction) const
    {
        return columnX * direction.x + columnY * direction.y + columnZ * direction.z;
    }
};

[[nodiscard]] RigidTransform Compose(const RigidTransform& outer, const RigidTransform& inner)
{
    RigidTransform result;
    result.columnX = outer.ApplyDirection(inner.columnX);
    result.columnY = outer.ApplyDirection(inner.columnY);
    result.columnZ = outer.ApplyDirection(inner.columnZ);
    result.translation = outer.Apply(inner.translation);
    return result;
}

//! 軸まわりの回転。軸は点 origin を通り、向き direction(単位)。
[[nodiscard]] RigidTransform RotationAbout(const Vector3& origin, const Vector3& direction,
    double angleRad)
{
    const Vector3 axis = Normalized(direction);
    const double c = std::cos(angleRad);
    const double s = std::sin(angleRad);
    const double t = 1.0 - c;
    RigidTransform rotation;
    rotation.columnX = {t * axis.x * axis.x + c, t * axis.x * axis.y + s * axis.z,
        t * axis.x * axis.z - s * axis.y};
    rotation.columnY = {t * axis.x * axis.y - s * axis.z, t * axis.y * axis.y + c,
        t * axis.y * axis.z + s * axis.x};
    rotation.columnZ = {t * axis.x * axis.z + s * axis.y, t * axis.y * axis.z - s * axis.x,
        t * axis.z * axis.z + c};
    // 原点を通るように平行移動を足す。
    const Vector3 rotated = rotation.ApplyDirection(origin);
    rotation.translation = origin - rotated;
    return rotation;
}

[[nodiscard]] Vector3 Lift(const Point2& point)
{
    return Vector3{point.u, point.v, 0.0};
}

//! 曲げる板を、母線で区切った細い帯へ分ける。
//! 帯どうしは母線をヒンジにして繋がる。こうすると曲げも剛体の連なりになる。
struct BentPanelPieces {
    std::vector<std::vector<Point2>> strips;
    std::vector<double> hingeAngleRad;   //!< strips.size() - 1 個
    std::vector<std::pair<Point2, Point2>> hinges;
};

[[nodiscard]] BentPanelPieces SplitByRulings(const AssemblyPanel& panel)
{
    BentPanelPieces pieces;
    if (panel.rulingUCoordinates.empty()) {
        pieces.strips.push_back(panel.flatOutline);
        return pieces;
    }
    // 輪郭の u 範囲を母線で区切る。輪郭は凸でなくてもよいが、
    // ここでは母線が輪郭を2点で横切る形(帯状の板)を前提にする。
    double minimumV = panel.flatOutline.front().v;
    double maximumV = minimumV;
    double minimumU = panel.flatOutline.front().u;
    double maximumU = minimumU;
    for (const Point2& point : panel.flatOutline) {
        minimumV = std::min(minimumV, point.v);
        maximumV = std::max(maximumV, point.v);
        minimumU = std::min(minimumU, point.u);
        maximumU = std::max(maximumU, point.u);
    }
    std::vector<double> cuts;
    cuts.push_back(minimumU);
    for (const double u : panel.rulingUCoordinates) {
        cuts.push_back(u);
    }
    cuts.push_back(maximumU);
    std::sort(cuts.begin(), cuts.end());

    for (std::size_t index = 0; index + 1 < cuts.size(); ++index) {
        const double u0 = cuts[index];
        const double u1 = cuts[index + 1];
        pieces.strips.push_back({Point2{u0, minimumV}, Point2{u1, minimumV},
            Point2{u1, maximumV}, Point2{u0, maximumV}});
        if (index + 2 < cuts.size()) {
            pieces.hinges.emplace_back(Point2{u1, minimumV}, Point2{u1, maximumV});
            const double angle = index < panel.targetBendAngleRad.size()
                ? panel.targetBendAngleRad[index]
                : 0.0;
            pieces.hingeAngleRad.push_back(angle);
        }
    }
    return pieces;
}

//! 型紙の上の点を、置いたあとの3次元の点へ写す。
[[nodiscard]] Vector3 Place(const RigidTransform& transform, const Point2& point)
{
    return transform.Apply(Lift(point));
}

[[nodiscard]] double PolylineLength(const std::vector<Vector3>& points)
{
    double total = 0.0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        total += (points[index] - points[index - 1]).Length();
    }
    return total;
}

} // namespace

Result<AssemblyResult> EvaluateAssembly(const std::vector<AssemblyPanel>& panels,
    const std::vector<AssemblyFold>& folds, const AssemblyState& state)
{
    if (panels.empty()) {
        return Result<AssemblyResult>::Failure(MakeError(kBadInput,
            "組み立てる板がありません。", {}));
    }
    std::map<std::string, const AssemblyPanel*> byId;
    for (const AssemblyPanel& panel : panels) {
        if (panel.panelId.empty()) {
            return Result<AssemblyResult>::Failure(MakeError(kBadInput,
                "名前の無い板があります。", {}));
        }
        if (panel.flatOutline.size() < 3) {
            return Result<AssemblyResult>::Failure(MakeError(kBadInput,
                "板の輪郭が足りません。", panel.panelId));
        }
        if (!byId.emplace(panel.panelId, &panel).second) {
            return Result<AssemblyResult>::Failure(MakeError(kBadInput,
                "同じ名前の板が2つあります。", panel.panelId));
        }
    }
    for (const AssemblyFold& fold : folds) {
        if (byId.count(fold.parentPanelId) == 0 || byId.count(fold.childPanelId) == 0) {
            return Result<AssemblyResult>::Failure(MakeError(kBadInput,
                "折り線が、無い板を指しています。", fold.foldId));
        }
        if (fold.parentPanelId == fold.childPanelId) {
            return Result<AssemblyResult>::Failure(MakeError(kBadInput,
                "折り線の両側が同じ板です。", fold.foldId));
        }
        if ((Lift(fold.hingeTo) - Lift(fold.hingeFrom)).Length() <= 0.0) {
            return Result<AssemblyResult>::Failure(MakeError(kBadInput,
                "長さが0の折り線があります。", fold.foldId));
        }
    }

    // 折り線の木を作る。閉じた輪があると、剛体だけでは閉じない場合がある(§10.3)。
    // ここでは木として順に変換し、輪はあとで残差として測る。
    std::map<std::string, std::vector<const AssemblyFold*>> children;
    std::set<std::string> hasParent;
    for (const AssemblyFold& fold : folds) {
        if (hasParent.count(fold.childPanelId) > 0) {
            // 同じ板に親が2つ。木にならない。閉じた輪として扱う。
            continue;
        }
        children[fold.parentPanelId].push_back(&fold);
        hasParent.insert(fold.childPanelId);
    }
    std::vector<std::string> roots;
    for (const AssemblyPanel& panel : panels) {
        if (hasParent.count(panel.panelId) == 0) {
            roots.push_back(panel.panelId);
        }
    }
    if (roots.empty()) {
        return Result<AssemblyResult>::Failure(MakeError(kNotConnected,
            "折り線が輪になっていて、始まりの板が決まりません。",
            "どこか1枚を固定してください。"));
    }

    const double master = std::clamp(state.masterPercent, 0.0, 100.0) / 100.0;
    AssemblyResult result;
    std::map<std::string, RigidTransform> transforms;

    // 木を上からたどって、板ごとの剛体変換を決める。
    std::vector<std::string> queue = roots;
    std::set<std::string> placed;
    for (const std::string& root : roots) {
        transforms[root] = RigidTransform{};
        placed.insert(root);
    }
    while (!queue.empty()) {
        const std::string current = queue.back();
        queue.pop_back();
        const auto found = children.find(current);
        if (found == children.end()) {
            continue;
        }
        for (const AssemblyFold* fold : found->second) {
            const double progress = fold->progressPercentOverride.has_value()
                ? std::clamp(*fold->progressPercentOverride, 0.0, 100.0) / 100.0
                : master;
            const double angle = fold->targetAngleRad * progress;
            // ヒンジは、親を置いたあとの位置で取る。
            const RigidTransform& parent = transforms[current];
            const Vector3 axisFrom = Place(parent, fold->hingeFrom);
            const Vector3 axisTo = Place(parent, fold->hingeTo);
            const RigidTransform rotation =
                RotationAbout(axisFrom, axisTo - axisFrom, angle);
            transforms[fold->childPanelId] = Compose(rotation, parent);
            placed.insert(fold->childPanelId);
            queue.push_back(fold->childPanelId);
        }
    }
    for (const AssemblyPanel& panel : panels) {
        if (placed.count(panel.panelId) == 0) {
            // 木に入らなかった板。輪の一部。ここでは親の変換を持たないので、
            // そのまま平らに置いて、閉じていないことを伝える。
            transforms[panel.panelId] = RigidTransform{};
            result.notes.push_back(MakeWarning("FAB-A001",
                "折り線が輪になっている板があります。",
                panel.panelId + "。この板は動かしていません。"));
        }
    }

    for (const AssemblyPanel& panel : panels) {
        PlacedPanel output;
        output.panelId = panel.panelId;
        const RigidTransform& base = transforms[panel.panelId];

        if (panel.rulingUCoordinates.empty()) {
            // 平らな板。剛体変換だけで動く。
            for (const Point2& point : panel.flatOutline) {
                output.outline.push_back(Place(base, point));
            }
            output.strips.push_back(output.outline);
        } else {
            // 曲げる板。母線で区切った細い帯を、順に回していく。
            const BentPanelPieces pieces = SplitByRulings(panel);
            std::vector<RigidTransform> stripTransforms(pieces.strips.size(), base);
            for (std::size_t index = 1; index < pieces.strips.size(); ++index) {
                const double angle = (index - 1) < pieces.hingeAngleRad.size()
                    ? pieces.hingeAngleRad[index - 1] * master
                    : 0.0;
                const RigidTransform& previous = stripTransforms[index - 1];
                const Vector3 axisFrom = Place(previous, pieces.hinges[index - 1].first);
                const Vector3 axisTo = Place(previous, pieces.hinges[index - 1].second);
                stripTransforms[index] =
                    Compose(RotationAbout(axisFrom, axisTo - axisFrom, angle), previous);
            }
            for (std::size_t index = 0; index < pieces.strips.size(); ++index) {
                std::vector<Vector3> strip;
                for (const Point2& point : pieces.strips[index]) {
                    strip.push_back(Place(stripTransforms[index], point));
                }
                output.strips.push_back(std::move(strip));
            }
            // 板の輪郭は、各点が属する帯の変換で写す。
            for (const Point2& point : panel.flatOutline) {
                std::size_t strip = 0;
                for (std::size_t index = 0; index < pieces.strips.size(); ++index) {
                    double lowest = pieces.strips[index].front().u;
                    double highest = lowest;
                    for (const Point2& corner : pieces.strips[index]) {
                        lowest = std::min(lowest, corner.u);
                        highest = std::max(highest, corner.u);
                    }
                    if (point.u >= lowest - 1.0e-9 && point.u <= highest + 1.0e-9) {
                        strip = index;
                        break;
                    }
                }
                output.outline.push_back(Place(stripTransforms[strip], point));
            }
        }
        result.panels.push_back(std::move(output));
    }
    return Result<AssemblyResult>::Success(std::move(result));
}

AssemblyMetricCheck CheckAssemblyMetric(const std::vector<AssemblyPanel>& flat,
    const AssemblyResult& placed)
{
    AssemblyMetricCheck check;
    std::map<std::string, const AssemblyPanel*> byId;
    for (const AssemblyPanel& panel : flat) {
        byId[panel.panelId] = &panel;
    }
    for (const PlacedPanel& panel : placed.panels) {
        const auto found = byId.find(panel.panelId);
        if (found == byId.end()) {
            continue;
        }
        const AssemblyPanel& source = *found->second;
        const std::vector<Point2>& outline = source.flatOutline;
        if (outline.size() != panel.outline.size()) {
            check.withinTolerance = false;
            check.worstPanelId = panel.panelId;
            continue;
        }
        const BentPanelPieces pieces = SplitByRulings(source);

        // その点がどの帯に属するか。
        const auto stripOf = [&](const Point2& point) {
            for (std::size_t index = 0; index < pieces.strips.size(); ++index) {
                double lowest = pieces.strips[index].front().u;
                double highest = lowest;
                for (const Point2& corner : pieces.strips[index]) {
                    lowest = std::min(lowest, corner.u);
                    highest = std::max(highest, corner.u);
                }
                if (point.u >= lowest - 1.0e-9 && point.u <= highest + 1.0e-9) {
                    return index;
                }
            }
            return std::size_t{0};
        };

        const auto note = [&](double before, double after) {
            if (!(before > 0.0)) {
                return;
            }
            const double change = std::abs(after - before) / before;
            if (change > check.maximumEdgeLengthChangeRelative) {
                check.maximumEdgeLengthChangeRelative = change;
                check.worstPanelId = panel.panelId;
            }
        };

        // 輪郭の辺。同じ帯の中にある辺は、長さが変わってはならない。
        // 帯をまたぐ辺は、曲げたぶんだけ短く見えるのが正しいので数えない。
        for (std::size_t index = 1; index <= outline.size(); ++index) {
            const std::size_t current = index % outline.size();
            const std::size_t previous = index - 1;
            if (stripOf(outline[current]) != stripOf(outline[previous])) {
                continue;
            }
            const double before = std::sqrt(
                std::pow(outline[current].u - outline[previous].u, 2.0)
                + std::pow(outline[current].v - outline[previous].v, 2.0));
            note(before, (panel.outline[current] - panel.outline[previous]).Length());
        }

        // 帯そのものは剛体で動く。辺も対角線も、1つも変わってはならない。
        for (std::size_t index = 0;
            index < pieces.strips.size() && index < panel.strips.size(); ++index) {
            const std::vector<Point2>& before = pieces.strips[index];
            const std::vector<Vector3>& after = panel.strips[index];
            if (before.size() != after.size()) {
                check.withinTolerance = false;
                check.worstPanelId = panel.panelId;
                continue;
            }
            for (std::size_t a = 0; a < before.size(); ++a) {
                for (std::size_t b = a + 1; b < before.size(); ++b) {
                    const double flatDistance = std::sqrt(
                        std::pow(before[a].u - before[b].u, 2.0)
                        + std::pow(before[a].v - before[b].v, 2.0));
                    const double placedDistance = (after[a] - after[b]).Length();
                    if (!(flatDistance > 0.0)) {
                        continue;
                    }
                    check.maximumScaleError = std::max(check.maximumScaleError,
                        std::abs(placedDistance - flatDistance) / flatDistance);
                }
            }
        }

        // 三角形の裏返り。帯ごとに、面の向きが揃っているかを見る。
        for (const std::vector<Vector3>& strip : panel.strips) {
            if (strip.size() < 3) {
                continue;
            }
            Vector3 reference{};
            bool haveReference = false;
            for (std::size_t index = 1; index + 1 < strip.size(); ++index) {
                const Vector3 normal =
                    Cross(strip[index] - strip[0], strip[index + 1] - strip[0]);
                if (!(normal.Length() > 0.0)) {
                    continue;
                }
                if (!haveReference) {
                    reference = normal;
                    haveReference = true;
                    continue;
                }
                if (Dot(reference, normal) < 0.0) {
                    ++check.flippedTriangleCount;
                }
            }
        }
    }
    check.withinTolerance = check.withinTolerance
        && check.maximumEdgeLengthChangeRelative <= 1.0e-6
        && check.maximumScaleError <= 1.0e-9 && check.flippedTriangleCount == 0;
    if (!check.withinTolerance) {
        // 組み立ては剛体変換と折りだけで行う。長さが変わったなら、
        // どこかで伸び縮みしている。板取りへ流してはならない(§10.2)。
        check.diagnostics.push_back(MakeError(kMetricDistortion,
            "組み立てで板が伸び縮みしています。",
            "辺の長さの変化 " + std::to_string(check.maximumEdgeLengthChangeRelative)
                + " / 拡大縮小の誤差 " + std::to_string(check.maximumScaleError)
                + " / 裏返った三角形 " + std::to_string(check.flippedTriangleCount)
                + " 個。" + (check.worstPanelId.empty()
                    ? std::string() : "最も悪い板 " + check.worstPanelId + "。")));
    }
    return check;
}

AssemblyIntersectionReport FindAssemblyIntersections(const AssemblyResult& placed,
    const std::vector<AssemblyFold>& folds, double toleranceMm)
{
    AssemblyIntersectionReport report;
    // 折り線でつながっている板どうしは、境目で必ず接する。そこは数えない。
    std::set<std::pair<std::string, std::string>> adjacent;
    for (const AssemblyFold& fold : folds) {
        std::string first = fold.parentPanelId;
        std::string second = fold.childPanelId;
        if (second < first) {
            std::swap(first, second);
        }
        adjacent.emplace(first, second);
    }
    for (std::size_t a = 0; a < placed.panels.size(); ++a) {
        for (std::size_t b = a + 1; b < placed.panels.size(); ++b) {
            std::string first = placed.panels[a].panelId;
            std::string second = placed.panels[b].panelId;
            if (second < first) {
                std::swap(first, second);
            }
            if (adjacent.count({first, second}) > 0) {
                continue;
            }
            const geometry::PolylineApproach approach = geometry::ClosestApproachBetween(
                placed.panels[a].outline, placed.panels[b].outline);
            if (approach.valid && approach.distanceMm <= toleranceMm) {
                AssemblyIntersectionReport::Hit hit;
                hit.firstPanelId = placed.panels[a].panelId;
                hit.secondPanelId = placed.panels[b].panelId;
                hit.distanceMm = approach.distanceMm;
                hit.position = (approach.firstPoint + approach.secondPoint) * 0.5;
                report.hits.push_back(hit);
            }
        }
    }
    (void)PolylineLength;
    if (!report.hits.empty()) {
        // 板どうしがぶつかっている。黙って重ねない(§10.2)。
        double closest = report.hits.front().distanceMm;
        for (const AssemblyIntersectionReport::Hit& hit : report.hits) {
            closest = std::min(closest, hit.distanceMm);
        }
        report.diagnostics.push_back(MakeError(kSelfIntersection,
            "組み立てたときに板どうしがぶつかります。",
            std::to_string(report.hits.size()) + " 箇所。最も近いところで "
                + std::to_string(closest) + " mm(許容 "
                + std::to_string(toleranceMm) + " mm)。"));
    }
    return report;
}

} // namespace kachakacha::v2::fabrication
