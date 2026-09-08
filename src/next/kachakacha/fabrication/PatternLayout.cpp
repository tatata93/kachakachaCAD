#include "kachakacha/fabrication/PatternLayout.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace kachakacha::v2::fabrication {

using base::Diagnostic;
using base::MakeError;
using base::MakeWarning;
using base::Result;
using geometry::Vector3;

namespace {

constexpr const char* kTooLarge = "FAB-P001";
constexpr const char* kBadInput = "FAB-P002";

struct Bounds {
    double minimumU = 0.0;
    double minimumV = 0.0;
    double maximumU = 0.0;
    double maximumV = 0.0;

    [[nodiscard]] double Width() const { return maximumU - minimumU; }
    [[nodiscard]] double Height() const { return maximumV - minimumV; }
};

[[nodiscard]] Bounds BoundsOf(const std::vector<Point2>& points)
{
    Bounds bounds;
    if (points.empty()) {
        return bounds;
    }
    bounds.minimumU = points.front().u;
    bounds.maximumU = points.front().u;
    bounds.minimumV = points.front().v;
    bounds.maximumV = points.front().v;
    for (const Point2& point : points) {
        bounds.minimumU = std::min(bounds.minimumU, point.u);
        bounds.maximumU = std::max(bounds.maximumU, point.u);
        bounds.minimumV = std::min(bounds.minimumV, point.v);
        bounds.maximumV = std::max(bounds.maximumV, point.v);
    }
    return bounds;
}

[[nodiscard]] std::vector<Vector3> LiftClosed(const std::vector<Point2>& points)
{
    std::vector<Vector3> lifted;
    lifted.reserve(points.size() + 1);
    for (const Point2& point : points) {
        lifted.push_back(Vector3{point.u, point.v, 0.0});
    }
    if (!lifted.empty()) {
        lifted.push_back(lifted.front());
    }
    return lifted;
}

} // namespace

std::string_view PatternLayerNameJa(PatternLayer layer) noexcept
{
    switch (layer) {
    case PatternLayer::Outline:    return "外周";
    case PatternLayer::Fold:       return "折り線";
    case PatternLayer::Cut:        return "切れ目";
    case PatternLayer::Opening:    return "開口";
    case PatternLayer::Annotation: return "注記";
    }
    return "";
}

std::optional<PaperSize> StandardPaper(const std::string& name)
{
    if (name == "a4") {
        return PaperSize{"a4", 210.0, 297.0, 5.0, 2.0};
    }
    if (name == "a3") {
        return PaperSize{"a3", 297.0, 420.0, 5.0, 2.0};
    }
    if (name == "b4") {
        return PaperSize{"b4", 257.0, 364.0, 5.0, 2.0};
    }
    if (name == "letter") {
        return PaperSize{"letter", 215.9, 279.4, 5.0, 2.0};
    }
    return std::nullopt;
}

std::vector<Point2> ApplyPlacement(const std::vector<Point2>& outline,
    const PatternPlacement& placement)
{
    const double c = std::cos(placement.rotationRad);
    const double s = std::sin(placement.rotationRad);
    std::vector<Point2> moved;
    moved.reserve(outline.size());
    for (const Point2& point : outline) {
        // 回転と平行移動だけ。倍率も鏡像も掛からない。
        moved.push_back(Point2{point.u * c - point.v * s + placement.translationMm.u,
            point.u * s + point.v * c + placement.translationMm.v});
    }
    return moved;
}

std::vector<std::pair<std::string, int>> AssignPartNumbers(
    const std::vector<PatternPanel>& panels)
{
    std::vector<std::string> names;
    names.reserve(panels.size());
    for (const PatternPanel& panel : panels) {
        names.push_back(panel.panelId);
    }
    // 名前の順で番号を振る。渡す順に依らないので、番号が安定する。
    std::sort(names.begin(), names.end());
    std::vector<std::pair<std::string, int>> numbers;
    for (std::size_t index = 0; index < names.size(); ++index) {
        numbers.emplace_back(names[index], static_cast<int>(index) + 1);
    }
    return numbers;
}

