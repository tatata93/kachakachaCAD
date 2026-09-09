#pragma once
//! Qt の当て木、絵と窓の側。宣言だけ。
#include "QtStubCore.h"

class QPoint {
public:
    QPoint() = default;
    QPoint(int, int) {}
    [[nodiscard]] int x() const;
    [[nodiscard]] int y() const;
    QPoint operator-(const QPoint&) const;
    QPoint operator+(const QPoint&) const;
};

class QPointF {
public:
    QPointF() = default;
    QPointF(double, double) {}
    QPointF(const QPoint&) {}
    [[nodiscard]] double x() const;
    [[nodiscard]] double y() const;
    [[nodiscard]] QPoint toPoint() const;
    [[nodiscard]] double manhattanLength() const;
    QPointF operator+(const QPointF&) const;
    QPointF operator-(const QPointF&) const;
    QPointF operator*(double) const;
};

class QSize {
public:
    QSize() = default;
    QSize(int, int) {}
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    void setWidth(int);
    void setHeight(int);
    [[nodiscard]] QSize expandedTo(const QSize&) const;
    [[nodiscard]] QSize boundedTo(const QSize&) const;
    [[nodiscard]] bool isEmpty() const;
    QSize operator*(double) const;
};

class QRect {
public:
    QRect() = default;
    QRect(int, int, int, int) {}
    void setSize(const QSize&);
    void moveCenter(const QPoint&);
    void moveTo(int, int);
    void moveTopLeft(const QPoint&);
    [[nodiscard]] bool isValid() const;
    void setTop(int);
    void setBottom(int);
    void setLeft(int);
    void setRight(int);
    void translate(int, int);
    void translate(const QPoint&);
    [[nodiscard]] QPoint topLeft() const;
    [[nodiscard]] QPoint topRight() const;
    [[nodiscard]] QPoint bottomLeft() const;
    [[nodiscard]] QPoint bottomRight() const;
    [[nodiscard]] int x() const;
    [[nodiscard]] int y() const;
    [[nodiscard]] int left() const;
    [[nodiscard]] int right() const;
    [[nodiscard]] int top() const;
    [[nodiscard]] int bottom() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] QPoint center() const;
    [[nodiscard]] QRect adjusted(int, int, int, int) const;
    [[nodiscard]] QRect translated(int, int) const;
    [[nodiscard]] bool contains(const QPoint&) const;
    [[nodiscard]] bool isEmpty() const;
    void adjust(int, int, int, int);
    void setWidth(int);
    void setHeight(int);
};

class QRectF {
public:
    QRectF() = default;
    QRectF(double, double, double, double) {}
    QRectF(const QRect&) {}
    QRectF(const QPointF&, const QSize&) {}
    [[nodiscard]] double x() const;
    [[nodiscard]] double y() const;
    [[nodiscard]] double left() const;
    [[nodiscard]] double right() const;
    [[nodiscard]] double top() const;
    [[nodiscard]] double bottom() const;
    [[nodiscard]] double width() const;
    [[nodiscard]] double height() const;
    [[nodiscard]] QPointF center() const;
    [[nodiscard]] QRectF adjusted(double, double, double, double) const;
    [[nodiscard]] bool contains(const QPointF&) const;
    [[nodiscard]] QRect toRect() const;
};

class QColor {
public:
    QColor() = default;
    QColor(int, int, int, int = 255) {}
    QColor(Qt::GlobalColor) {}
    [[nodiscard]] int red() const;
    [[nodiscard]] int green() const;
    [[nodiscard]] int blue() const;
    [[nodiscard]] int alpha() const;
    void setAlpha(int);
    [[nodiscard]] QColor lighter(int = 150) const;
    [[nodiscard]] QColor darker(int = 200) const;
    [[nodiscard]] bool isValid() const;
    bool operator==(const QColor&) const;
    bool operator!=(const QColor&) const;
};

class QBrush {
public:
    QBrush() = default;
    QBrush(const QColor&) {}
    QBrush(const QColor&, Qt::BrushStyle) {}
    QBrush(Qt::GlobalColor) {}
    QBrush(const class QPixmap&) {}
    [[nodiscard]] QColor color() const;
};

class QPen {
public:
    QPen() = default;
    QPen(const QColor&) {}
    QPen(const QColor&, double) {}
    QPen(const QColor&, double, Qt::PenStyle) {}
    QPen(const QBrush&, double) {}
    QPen(Qt::PenStyle) {}
    void setWidthF(double);
    void setColor(const QColor&);
    void setStyle(Qt::PenStyle);
};

