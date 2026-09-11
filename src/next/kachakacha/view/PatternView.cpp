#include "kachakacha/view/PatternView.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::view {

PatternFit FitPatternPage(double pageWidthMm, double pageHeightMm, double viewWidthPx,
    double viewHeightPx, double marginPx)
{
    PatternFit fit;
    const bool usable = std::isfinite(pageWidthMm) && std::isfinite(pageHeightMm)
        && std::isfinite(viewWidthPx) && std::isfinite(viewHeightPx)
        && pageWidthMm > 0.0 && pageHeightMm > 0.0;
    if (!usable) {
        return fit;
    }
    const double margin = std::max(0.0, marginPx);
    const double availableWidth = viewWidthPx - margin * 2.0;
    const double availableHeight = viewHeightPx - margin * 2.0;
    if (availableWidth <= 0.0 || availableHeight <= 0.0) {
        return fit;
    }
    // 縦横で別の倍率にしない。型紙が歪んで見えると、原寸で切るものとして信用できない。
    fit.pixelsPerMm = std::min(availableWidth / pageWidthMm, availableHeight / pageHeightMm);
    fit.widthPx = pageWidthMm * fit.pixelsPerMm;
    fit.heightPx = pageHeightMm * fit.pixelsPerMm;
    // 余った分は左右・上下へ等しく配る。紙が真ん中に来る。
    fit.originPx = Point2{(viewWidthPx - fit.widthPx) * 0.5,
        (viewHeightPx - fit.heightPx) * 0.5};
    return fit;
}

} // namespace kachakacha::v2::view
