#include "PlatePdfExport.h"

#include <QFileInfo>
#include <QMarginsF>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace kachakacha::qtio {

using geometry::Vector2;
using io::PlateFlatPattern;
using io::PlateFlatPatternPath;

namespace {

constexpr int kPdfResolution = 600;
constexpr double kMillimetersPerInch = 25.4;
constexpr double kRegistrationInsetMillimeters = 12.0;

struct PatternBounds {
    double minimumX = std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double maximumY = -std::numeric_limits<double>::infinity();
};

struct RegistrationMark {
    QPointF point;
    double halfSizeMillimeters = 0.0;
};

void IncludePath(PatternBounds& bounds, const PlateFlatPatternPath& path)
{
    for (const Vector2 point : path.points) {
        if (!point.IsFinite()) {
            throw std::invalid_argument("Plate PDF contains invalid coordinates.");
        }
        bounds.minimumX = std::min(bounds.minimumX, point.x);
        bounds.minimumY = std::min(bounds.minimumY, point.y);
        bounds.maximumX = std::max(bounds.maximumX, point.x);
        bounds.maximumY = std::max(bounds.maximumY, point.y);
    }
}

PatternBounds MeasurePattern(const PlateFlatPattern& pattern)
{
    if (pattern.outerBoundary.points.size() < 4) {
        throw std::invalid_argument("Plate PDF requires a usable outer boundary.");
    }
    PatternBounds bounds;
    if (pattern.pieces.empty()) {
        IncludePath(bounds, pattern.outerBoundary);
    } else {
        for (const auto& piece : pattern.pieces) {
            IncludePath(bounds, piece.outerBoundary);
        }
    }
    for (const auto& opening : pattern.openings) {
        IncludePath(bounds, opening);
    }
    for (const auto& fold : pattern.foldLines) {
        IncludePath(bounds, fold);
    }
    for (const auto& cut : pattern.reliefCuts) {
        IncludePath(bounds, cut);
    }
    if (!std::isfinite(bounds.minimumX) || bounds.maximumX - bounds.minimumX <= 1.0e-9
        || bounds.maximumY - bounds.minimumY <= 1.0e-9) {
        throw std::invalid_argument("Plate PDF boundary has no measurable area.");
    }
    return bounds;
}

void ValidateOptions(const PlatePdfOptions& options)
{
    if (!QPageSize(options.pageSize).isValid()
        || !std::isfinite(options.marginMillimeters) || options.marginMillimeters < 3.0
        || !std::isfinite(options.overlapMillimeters) || options.overlapMillimeters < 0.0
        || !std::isfinite(options.footerHeightMillimeters) || options.footerHeightMillimeters < 8.0) {
        throw std::invalid_argument("Plate PDF options are invalid.");
    }
}

int TileCount(double patternLength, double tileLength, double step)
{
    if (patternLength <= tileLength + 1.0e-9) {
        return 1;
    }
    return 1 + static_cast<int>(std::ceil((patternLength - tileLength) / step - 1.0e-12));
}

PlatePdfLayout LayoutForOrientation(
    const PatternBounds& bounds,
    const PlatePdfOptions& options,
    QPageLayout::Orientation orientation)
{
    QSizeF pageSize = QPageSize(options.pageSize).size(QPageSize::Millimeter);
    if (orientation == QPageLayout::Landscape) {
        pageSize.transpose();
    }
    const QSizeF tileSize(
        pageSize.width() - options.marginMillimeters * 2.0,
        pageSize.height() - options.marginMillimeters * 2.0 - options.footerHeightMillimeters);
    if (tileSize.width() <= 20.0 || tileSize.height() <= 20.0
        || options.overlapMillimeters >= tileSize.width()
        || options.overlapMillimeters >= tileSize.height()) {
        throw std::invalid_argument("Plate PDF margins and overlap leave no usable page area.");
    }

    PlatePdfLayout layout;
    layout.orientation = orientation;
    layout.pageSizeMillimeters = pageSize;
    layout.tileSizeMillimeters = tileSize;
    layout.patternBoundsMillimeters = QRectF(
        bounds.minimumX,
        bounds.minimumY,
        bounds.maximumX - bounds.minimumX,
        bounds.maximumY - bounds.minimumY);
    layout.horizontalStepMillimeters = tileSize.width() - options.overlapMillimeters;
    layout.verticalStepMillimeters = tileSize.height() - options.overlapMillimeters;
    layout.columns = TileCount(
        layout.patternBoundsMillimeters.width(),
        tileSize.width(),
        layout.horizontalStepMillimeters);
    layout.rows = TileCount(
        layout.patternBoundsMillimeters.height(),
        tileSize.height(),
        layout.verticalStepMillimeters);
    if (layout.PageCount() > 100) {
        throw std::invalid_argument("Plate PDF would require more than 100 pages.");
    }
    return layout;
}

QPainterPath ToPainterPath(const PlateFlatPatternPath& path, const PatternBounds& bounds)
{
    QPainterPath painterPath;
    if (path.points.empty()) {
        return painterPath;
    }
    const auto mapPoint = [&](Vector2 point) {
        return QPointF(point.x - bounds.minimumX, bounds.maximumY - point.y);
    };
    painterPath.moveTo(mapPoint(path.points.front()));
    for (std::size_t index = 1; index < path.points.size(); ++index) {
        painterPath.lineTo(mapPoint(path.points[index]));
    }
    if (path.points.size() > 2
        && std::hypot(
            path.points.front().x - path.points.back().x,
            path.points.front().y - path.points.back().y) <= 1.0e-9) {
        painterPath.closeSubpath();
    }
    return painterPath;
}

bool ContainsWithTolerance(const QRectF& rectangle, QPointF point)
{
    constexpr double tolerance = 1.0e-7;
    return point.x() >= rectangle.left() - tolerance
        && point.x() <= rectangle.right() + tolerance
        && point.y() >= rectangle.top() - tolerance
        && point.y() <= rectangle.bottom() + tolerance;
}

std::vector<RegistrationMark> RegistrationMarks(const PlatePdfLayout& layout)
{
    std::vector<RegistrationMark> marks;
    const double horizontalOverlap = layout.tileSizeMillimeters.width()
        - layout.horizontalStepMillimeters;
    const double verticalOverlap = layout.tileSizeMillimeters.height()
        - layout.verticalStepMillimeters;
    if (horizontalOverlap > 1.0e-9) {
        const double halfSize = std::min(2.5, horizontalOverlap * 0.4);
        for (int column = 1; column < layout.columns; ++column) {
            const double x = column * layout.horizontalStepMillimeters + horizontalOverlap * 0.5;
            for (int row = 0; row < layout.rows; ++row) {
                const double top = row * layout.verticalStepMillimeters;
                marks.push_back({{x, top + kRegistrationInsetMillimeters}, halfSize});
                marks.push_back({
                    {x, top + layout.tileSizeMillimeters.height() - kRegistrationInsetMillimeters},
                    halfSize,
                });
            }
        }
    }
    if (verticalOverlap > 1.0e-9) {
        const double halfSize = std::min(2.5, verticalOverlap * 0.4);
        for (int row = 1; row < layout.rows; ++row) {
            const double y = row * layout.verticalStepMillimeters + verticalOverlap * 0.5;
            for (int column = 0; column < layout.columns; ++column) {
                const double left = column * layout.horizontalStepMillimeters;
                marks.push_back({{left + kRegistrationInsetMillimeters, y}, halfSize});
                marks.push_back({
                    {left + layout.tileSizeMillimeters.width() - kRegistrationInsetMillimeters, y},
                    halfSize,
                });
            }
        }
    }
    return marks;
}

void DrawRegistrationMark(QPainter& painter, QPointF point, double halfSize)
{
    painter.drawLine(
        QPointF(point.x() - halfSize, point.y()),
        QPointF(point.x() + halfSize, point.y()));
    painter.drawLine(
        QPointF(point.x(), point.y() - halfSize),
        QPointF(point.x(), point.y() + halfSize));
    const double radius = std::min(1.2, halfSize * 0.48);
    painter.drawEllipse(point, radius, radius);
}

void DrawStrokeText(
    QPainter& painter,
    const QRectF& rectangle,
    Qt::Alignment alignment,
    const QString& text)
{
    const double pixelsPerMillimeter = static_cast<double>(kPdfResolution) / kMillimetersPerInch;
    const double glyphWidth = 1.8 * pixelsPerMillimeter;
    const double glyphHeight = 3.0 * pixelsPerMillimeter;
    const double advance = 2.6 * pixelsPerMillimeter;
    const double textWidth = text.isEmpty() ? 0.0 : glyphWidth + advance * (text.size() - 1);
    double x = rectangle.left();
    if (alignment.testFlag(Qt::AlignRight)) {
        x = rectangle.right() - textWidth;
    } else if (alignment.testFlag(Qt::AlignHCenter)) {
        x = rectangle.center().x() - textWidth * 0.5;
    }
    double y = rectangle.top();
    if (alignment.testFlag(Qt::AlignVCenter)) {
        y = rectangle.center().y() - glyphHeight * 0.5;
    } else if (alignment.testFlag(Qt::AlignBottom)) {
        y = rectangle.bottom() - glyphHeight;
    }

    enum Segment {
        Top = 1 << 0,
        UpperRight = 1 << 1,
        LowerRight = 1 << 2,
        Bottom = 1 << 3,
        LowerLeft = 1 << 4,
        UpperLeft = 1 << 5,
        Middle = 1 << 6,
    };
    const auto digitSegments = [](QChar character) {
        switch (character.unicode()) {
        case '0': return Top | UpperRight | LowerRight | Bottom | LowerLeft | UpperLeft;
        case '1': return UpperRight | LowerRight;
        case '2': return Top | UpperRight | Middle | LowerLeft | Bottom;
        case '3': return Top | UpperRight | Middle | LowerRight | Bottom;
        case '4': return UpperLeft | Middle | UpperRight | LowerRight;
        case '5': return Top | UpperLeft | Middle | LowerRight | Bottom;
        case '6': return Top | UpperLeft | Middle | LowerLeft | LowerRight | Bottom;
        case '7': return Top | UpperRight | LowerRight;
        case '8': return Top | UpperRight | LowerRight | Bottom | LowerLeft | UpperLeft | Middle;
        case '9': return Top | UpperRight | LowerRight | Bottom | UpperLeft | Middle;
        default: return 0;
        }
    };
    painter.save();
    painter.setPen(QPen(Qt::black, std::max(1.0, pixelsPerMillimeter * 0.16), Qt::SolidLine, Qt::RoundCap));
    const auto drawNormalizedLine = [&](double x1, double y1, double x2, double y2) {
        painter.drawLine(
            QPointF(x + x1 * glyphWidth, y + y1 * glyphHeight),
            QPointF(x + x2 * glyphWidth, y + y2 * glyphHeight));
    };
    for (const QChar character : text) {
        const int segments = digitSegments(character);
        if (segments & Top) drawNormalizedLine(0.15, 0.0, 0.85, 0.0);
        if (segments & UpperRight) drawNormalizedLine(1.0, 0.08, 1.0, 0.44);
        if (segments & LowerRight) drawNormalizedLine(1.0, 0.56, 1.0, 0.92);
        if (segments & Bottom) drawNormalizedLine(0.15, 1.0, 0.85, 1.0);
        if (segments & LowerLeft) drawNormalizedLine(0.0, 0.56, 0.0, 0.92);
        if (segments & UpperLeft) drawNormalizedLine(0.0, 0.08, 0.0, 0.44);
        if (segments & Middle) drawNormalizedLine(0.15, 0.5, 0.85, 0.5);
        if (character == '/') {
            drawNormalizedLine(0.1, 0.95, 0.9, 0.05);
        } else if (character == 'C') {
            drawNormalizedLine(0.85, 0.0, 0.15, 0.0);
            drawNormalizedLine(0.0, 0.08, 0.0, 0.92);
            drawNormalizedLine(0.15, 1.0, 0.85, 1.0);
        } else if (character == 'R') {
            drawNormalizedLine(0.0, 1.0, 0.0, 0.0);
            drawNormalizedLine(0.0, 0.0, 0.75, 0.0);
            drawNormalizedLine(0.75, 0.0, 1.0, 0.25);
            drawNormalizedLine(1.0, 0.25, 0.75, 0.5);
            drawNormalizedLine(0.75, 0.5, 0.0, 0.5);
            drawNormalizedLine(0.55, 0.5, 1.0, 1.0);
        } else if (character == 'm') {
            drawNormalizedLine(0.0, 1.0, 0.0, 0.35);
            drawNormalizedLine(0.0, 0.35, 0.35, 0.15);
            drawNormalizedLine(0.35, 0.15, 0.5, 0.35);
            drawNormalizedLine(0.5, 0.35, 0.8, 0.15);
            drawNormalizedLine(0.8, 0.15, 1.0, 0.35);
            drawNormalizedLine(1.0, 0.35, 1.0, 1.0);
        }
        x += advance;
    }
    painter.restore();
}

} // namespace

