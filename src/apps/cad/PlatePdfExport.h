#pragma once

#include "kachakacha/io/PlateFlatPattern.h"

#include <QPageLayout>
#include <QPageSize>
#include <QRectF>
#include <QSizeF>
#include <QString>

namespace kachakacha::qtio {

enum class PlatePdfOrientation {
    Automatic,
    Portrait,
    Landscape,
};

struct PlatePdfOptions {
    QPageSize::PageSizeId pageSize = QPageSize::A4;
    PlatePdfOrientation orientation = PlatePdfOrientation::Automatic;
    double marginMillimeters = 10.0;
    double overlapMillimeters = 5.0;
    double footerHeightMillimeters = 14.0;
};

struct PlatePdfLayout {
    QPageLayout::Orientation orientation = QPageLayout::Portrait;
    QSizeF pageSizeMillimeters;
    QSizeF tileSizeMillimeters;
    QRectF patternBoundsMillimeters;
    double horizontalStepMillimeters = 0.0;
    double verticalStepMillimeters = 0.0;
    int columns = 1;
    int rows = 1;

    [[nodiscard]] int PageCount() const noexcept { return columns * rows; }
};

[[nodiscard]] PlatePdfLayout CalculatePlatePdfLayout(
    const io::PlateFlatPattern& pattern,
    PlatePdfOptions options = {});

void WritePlateFlatPatternPdf(
    const QString& path,
    const io::PlateFlatPattern& pattern,
    PlatePdfOptions options = {});

} // namespace kachakacha::qtio
