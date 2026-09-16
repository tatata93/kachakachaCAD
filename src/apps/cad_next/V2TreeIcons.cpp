#include "V2TreeIcons.h"

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

namespace {

constexpr int kIconSize = 18;

QPixmap BlankIcon()
{
    QPixmap pixmap(kIconSize, kIconSize);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

QPen IconPen(const QColor& color, double width = 1.7)
{
    QPen pen(color, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

QIcon PointIcon()
{
    QPixmap pixmap = BlankIcon();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(IconPen(QColor(0xB4, 0x52, 0x36), 2.0));
    painter.setBrush(QColor(0xFF, 0xD0, 0x5A));
    painter.drawEllipse(QPointF(9.0, 9.0), 3.0, 3.0);
    return QIcon(pixmap);
}

QIcon WireIcon()
{
    QPixmap pixmap = BlankIcon();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(IconPen(QColor(0x18, 0x6E, 0xA8), 2.2));
    painter.drawPolyline(QPolygonF({QPointF(2.5, 13.5), QPointF(6.5, 5.0),
        QPointF(11.0, 11.5), QPointF(15.5, 4.0)}));
    return QIcon(pixmap);
}

QIcon PlaneIcon(bool guide)
{
    QPixmap pixmap = BlankIcon();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor edge = guide ? QColor(0x13, 0x78, 0x61) : QColor(0x35, 0x7E, 0x88);
    QColor fill = guide ? QColor(0x72, 0xC5, 0x98) : QColor(0x86, 0xC7, 0xD1);
    fill.setAlpha(150);
    painter.setPen(IconPen(edge, 1.5));
    painter.setBrush(fill);
    painter.drawPolygon(QPolygonF({QPointF(2.5, 7.0), QPointF(11.5, 3.0),
        QPointF(15.5, 10.5), QPointF(6.5, 15.0)}));
    return QIcon(pixmap);
}

QIcon SolidIcon(const QColor& fill)
{
    QPixmap pixmap = BlankIcon();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(IconPen(QColor(0x45, 0x4B, 0x55), 1.3));
    painter.setBrush(fill);
    painter.drawPolygon(QPolygonF({QPointF(3.0, 6.0), QPointF(9.0, 2.5),
        QPointF(15.0, 6.0), QPointF(9.0, 9.5)}));
    painter.drawPolygon(QPolygonF({QPointF(3.0, 6.0), QPointF(9.0, 9.5),
        QPointF(9.0, 16.0), QPointF(3.0, 12.5)}));
    painter.drawPolygon(QPolygonF({QPointF(9.0, 9.5), QPointF(15.0, 6.0),
        QPointF(15.0, 12.5), QPointF(9.0, 16.0)}));
    return QIcon(pixmap);
}

QIcon PatternIcon()
{
    QPixmap pixmap = BlankIcon();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(IconPen(QColor(0x8B, 0x62, 0x12), 1.5));
    painter.setBrush(QColor(0xF3, 0xD5, 0x73));
    painter.drawRect(QRectF(3.0, 3.0, 12.0, 12.0));
    painter.setPen(QPen(QColor(0x8B, 0x62, 0x12), 1.0, Qt::DashLine));
    painter.drawLine(QPointF(5.0, 9.0), QPointF(13.0, 9.0));
    return QIcon(pixmap);
}

} // namespace

QIcon V2EntityTreeIcon(kachakacha::v2::domain::EntityKind kind)
{
    using kachakacha::v2::domain::EntityKind;
    switch (kind) {
    case EntityKind::Point:            return PointIcon();
    case EntityKind::WorkPlane:        return PlaneIcon(false);
    case EntityKind::Wire:             return WireIcon();
    case EntityKind::GuideSurface:     return PlaneIcon(true);
    case EntityKind::Part:             return SolidIcon(QColor(0x82, 0xA9, 0xD1));
    case EntityKind::FabricationModel: return SolidIcon(QColor(0xE0, 0x93, 0x45));
    case EntityKind::Pattern:          return PatternIcon();
    }
    return QIcon();
}

QIcon V2GroupTreeIcon()
{
    QPixmap pixmap = BlankIcon();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(IconPen(QColor(0x8A, 0x67, 0x18), 1.3));
    painter.setBrush(QColor(0xE8, 0xC4, 0x55));
    painter.drawRoundedRect(QRectF(2.0, 5.0, 14.0, 10.0), 1.5, 1.5);
    painter.drawRect(QRectF(3.0, 3.0, 6.0, 3.5));
    return QIcon(pixmap);
}

QIcon V2OriginTreeIcon()
{
    QPixmap pixmap = BlankIcon();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(IconPen(QColor(0x58, 0x60, 0x68), 1.4));
    painter.drawEllipse(QPointF(9.0, 9.0), 3.0, 3.0);
    painter.drawLine(QPointF(1.5, 9.0), QPointF(16.5, 9.0));
    painter.drawLine(QPointF(9.0, 1.5), QPointF(9.0, 16.5));
    return QIcon(pixmap);
}

QIcon V2AxisTreeIcon(int axis)
{
    const QColor colors[3] = {QColor(0xC9, 0x3D, 0x3D), QColor(0x24, 0x91, 0x4B),
        QColor(0x2F, 0x6F, 0xCE)};
    const QColor color = colors[axis >= 0 && axis < 3 ? axis : 0];
    QPixmap pixmap = BlankIcon();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(IconPen(color, 2.0));
    painter.drawLine(QPointF(3.0, 14.0), QPointF(14.0, 3.0));
    painter.drawLine(QPointF(10.0, 3.5), QPointF(14.0, 3.0));
    painter.drawLine(QPointF(13.5, 7.0), QPointF(14.0, 3.0));
    return QIcon(pixmap);
}
