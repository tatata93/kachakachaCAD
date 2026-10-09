#include "V2Viewport.h"
#include <QFontMetrics>
#include <QPainter>
#include <QStringList>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <algorithm>

void V2Viewport::SetMeasureOverlay(kachakacha::v2::app::MeasureOverlay overlay)
{
    measureOverlay_ = std::move(overlay);
    update();
}

void V2Viewport::DrawMeasureOverlay(QPainter& painter) const
{
    if (!session_ || session_->CurrentTool() != kachakacha::v2::modeling::DrawingTool::Measure)
        return;
    painter.save();
    painter.setPen(QPen(palette_.selected, 2.0));
    painter.setBrush(palette_.background);
    std::optional<QPointF> anchor;
    for (const auto& line : measureOverlay_.lines) {
        for (std::size_t index = 1; index < line.size(); ++index) {
            const auto a = ToScreen(line[index - 1]);
            const auto b = ToScreen(line[index]);
            if (a && b) {
                painter.drawLine(*a, *b);
                if (!anchor) anchor = (*a + *b) * 0.5;
            }
        }
    }
    for (const auto& point : measureOverlay_.points) {
        if (const auto screen = ToScreen(point)) {
            painter.drawEllipse(*screen, 4.0, 4.0);
            if (!anchor) anchor = *screen;
        }
    }
    const QFontMetrics metrics(painter.font());
    const int lineHeight = metrics.height() + 4;
    const int available = std::max(1, (height() - 110) / lineHeight - 1);
    const int count = std::min(available, static_cast<int>(measureOverlay_.rows.size()));
    QStringList lines;
    for (int index = 0; index < count; ++index) {
        const auto& row = measureOverlay_.rows[static_cast<std::size_t>(index)];
        lines.push_back(QString::fromStdString(row.labelJa + "  " + row.valueJa));
    }
    const int remaining = static_cast<int>(measureOverlay_.rows.size()) - count;
    if (remaining > 0) lines.push_back(QString::fromUtf8("ほか %1 項目は右ペイン").arg(remaining));
    if (lines.empty()) { painter.restore(); return; }
    int textWidth = 0;
    for (const auto& text : lines) textWidth = std::max(textWidth, metrics.horizontalAdvance(text));
    const int boxWidth = std::min(std::max(80, std::min(560, width() - 24)), textWidth + 20);
    const int boxHeight = static_cast<int>(lines.size()) * lineHeight + 12;
    const double x = std::clamp(anchor ? anchor->x() + 24.0 : 16.0,
        12.0, static_cast<double>(std::max(12, width() - boxWidth - 12)));
    const double y = std::clamp(anchor ? anchor->y() + 24.0 : 80.0,
        70.0, static_cast<double>(std::max(70, height() - boxHeight - 12)));
    const QRectF box(x, y, boxWidth, boxHeight);
    if (anchor) painter.drawLine(*anchor, box.topLeft());
    painter.setPen(QPen(palette_.text, 1.0));
    painter.drawRect(box);
    painter.setClipRect(box.adjusted(5, 3, -5, -3));
    for (int index = 0; index < lines.size(); ++index) {
        const auto text = metrics.elidedText(lines[index], Qt::ElideRight, boxWidth - 16);
        painter.drawText(QPointF(x + 8, y + 6 + metrics.ascent() + index * lineHeight), text);
    }
    painter.restore();
}