class QFont {
public:
    QFont() = default;
    QFont(const QString&, int = -1, int = -1, bool = false) {}
    void setPointSize(int);
    void setPointSizeF(double);
    void setBold(bool);
    void setFamily(const QString&);
    [[nodiscard]] int pointSize() const;
    [[nodiscard]] double pointSizeF() const;
    [[nodiscard]] QString family() const;
    enum Weight { Normal = 400, Bold = 700 };
    void setWeight(Weight);
    enum StyleStrategy { PreferDefault, PreferBitmap, PreferDevice, PreferOutline,
        ForceOutline, PreferMatch, PreferQuality, PreferAntialias, NoAntialias };
    void setStyleStrategy(StyleStrategy);
    enum StyleHint { Helvetica, SansSerif, Times, Serif, Courier, TypeWriter, System, AnyStyle };
    void setStyleHint(StyleHint, StyleStrategy = PreferDefault);
    void setPixelSize(int);
    void setItalic(bool);
    void setUnderline(bool);
    [[nodiscard]] bool bold() const;
};

class QFontMetrics {
public:
    explicit QFontMetrics(const QFont&) {}
    [[nodiscard]] int height() const;
    [[nodiscard]] int ascent() const;
    [[nodiscard]] int horizontalAdvance(const QString&) const;
    [[nodiscard]] QRect boundingRect(const QString&) const;
    [[nodiscard]] int descent() const;
    [[nodiscard]] QString elidedText(const QString&, Qt::TextElideMode, int, int = 0) const;
};

class QFontDatabase {
public:
    enum SystemFont { GeneralFont, FixedFont };
    static QFont systemFont(SystemFont);
    static QStringList families();
};

class QPolygonF {
public:
    QPolygonF() = default;
    QPolygonF(std::initializer_list<QPointF>) {}
    QPolygonF& operator<<(const QPointF&);
    [[nodiscard]] int size() const;
};

class QPolygon {
public:
    QPolygon() = default;
    QPolygon(std::initializer_list<QPoint>) {}
    QPolygon& operator<<(const QPoint&);
};

class QPainterPath {
public:
    QPainterPath() = default;
    void moveTo(const QPointF&);
    void lineTo(const QPointF&);
    void cubicTo(const QPointF&, const QPointF&, const QPointF&);
    void arcTo(const QRectF&, double, double);
    void closeSubpath();
    [[nodiscard]] QPointF currentPosition() const;
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] int elementCount() const;
};

class QPaintDevice {
public:
    virtual ~QPaintDevice() = default;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
};

class QImage : public QPaintDevice {
public:
    enum Format { Format_ARGB32, Format_RGB32, Format_ARGB32_Premultiplied };
    QImage() = default;
    QImage(int, int, Format) {}
    QImage(const QSize&, Format) {}
    void fill(const QColor&);
    void fill(Qt::GlobalColor);
    [[nodiscard]] bool save(const QString&, const char* = nullptr, int = -1) const;
    [[nodiscard]] bool isNull() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    void setDevicePixelRatio(double);
};

class QPixmap : public QPaintDevice {
public:
    QPixmap() = default;
    QPixmap(int, int) {}
    QPixmap(const QSize&) {}
    [[nodiscard]] QRect rect() const;
    void fill(const QColor&);
    [[nodiscard]] bool isNull() const;
    [[nodiscard]] QImage toImage() const;
    static QPixmap fromImage(const QImage&);
    [[nodiscard]] QPixmap scaled(const QSize&, Qt::AspectRatioMode = Qt::IgnoreAspectRatio,
        Qt::TransformationMode = Qt::FastTransformation) const;
};

class QIcon {
public:
    QIcon() = default;
    QIcon(const QPixmap&) {}
    enum Mode { Normal, Disabled, Active, Selected };
    enum State { Off, On };
    [[nodiscard]] QPixmap pixmap(const QSize&, Mode = Normal, State = Off) const;
    [[nodiscard]] QPixmap pixmap(int, int, Mode = Normal, State = Off) const;
    [[nodiscard]] bool isNull() const;
};

//! カーソルの形。当て木は形を持つだけでよい。
class QCursor {
public:
    QCursor() = default;
    QCursor(Qt::CursorShape) {}
    [[nodiscard]] static QPoint pos();
};

