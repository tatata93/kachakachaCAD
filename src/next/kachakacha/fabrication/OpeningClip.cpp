#include "kachakacha/fabrication/OpeningClip.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace kachakacha::v2::fabrication {

using base::Diagnostic;
using base::MakeError;
using base::MakeWarning;
using base::Result;

namespace {

constexpr const char* kProjectionFailed = "FAB-O001";
constexpr const char* kNotClosed = "FAB-O002";
constexpr const char* kConflictsWithSeam = "FAB-O004";

[[nodiscard]] double PolylineLength(const std::vector<Vector3>& points, bool closed)
{
    double total = 0.0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        total += (points[index] - points[index - 1]).Length();
    }
    if (closed && points.size() >= 2) {
        total += (points.front() - points.back()).Length();
    }
    return total;
}

//! 点が閉じた領域の内側にあるか。
//! 領域は面の上の閉じた点列。面が曲がっているので、領域の平均法線で平面へ落として見る。
[[nodiscard]] bool InsideRegion(const std::vector<Vector3>& boundary, const Vector3& point)
{
    if (boundary.size() < 3) {
        return false;
    }
    // Newell の方法で法線を出す。ねじれていても向きが決まる。
    Vector3 normal{};
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const Vector3& a = boundary[index];
        const Vector3& b = boundary[(index + 1) % boundary.size()];
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }
    const double length = normal.Length();
    if (!(length > 0.0)) {
        return false;
    }
    normal = normal * (1.0 / length);
    Vector3 origin = geometry::Centroid(boundary);
    Vector3 u = boundary.front() - origin;
    u = u - normal * Dot(u, normal);
    const double uLength = u.Length();
    if (!(uLength > 0.0)) {
        return false;
    }
    u = u * (1.0 / uLength);
    const Vector3 v = Cross(normal, u);
    const auto flatten = [&](const Vector3& value) {
        const Vector3 relative = value - origin;
        return geometry::Point2{Dot(relative, u), Dot(relative, v)};
    };
    std::vector<geometry::Point2> loop;
    loop.reserve(boundary.size());
    for (const Vector3& value : boundary) {
        loop.push_back(flatten(value));
    }
    return geometry::ContainsPoint(loop, flatten(point));
}

//! 領域の境目に沿って、from から to まで辿る。
//!
//! ここが肝。2点をまっすぐ結ぶと、曲がった面の上では弦になって形が崩れる。
//! 境目の点列そのものを通るようにし、さらに刻みが粗いところは分割する。
[[nodiscard]] std::vector<Vector3> WalkAlongBoundary(const std::vector<Vector3>& boundary,
    const Vector3& from, const Vector3& to, double maximumStepMm)
{
    std::vector<Vector3> path;
    if (boundary.size() < 2) {
        path.push_back(to);
        return path;
    }
    // 境目の上で from と to にいちばん近い位置(添字)を探す。
    const auto nearestIndex = [&](const Vector3& point) {
        std::size_t best = 0;
        double bestDistance = (boundary.front() - point).Length();
        for (std::size_t index = 1; index < boundary.size(); ++index) {
            const double distance = (boundary[index] - point).Length();
            if (distance < bestDistance) {
                bestDistance = distance;
                best = index;
            }
        }
        return best;
    };
    const std::size_t start = nearestIndex(from);
    const std::size_t end = nearestIndex(to);
    // 短いほうの回り方を選ぶ。
    const std::size_t count = boundary.size();
    const std::size_t forward = (end + count - start) % count;
    const std::size_t backward = (start + count - end) % count;
    std::vector<Vector3> walk;
    if (forward <= backward) {
        for (std::size_t step = 1; step <= forward; ++step) {
            walk.push_back(boundary[(start + step) % count]);
        }
    } else {
        for (std::size_t step = 1; step <= backward; ++step) {
            walk.push_back(boundary[(start + count - step) % count]);
        }
    }
    // 刻みが粗いところを分割する。弦になるのを防ぐ。
    Vector3 previous = from;
    for (const Vector3& next : walk) {
        const double distance = (next - previous).Length();
        if (distance > maximumStepMm && maximumStepMm > 0.0) {
            const int pieces = static_cast<int>(std::ceil(distance / maximumStepMm));
            for (int index = 1; index < pieces; ++index) {
                const double t = static_cast<double>(index) / static_cast<double>(pieces);
                path.push_back(previous + (next - previous) * t);
            }
        }
        path.push_back(next);
        previous = next;
    }
    if (path.empty() || (path.back() - to).Length() > 1.0e-12) {
        path.push_back(to);
    }
    return path;
}

