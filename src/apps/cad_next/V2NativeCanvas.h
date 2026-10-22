#pragma once
#include <functional>
class QWidget;
class QPainter;
class QPaintDevice;

// Presentation only. Input and CAD/picking state remain on the viewport widget.
QWidget* V2CreateNativeCanvas(QWidget* parent,
    std::function<bool(QPaintDevice&, double)> paint);
bool V2NativeCanvasActive(const QWidget* canvas);
void V2CompleteNativeCanvas(QWidget* canvas);
