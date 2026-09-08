#pragma once

//! 型紙(fabrication-contract.md §9)。
//!
//! 決まりごと:
//!   - 自動配置で部材を拡大縮小・鏡像反転してはならない。回転と平行移動だけ。
//!   - 輪郭を長方形へ均さない。曲線は曲線のまま。
//!   - 部材番号は安定していること。並べ替えても同じ部材は同じ番号。
//!   - 開口・切れ目・外周・折り線は別レイヤー。
//!   - 1枚へまとめるときは、重なりを検出する。解が無ければ重なり位置を示す。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::fabrication {

using geometry::Point2;

enum class PatternLayer {
    Outline,
    Fold,
    Cut,
    Opening,
    Annotation,
};

[[nodiscard]] std::string_view PatternLayerNameJa(PatternLayer layer) noexcept;

enum class FoldSense { Mountain, Valley };

//! 型紙に載る1枚の部材。
struct PatternPanel {
    std::string panelId;
    //! 型紙の座標での外周。
    std::vector<Point2> outline;
    std::vector<std::vector<Point2>> openings;
    std::vector<std::vector<Point2>> reliefCuts;
    struct Fold {
        std::string foldId;
        std::vector<Point2> path;
        FoldSense sense = FoldSense::Mountain;
        double angleRad = 0.0;
    };
    std::vector<Fold> folds;
    //! 接着する相手。同じ番号どうしを貼り合わせる。
    struct Mate {
        std::string matePairId;
        std::vector<Point2> path;
    };
    std::vector<Mate> mates;
};

//! 用紙。
struct PaperSize {
    std::string name = "a4";
    double widthMm = 210.0;
    double heightMm = 297.0;
    double marginMm = 5.0;
    //! 部材どうしの間隔。0にすると切断線が重なって、隣の部材まで切ってしまう。
    double spacingMm = 2.0;
};

[[nodiscard]] std::optional<PaperSize> StandardPaper(const std::string& name);

//! 配置。回転と平行移動だけ。倍率も鏡像も持たない(型として持たせない)。
struct PatternPlacement {
    std::string panelId;
    Point2 translationMm{};
    double rotationRad = 0.0;
    int pageIndex = 0;
    //! 安定した部材番号。1始まり。
    int partNumber = 0;
};

struct PatternLayoutResult {
    std::vector<PatternPlacement> placements;
    int pageCount = 0;
    std::vector<base::Diagnostic> notes;
};

//! 部材を用紙へ並べる。入る場所が無ければページを足す。
//! 1枚に入りきらない部材は、拡大縮小せずに断る。
[[nodiscard]] base::Result<PatternLayoutResult> LayoutPattern(
    const std::vector<PatternPanel>& panels, const PaperSize& paper);

//! 並べた結果が重なっていないかを確かめる。
struct PatternOverlapReport {
    struct Hit {
        std::string firstPanelId;
        std::string secondPanelId;
        int pageIndex = 0;
        Point2 position{};
    };
    std::vector<Hit> hits;
};

[[nodiscard]] PatternOverlapReport FindPatternOverlaps(
    const std::vector<PatternPanel>& panels, const PatternLayoutResult& layout);

//! 配置を当てはめた後の輪郭。回転と平行移動だけを掛ける。
[[nodiscard]] std::vector<Point2> ApplyPlacement(const std::vector<Point2>& outline,
    const PatternPlacement& placement);

//! 部材番号を安定して振る。名前の順で決めるので、渡す順を変えても同じ番号になる。
[[nodiscard]] std::vector<std::pair<std::string, int>> AssignPartNumbers(
    const std::vector<PatternPanel>& panels);

} // namespace kachakacha::v2::fabrication
