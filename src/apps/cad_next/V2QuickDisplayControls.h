#pragma once
#include "kachakacha/base/Ids.h"
class V2Viewport;
class QToolBar;
class QCheckBox;
namespace kachakacha::v2::app {class DrawingSession;}
// Display-only controls and their geometric visibility cache; no document ownership.
void InstallQuickDisplayControls(V2Viewport& viewport,QToolBar& bar,kachakacha::v2::app::DrawingSession& session);
void BindQuickGridControl(V2Viewport& viewport,QCheckBox& check);
void RefreshQuickGridVisibility(V2Viewport& viewport);
void RefreshPlaneWireDisplay(V2Viewport& viewport);
bool PlaneWireDisplayActive(const V2Viewport& viewport);
bool PlaneWireDisplayAllows(const V2Viewport& viewport,kachakacha::v2::base::EntityId id);