Result<PatternLayoutResult> LayoutPattern(const std::vector<PatternPanel>& panels,
    const PaperSize& paper)
{
    if (panels.empty()) {
        return Result<PatternLayoutResult>::Failure(MakeError(kBadInput,
            "並べる部材がありません。", {}));
    }
    if (!(paper.widthMm > 0.0) || !(paper.heightMm > 0.0)) {
        return Result<PatternLayoutResult>::Failure(MakeError(kBadInput,
            "用紙の大きさが正しくありません。", paper.name));
    }
    const double usableWidth = paper.widthMm - paper.marginMm * 2.0;
    const double usableHeight = paper.heightMm - paper.marginMm * 2.0;
    if (!(usableWidth > 0.0) || !(usableHeight > 0.0)) {
        return Result<PatternLayoutResult>::Failure(MakeError(kBadInput,
            "余白が用紙より大きいです。", paper.name));
    }

    const std::vector<std::pair<std::string, int>> numbers = AssignPartNumbers(panels);
    std::map<std::string, int> numberOf;
    for (const auto& entry : numbers) {
        numberOf[entry.first] = entry.second;
    }

    // 大きいものから順に置く。並べ方は決定的にする。
    std::vector<const PatternPanel*> ordered;
    for (const PatternPanel& panel : panels) {
        if (panel.outline.size() < 3) {
            return Result<PatternLayoutResult>::Failure(MakeError(kBadInput,
                "部材の外周が足りません。", panel.panelId));
        }
        ordered.push_back(&panel);
    }
    std::sort(ordered.begin(), ordered.end(),
        [](const PatternPanel* l, const PatternPanel* r) {
            const Bounds left = BoundsOf(l->outline);
            const Bounds right = BoundsOf(r->outline);
            if (left.Height() != right.Height()) {
                return left.Height() > right.Height();
            }
            if (left.Width() != right.Width()) {
                return left.Width() > right.Width();
            }
            return l->panelId < r->panelId;
        });

    PatternLayoutResult result;
    int page = 0;
    double cursorU = 0.0;
    double cursorV = 0.0;
    double rowHeight = 0.0;

    for (const PatternPanel* panel : ordered) {
        const Bounds bounds = BoundsOf(panel->outline);
        double rotation = 0.0;
        double width = bounds.Width();
        double height = bounds.Height();
        const bool fitsUpright = width <= usableWidth && height <= usableHeight;
        const bool fitsTurned = height <= usableWidth && width <= usableHeight;
        if (!fitsUpright && !fitsTurned) {
            // 縮めて入れない。断る(§9「拡大縮小してはならない」)。
            return Result<PatternLayoutResult>::Failure(MakeError(kTooLarge,
                "用紙に入らない部材があります。",
                panel->panelId + ": " + std::to_string(width) + " × "
                    + std::to_string(height) + " mm、用紙は " + std::to_string(usableWidth)
                    + " × " + std::to_string(usableHeight)
                    + " mm。縮小はしません。分割するか、大きい用紙にしてください。"));
        }
        if (!fitsUpright) {
            rotation = 1.5707963267948966;   // 90度だけ回す。鏡像にはしない
            std::swap(width, height);
        }
        if (cursorU + width > usableWidth) {
            cursorU = 0.0;
            cursorV += rowHeight;
            rowHeight = 0.0;
        }
        if (cursorV + height > usableHeight) {
            ++page;
            cursorU = 0.0;
            cursorV = 0.0;
            rowHeight = 0.0;
        }
        PatternPlacement placement;
        placement.panelId = panel->panelId;
        placement.rotationRad = rotation;
        placement.pageIndex = page;
        placement.partNumber = numberOf[panel->panelId];
        // 回転後の左下が (margin + cursor) に来るように寄せる。
        const std::vector<Point2> rotated = ApplyPlacement(panel->outline,
            PatternPlacement{panel->panelId, Point2{0.0, 0.0}, rotation, 0, 0});
        const Bounds rotatedBounds = BoundsOf(rotated);
        placement.translationMm = Point2{
            paper.marginMm + cursorU - rotatedBounds.minimumU,
            paper.marginMm + cursorV - rotatedBounds.minimumV};
        result.placements.push_back(placement);

        const double spacing = std::max(paper.spacingMm, 0.0);
        cursorU += width + spacing;
        rowHeight = std::max(rowHeight, height + spacing);
    }
    result.pageCount = page + 1;
    if (result.pageCount > 1) {
        result.notes.push_back(MakeWarning("FAB-P100", "型紙が複数ページになりました。",
            std::to_string(result.pageCount) + " ページ。"));
    }
    // 並びを部材番号順にして、出力を決定的にする。
    std::sort(result.placements.begin(), result.placements.end(),
        [](const PatternPlacement& l, const PatternPlacement& r) {
            return l.partNumber < r.partNumber;
        });
    return Result<PatternLayoutResult>::Success(std::move(result));
}

PatternOverlapReport FindPatternOverlaps(const std::vector<PatternPanel>& panels,
    const PatternLayoutResult& layout)
{
    PatternOverlapReport report;
    std::map<std::string, const PatternPanel*> byId;
    for (const PatternPanel& panel : panels) {
        byId[panel.panelId] = &panel;
    }
    for (std::size_t a = 0; a < layout.placements.size(); ++a) {
        for (std::size_t b = a + 1; b < layout.placements.size(); ++b) {
            if (layout.placements[a].pageIndex != layout.placements[b].pageIndex) {
                continue;
            }
            const auto first = byId.find(layout.placements[a].panelId);
            const auto second = byId.find(layout.placements[b].panelId);
            if (first == byId.end() || second == byId.end()) {
                continue;
            }
            const std::vector<Point2> left =
                ApplyPlacement(first->second->outline, layout.placements[a]);
            const std::vector<Point2> right =
                ApplyPlacement(second->second->outline, layout.placements[b]);
            // 輪郭どうしが交わるか、片方がもう片方の中に入っているか。
            const bool crossing = geometry::LoopsIntersect(left, right, 1.0e-9);
            const bool contained = geometry::ContainsPoint(right, left.front())
                || geometry::ContainsPoint(left, right.front());
            if (!crossing && !contained) {
                continue;
            }
            const geometry::PolylineApproach approach =
                geometry::ClosestApproachBetween(LiftClosed(left), LiftClosed(right));
            PatternOverlapReport::Hit hit;
            hit.firstPanelId = layout.placements[a].panelId;
            hit.secondPanelId = layout.placements[b].panelId;
            hit.pageIndex = layout.placements[a].pageIndex;
            if (approach.valid) {
                hit.position = Point2{approach.firstPoint.x, approach.firstPoint.y};
            }
            report.hits.push_back(hit);
        }
    }
    return report;
}

} // namespace kachakacha::v2::fabrication