//! 線分が領域の境目と交わる位置。複数あれば from に近い順。
[[nodiscard]] std::vector<Vector3> CrossingsWithBoundary(const std::vector<Vector3>& boundary,
    const Vector3& from, const Vector3& to, double toleranceMm)
{
    std::vector<std::pair<double, Vector3>> found;
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const Vector3& a = boundary[index];
        const Vector3& b = boundary[(index + 1) % boundary.size()];
        const geometry::PolylineApproach approach =
            geometry::ClosestApproachBetween({from, to}, {a, b});
        if (!approach.valid || approach.distanceMm > toleranceMm) {
            continue;
        }
        // 端点そのものは交差として数えない(同じ点が2回出る)。
        if (approach.firstParameter <= 1.0e-9 || approach.firstParameter >= 1.0 - 1.0e-9) {
            continue;
        }
        found.emplace_back(approach.firstParameter, approach.firstPoint);
    }
    std::sort(found.begin(), found.end(),
        [](const auto& l, const auto& r) { return l.first < r.first; });
    std::vector<Vector3> points;
    for (const auto& item : found) {
        if (!points.empty() && (points.back() - item.second).Length() <= toleranceMm) {
            continue;
        }
        points.push_back(item.second);
    }
    return points;
}

} // namespace

Result<OpeningClipResult> ClipOpeningAcrossPanels(const std::vector<Vector3>& opening,
    const std::vector<PanelRegion>& panels, double maximumStepMm, double toleranceMm)
{
    if (opening.size() < 3) {
        return Result<OpeningClipResult>::Failure(MakeError(kProjectionFailed,
            "開口の形が足りません。",
            "点が " + std::to_string(opening.size()) + " 個しかありません。"));
    }
    if (panels.empty()) {
        return Result<OpeningClipResult>::Failure(MakeError(kProjectionFailed,
            "切り分ける先の部材がありません。", {}));
    }
    for (const PanelRegion& panel : panels) {
        if (panel.boundary.size() < 3) {
            return Result<OpeningClipResult>::Failure(MakeError(kProjectionFailed,
                "部材の輪郭が足りません。", panel.panelId));
        }
    }
    for (const Vector3& point : opening) {
        if (!point.IsFinite()) {
            return Result<OpeningClipResult>::Failure(MakeError(kProjectionFailed,
                "開口に有限でない数が入っています。", {}));
        }
    }

    OpeningClipResult result;
    result.originalPerimeterMm = PolylineLength(opening, true);

    // 開口の各点が、どの部材に属するかを決める。
    const std::size_t count = opening.size();
    std::vector<std::size_t> owner(count, panels.size());
    for (std::size_t index = 0; index < count; ++index) {
        for (std::size_t panel = 0; panel < panels.size(); ++panel) {
            if (InsideRegion(panels[panel].boundary, opening[index])) {
                owner[index] = panel;
                break;
            }
        }
    }
    const std::size_t orphan = static_cast<std::size_t>(
        std::count(owner.begin(), owner.end(), panels.size()));
    if (orphan == count) {
        return Result<OpeningClipResult>::Failure(MakeError(kProjectionFailed,
            "開口がどの部材にも載っていません。",
            "部材の範囲か、開口の位置を確かめてください。"));
    }
    if (orphan > 0) {
        // どの部材にも入らない点があっても、開口を消さない。
        // 直前の部材のものとして扱い、そのことを伝える。
        result.notes.push_back(MakeWarning(kConflictsWithSeam,
            "開口の一部が、どの部材の範囲にも入りませんでした。",
            std::to_string(orphan) + " 点。境目の位置を確かめてください。"));
        for (std::size_t index = 0; index < count; ++index) {
            if (owner[index] != panels.size()) {
                continue;
            }
            for (std::size_t step = 1; step < count; ++step) {
                const std::size_t previous = (index + count - step) % count;
                if (owner[previous] != panels.size()) {
                    owner[index] = owner[previous];
                    break;
                }
            }
        }
    }

    // 所属が変わるところで区切る。区切りごとに1つの断片になる。
    std::vector<std::size_t> breaks;
    for (std::size_t index = 0; index < count; ++index) {
        if (owner[index] != owner[(index + count - 1) % count]) {
            breaks.push_back(index);
        }
    }
    if (breaks.empty()) {
        // またいでいない。まるごと1枚に載っている。
        OpeningPiece piece;
        piece.panelId = panels[owner.front()].panelId;
        piece.outline = opening;
        piece.closed = true;
        result.pieces.push_back(std::move(piece));
        result.totalPieceBoundaryMm = result.originalPerimeterMm;
        return Result<OpeningClipResult>::Success(std::move(result));
    }

    // 境目をまたぐところの、正確な交点を先に出す。
    // 断片の端をここに揃えると、隣の断片とぴったり合う。
    // 最後に内側だった点で止めると、必ず隙間ができる。
    std::map<std::size_t, Vector3> crossingAt;
    for (const std::size_t breakIndex : breaks) {
        const std::size_t previous = (breakIndex + count - 1) % count;
        const std::size_t panel = owner[breakIndex];
        const std::vector<Vector3> crossings = CrossingsWithBoundary(
            panels[panel].boundary, opening[previous], opening[breakIndex],
            std::max(toleranceMm, maximumStepMm));
        crossingAt[breakIndex] = crossings.empty()
            ? (opening[previous] + opening[breakIndex]) * 0.5
            : crossings.back();
        if (crossings.empty()) {
            result.notes.push_back(MakeWarning(kConflictsWithSeam,
                "境目との交点を正確に求められませんでした。",
                "中点で代用しました。境目の刻みを細かくしてください。"));
        }
    }

    // 横切る点ごとにIDを振る。隣り合う2つの断片が同じIDを共有する。
    // 断片ごとに別のIDを振ると、あとで「本当に繋がっているか」を確かめられない。
    std::map<std::size_t, std::string> junctionIdAt;
    for (std::size_t at = 0; at < breaks.size(); ++at) {
        const std::size_t breakIndex = breaks[at];
        const std::size_t before = owner[(breakIndex + count - 1) % count];
        const std::size_t after = owner[breakIndex];
        std::string first = panels[before].panelId;
        std::string second = panels[after].panelId;
        if (second < first) {
            std::swap(first, second);
        }
        junctionIdAt[breakIndex] =
            "opening/junction/" + first + "|" + second + "/" + std::to_string(at);
    }

    // 区切りごとに断片を作る。
    // 切り口は境目に沿って辿る。まっすぐ結ばない。
    for (std::size_t at = 0; at < breaks.size(); ++at) {
        const std::size_t begin = breaks[at];
        const std::size_t end = breaks[(at + 1) % breaks.size()];
        const std::size_t panel = owner[begin];
        OpeningPiece piece;
        piece.panelId = panels[panel].panelId;

        // 境目の上の入口から始める。
        piece.outline.push_back(crossingAt[begin]);
        // 開口の線に沿った部分。
        std::size_t index = begin;
        while (true) {
            piece.outline.push_back(opening[index]);
            index = (index + 1) % count;
            if (index == end) {
                break;
            }
        }
        // 境目の上の出口で終える。
        piece.outline.push_back(crossingAt[end]);

        // 出口から入口まで、境目に沿って戻る。
        const Vector3 exitPoint = piece.outline.back();
        const Vector3 entryPoint = piece.outline.front();
        const std::vector<Vector3> along = WalkAlongBoundary(panels[panel].boundary,
            exitPoint, entryPoint, maximumStepMm);
        OpeningPiece::Junction junction;
        junction.firstIndex = piece.outline.size() - 1;   // 出口の点から切り口が始まる
        for (const Vector3& point : along) {
            piece.outline.push_back(point);
        }
        // 最後の点は入口と同じなので落とす(閉じた輪として扱う)。
        if (piece.outline.size() >= 2
            && (piece.outline.back() - entryPoint).Length() <= 1.0e-9) {
            piece.outline.pop_back();
        }
        junction.lastIndex = piece.outline.empty() ? 0 : piece.outline.size() - 1;
        piece.junctions.push_back(junction);
        piece.entryJunctionId = junctionIdAt[begin];
        piece.exitJunctionId = junctionIdAt[end];
        piece.closed = true;
        result.totalPieceBoundaryMm += PolylineLength(piece.outline, true);
        result.pieces.push_back(std::move(piece));
    }
    return Result<OpeningClipResult>::Success(std::move(result));
}

