//! 3D の左上の HUD(正本 3 HTML 2026-09-18、指示書 C-11)。
//!
//! 1行目 = モード › 道具、2行目 = 案内、測定を重ねていれば戻り先。
//! 文言は窓が core(app/StatusLine)から持ってくる。ここは描くだけ。
//! 3D を見ながら次に何をするかが読める。下の帯まで目を落とさなくてよい。

#include "V2Viewport.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <algorithm>
#include <utility>

void V2Viewport::SetHoverChangedCallback(std::function<void()> callback)
{
    hoverChangedCallback_ = std::move(callback);
}

void V2Viewport::SetMeasureResumeAvailable(std::function<bool()> query)
{
    measureResumeAvailable_ = std::move(query);
}

void V2Viewport::SetHudLines(std::vector<QString> lines)
{
    if (lines == hudLines_) {
        return;
    }
    hudLines_ = std::move(lines);
    update();
}

QRectF V2Viewport::HudRect() const
{
    if (hudLines_.empty()) {
        return QRectF();
    }
    const QFontMetrics metrics(font());
    double widest = 0.0;
    for (const QString& line : hudLines_) {
        widest = std::max(widest, static_cast<double>(metrics.horizontalAdvance(line)));
    }
    const double lineHeight = static_cast<double>(metrics.height());
    const double padding = 6.0;
    const double boxWidth = std::min(widest + padding * 2.0, static_cast<double>(width()) - 16.0);
    return QRectF(8.0, 8.0, boxWidth,
        lineHeight * static_cast<double>(hudLines_.size()) + padding * 2.0);
}

void V2Viewport::DrawHud(QPainter& painter) const
{
    const QRectF box = HudRect();
    if (hudLines_.empty() || box.width() <= 0.0) {
        return;
    }
    painter.save();
    QColor back = palette_.background;
    back.setAlpha(190);
    painter.setPen(Qt::NoPen);
    painter.setBrush(back);
    painter.drawRoundedRect(box, 4.0, 4.0);
    const QFontMetrics metrics(font());
    const double lineHeight = static_cast<double>(metrics.height());
    double y = box.top() + 6.0 + static_cast<double>(metrics.ascent());
    for (std::size_t index = 0; index < hudLines_.size(); ++index) {
        // 1行目(モード › 道具)は太く。案内は普通の太さ。
        QFont lineFont = font();
        lineFont.setBold(index == 0);
        painter.setFont(lineFont);
        painter.setPen(QPen(index == 0 ? palette_.selected : palette_.text, 1.0));
        painter.drawText(QPointF(box.left() + 6.0, y),
            metrics.elidedText(hudLines_[index], Qt::ElideRight, static_cast<int>(box.width() - 12.0)));
        y += lineHeight;
    }
    painter.restore();
}
