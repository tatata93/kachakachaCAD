#pragma once
#include "kachakacha/view/ViewOrientation.h"
class QPainter;
class QRectF;
class QColor;
void V2DrawNavigatorHighlight(QPainter& painter,const QRectF& box,
    const kachakacha::v2::view::Quaternion& orientation,
    const kachakacha::v2::view::ViewCubeZone& zone,const QColor& color);