OpeningClosureCheck CheckOpeningClosure(const OpeningClipResult& result, double toleranceMm)
{
    OpeningClosureCheck check;
    if (result.pieces.empty()) {
        check.closed = false;
        return check;
    }
    // 開口の線に由来する部分だけを集めて、元の開口の周長と比べる。
    double openingPart = 0.0;
    for (const OpeningPiece& piece : result.pieces) {
        std::vector<bool> isJunction(piece.outline.size(), false);
        for (const auto& junction : piece.junctions) {
            for (std::size_t index = junction.firstIndex;
                index <= junction.lastIndex && index < isJunction.size(); ++index) {
                isJunction[index] = true;
            }
        }
        for (std::size_t index = 1; index < piece.outline.size(); ++index) {
            if (isJunction[index] || isJunction[index - 1]) {
                continue;
            }
            openingPart += (piece.outline[index] - piece.outline[index - 1]).Length();
        }
    }
    check.perimeterDifferenceMm = std::abs(openingPart - result.originalPerimeterMm);

    // 断片どうしが、同じ横切り点でぴったり合っているか。
    std::map<std::string, std::vector<Vector3>> meeting;
    for (const OpeningPiece& piece : result.pieces) {
        if (piece.outline.empty()) {
            continue;
        }
        if (!piece.entryJunctionId.empty()) {
            meeting[piece.entryJunctionId].push_back(piece.outline.front());
        }
        if (!piece.exitJunctionId.empty() && !piece.junctions.empty()) {
            const std::size_t index = piece.junctions.front().firstIndex;
            if (index < piece.outline.size()) {
                meeting[piece.exitJunctionId].push_back(piece.outline[index]);
            }
        }
    }
    for (const auto& entry : meeting) {
        if (entry.second.size() < 2) {
            // 片側からしか来ていない。繋がっていない。
            check.closed = false;
            continue;
        }
        for (std::size_t a = 0; a < entry.second.size(); ++a) {
            for (std::size_t b = a + 1; b < entry.second.size(); ++b) {
                check.maximumGapMm = std::max(check.maximumGapMm,
                    (entry.second[a] - entry.second[b]).Length());
            }
        }
    }
    check.closed = check.closed && check.perimeterDifferenceMm <= toleranceMm
        && check.maximumGapMm <= toleranceMm;
    return check;
}

} // namespace kachakacha::v2::fabrication