class QPalette {
public:
    enum ColorRole { Window, WindowText, Base, AlternateBase, Text, Button, ButtonText,
        BrightText, Highlight, HighlightedText, Light, Midlight, Dark, Mid, Shadow,
        ToolTipBase, ToolTipText, PlaceholderText, Link, LinkVisited, NoRole };
    enum ColorGroup { Active, Disabled, Inactive, All };
    QPalette() = default;
    void setColor(ColorRole, const QColor&);
    void setColor(ColorGroup, ColorRole, const QColor&);
    [[nodiscard]] QColor color(ColorRole) const;
    [[nodiscard]] QColor color(ColorGroup, ColorRole) const;
    [[nodiscard]] QBrush brush(ColorRole) const;
    [[nodiscard]] QBrush window() const;
    [[nodiscard]] QBrush button() const;
    [[nodiscard]] QColor windowText() const;
};


class QPainter {
public:
    enum RenderHint { Antialiasing = 1, TextAntialiasing = 2, SmoothPixmapTransform = 4 };
    enum CompositionMode { CompositionMode_SourceOver };
    QPainter() = default;
    explicit QPainter(QPaintDevice*) {}
    void setRenderHint(RenderHint, bool = true);
    void drawPixmap(const QPoint&, const QPixmap&);
    void drawPoint(int, int);
    void drawPoint(const QPoint&);
    void drawPoint(const QPointF&);
    void setPen(const QPen&);
    void setPen(const QColor&);
    void setPen(Qt::PenStyle);
    void setBrush(const QBrush&);
    void setBrush(const QColor&);
    void setBrush(Qt::BrushStyle);
    void setFont(const QFont&);
    [[nodiscard]] QFont font() const;
    [[nodiscard]] QPen pen() const;
    [[nodiscard]] QBrush brush() const;
    void save();
    void restore();
    void translate(double, double);
    void rotate(double);
    void scale(double, double);
    void setClipRect(const QRect&);
    void setClipRect(const QRectF&);
    void fillRect(const QRect&, const QColor&);
    void fillRect(const QRect&, const QBrush&);
    void fillRect(const QRectF&, const QColor&);
    void fillRect(const QRectF&, const QBrush&);
    void drawRect(int, int, int, int);
    void drawEllipse(int, int, int, int);
    void fillRect(int, int, int, int, const QColor&);
    void fillRect(int, int, int, int, const QBrush&);
    void drawRect(const QRect&);
    void drawRect(const QRectF&);
    void drawLine(const QPointF&, const QPointF&);
    void drawLine(const QPoint&, const QPoint&);
    void drawLine(int, int, int, int);
    void drawEllipse(const QRectF&);
    void drawEllipse(const QPointF&, double, double);
    void drawPath(const QPainterPath&);
    void drawArc(const QRectF&, int, int);
    void drawRoundedRect(const QRectF&, double, double);
    void drawPolygon(const QPolygonF&);
    void drawPolygon(const QPolygon&);
    void drawPolyline(const QPolygonF&);
    void drawText(const QPointF&, const QString&);
    void drawText(const QRect&, int, const QString&);
    void drawText(const QRectF&, int, const QString&);
    void drawPixmap(const QRect&, const QPixmap&);
    void drawImage(const QRect&, const QImage&);
    [[nodiscard]] QFontMetrics fontMetrics() const;
    [[nodiscard]] bool isActive() const;
    bool end();
};

class QMouseEvent;
class QKeyEvent;
class QWheelEvent;
class QResizeEvent;
class QPaintEvent;
class QCloseEvent;
class QEvent {
public:
    enum Type { None, MouseButtonPress, MouseButtonRelease, MouseMove, KeyPress, Paint,
        Resize };
    [[nodiscard]] Type type() const;
    void accept();
    void ignore();
};

class QMouseEvent : public QEvent {
public:
    [[nodiscard]] QPointF position() const;
    [[nodiscard]] Qt::MouseButton button() const;
    [[nodiscard]] Qt::MouseButtons buttons() const;
    [[nodiscard]] Qt::KeyboardModifiers modifiers() const;
};

class QKeyEvent : public QEvent {
public:
    [[nodiscard]] int key() const;
    [[nodiscard]] Qt::KeyboardModifiers modifiers() const;
    [[nodiscard]] QString text() const;
};

class QPoint;
class QWheelEvent : public QEvent {
public:
    [[nodiscard]] QPoint angleDelta() const;
    [[nodiscard]] QPointF position() const;
    [[nodiscard]] Qt::KeyboardModifiers modifiers() const;
};

class QResizeEvent : public QEvent {
public:
    [[nodiscard]] QSize size() const;
    [[nodiscard]] QSize oldSize() const;
};

class QPaintEvent : public QEvent {
public:
    [[nodiscard]] QRect rect() const;
};

class QKeySequence {
public:
    QKeySequence() = default;
    QKeySequence(const QString&) {}
    QKeySequence(int) {}
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] QString toString() const;
    bool operator==(const QKeySequence&) const;
};