PlatePdfLayout CalculatePlatePdfLayout(const PlateFlatPattern& pattern, PlatePdfOptions options)
{
    ValidateOptions(options);
    const PatternBounds bounds = MeasurePattern(pattern);
    if (options.orientation == PlatePdfOrientation::Portrait) {
        return LayoutForOrientation(bounds, options, QPageLayout::Portrait);
    }
    if (options.orientation == PlatePdfOrientation::Landscape) {
        return LayoutForOrientation(bounds, options, QPageLayout::Landscape);
    }

    const PlatePdfLayout portrait = LayoutForOrientation(bounds, options, QPageLayout::Portrait);
    const PlatePdfLayout landscape = LayoutForOrientation(bounds, options, QPageLayout::Landscape);
    if (portrait.PageCount() != landscape.PageCount()) {
        return portrait.PageCount() < landscape.PageCount() ? portrait : landscape;
    }
    const double patternAspect = portrait.patternBoundsMillimeters.width()
        / portrait.patternBoundsMillimeters.height();
    const double portraitDifference = std::abs(
        patternAspect - portrait.tileSizeMillimeters.width() / portrait.tileSizeMillimeters.height());
    const double landscapeDifference = std::abs(
        patternAspect - landscape.tileSizeMillimeters.width() / landscape.tileSizeMillimeters.height());
    return portraitDifference <= landscapeDifference ? portrait : landscape;
}

