#pragma once
#include "kachakacha/app/SolidOutputMode.h"
#include "kachakacha/modeling/ShapeMesh.h"
class V2Viewport;
void ShowSolidOutputPreview(V2Viewport& viewport, const kachakacha::v2::modeling::ShapeMesh& mesh,
    kachakacha::v2::app::SolidOutputMode mode);