void WritePlateFlatPatternPdf(
    const QString& path,
    const PlateFlatPattern& pattern,
    PlatePdfOptions options)
{
    if (path.trimmed().isEmpty()) {
        throw std::invalid_argument("Plate PDF output path is empty.");
    }
    const PlatePdfLayout layout = CalculatePlatePdfLayout(pattern, options);
    const PatternBounds bounds = MeasurePattern(pattern);
    std::vector<QPainterPath> outerPaths;
    outerPaths.reserve(std::max<std::size_t>(1, pattern.pieces.size()));
    if (pattern.pieces.empty()) {
        outerPaths.push_back(ToPainterPath(pattern.outerBoundary, bounds));
    } else {
        for (const auto& piece : pattern.pieces) {
            outerPaths.push_back(ToPainterPath(piece.outerBoundary, bounds));
        }
    }
    std::vector<QPainterPath> openingPaths;
    openingPaths.reserve(pattern.openings.size());
    for (const auto& opening : pattern.openings) {
        openingPaths.push_back(ToPainterPath(opening, bounds));
    }
    std::vector<QPainterPath> foldPaths;
    foldPaths.reserve(pattern.foldLines.size());
    for (const auto& fold : pattern.foldLines) {
        foldPaths.push_back(ToPainterPath(fold, bounds));
    }
    std::vector<QPainterPath> reliefCutPaths;
    reliefCutPaths.reserve(pattern.reliefCuts.size());
    for (const auto& cut : pattern.reliefCuts) {
        if (!cut.incorporatedInOuterBoundary) {
            reliefCutPaths.push_back(ToPainterPath(cut, bounds));
        }
    }
    const auto registrationMarks = RegistrationMarks(layout);

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        throw std::runtime_error("Plate PDF output file could not be opened.");
    }
    {
        QPdfWriter writer(&output);
        writer.setTitle(QString::fromUtf8(pattern.plateName));
        writer.setCreator(QStringLiteral("kachakachaCAD"));
        writer.setResolution(kPdfResolution);
        QPageLayout pageLayout(
            QPageSize(options.pageSize),
            layout.orientation,
            QMarginsF(0.0, 0.0, 0.0, 0.0),
            QPageLayout::Millimeter);
        pageLayout.setMode(QPageLayout::FullPageMode);
        if (!writer.setPageLayout(pageLayout)) {
            throw std::runtime_error("Plate PDF page layout could not be applied.");
        }

        QPainter painter;
        if (!painter.begin(&writer)) {
            throw std::runtime_error("Plate PDF drawing could not be started.");
        }
        painter.setRenderHint(QPainter::Antialiasing, true);
        const double pixelsPerMillimeter = static_cast<double>(kPdfResolution) / kMillimetersPerInch;
        const double drawingOffsetX = layout.columns == 1
            ? (layout.tileSizeMillimeters.width() - layout.patternBoundsMillimeters.width()) * 0.5
            : 0.0;
        const double drawingOffsetY = layout.rows == 1
            ? (layout.tileSizeMillimeters.height() - layout.patternBoundsMillimeters.height()) * 0.5
            : 0.0;
        int pageNumber = 0;
        for (int row = 0; row < layout.rows; ++row) {
            for (int column = 0; column < layout.columns; ++column) {
                if (pageNumber > 0 && !writer.newPage()) {
                    painter.end();
                    throw std::runtime_error("Plate PDF could not create another page.");
                }
                ++pageNumber;
                const QPointF tileOrigin(
                    column * layout.horizontalStepMillimeters,
                    row * layout.verticalStepMillimeters);
                const QRectF tileRectangle(tileOrigin, layout.tileSizeMillimeters);

                painter.save();
                painter.setClipRect(QRectF(
                    options.marginMillimeters * pixelsPerMillimeter,
                    options.marginMillimeters * pixelsPerMillimeter,
                    layout.tileSizeMillimeters.width() * pixelsPerMillimeter,
                    layout.tileSizeMillimeters.height() * pixelsPerMillimeter));
                painter.translate(
                    (options.marginMillimeters + drawingOffsetX - tileOrigin.x()) * pixelsPerMillimeter,
                    (options.marginMillimeters + drawingOffsetY - tileOrigin.y()) * pixelsPerMillimeter);
                painter.scale(pixelsPerMillimeter, pixelsPerMillimeter);
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(Qt::black, 0.15, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                for (const QPainterPath& outerPath : outerPaths) {
                    painter.drawPath(outerPath);
                }
                for (const QPainterPath& opening : openingPaths) {
                    painter.drawPath(opening);
                }
                painter.setPen(QPen(QColor("#d12f3f"), 0.16, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                for (const QPainterPath& cut : reliefCutPaths) {
                    painter.drawPath(cut);
                }
                painter.setPen(QPen(QColor("#4c5963"), 0.1, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
                for (const QPainterPath& fold : foldPaths) {
                    painter.drawPath(fold);
                }
                painter.setPen(QPen(QColor("#4c5963"), 0.1));
                for (const RegistrationMark& mark : registrationMarks) {
                    if (ContainsWithTolerance(tileRectangle, mark.point)) {
                        DrawRegistrationMark(
                            painter,
                            mark.point - QPointF(drawingOffsetX, drawingOffsetY),
                            mark.halfSizeMillimeters);
                    }
                }
                painter.restore();

                painter.save();
                painter.setPen(QPen(Qt::black, std::max(1.0, pixelsPerMillimeter * 0.15)));
                const double footerTop = options.marginMillimeters
                    + layout.tileSizeMillimeters.height();
                const double scaleY = footerTop + 6.0;
                const double scaleStartX = options.marginMillimeters;
                const double scaleEndX = scaleStartX + 100.0;
                painter.drawLine(
                    QPointF(scaleStartX * pixelsPerMillimeter, scaleY * pixelsPerMillimeter),
                    QPointF(scaleEndX * pixelsPerMillimeter, scaleY * pixelsPerMillimeter));
                for (double x : {scaleStartX, scaleEndX}) {
                    painter.drawLine(
                        QPointF(x * pixelsPerMillimeter, (scaleY - 1.5) * pixelsPerMillimeter),
                        QPointF(x * pixelsPerMillimeter, (scaleY + 1.5) * pixelsPerMillimeter));
                }
                DrawStrokeText(
                    painter,
                    QRectF(
                        (scaleEndX + 3.0) * pixelsPerMillimeter,
                        (footerTop + 1.0) * pixelsPerMillimeter,
                        35.0 * pixelsPerMillimeter,
                        10.0 * pixelsPerMillimeter),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    QStringLiteral("100 mm"));
                DrawStrokeText(
                    painter,
                    QRectF(
                        (layout.pageSizeMillimeters.width() - options.marginMillimeters - 45.0) * pixelsPerMillimeter,
                        (footerTop + 1.0) * pixelsPerMillimeter,
                        45.0 * pixelsPerMillimeter,
                        10.0 * pixelsPerMillimeter),
                    Qt::AlignVCenter | Qt::AlignRight,
                    QStringLiteral("%1/%2  C%3 R%4")
                        .arg(pageNumber)
                        .arg(layout.PageCount())
                        .arg(column + 1)
                        .arg(row + 1));
                painter.restore();
            }
        }
        painter.end();
    }
    if (!output.commit()) {
        throw std::runtime_error("Plate PDF output could not be finalized.");
    }
}

} // namespace kachakacha::qtio
