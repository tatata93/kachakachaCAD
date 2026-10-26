#pragma once
#include <QString>
class V2Viewport;
class QToolBar;
class QWidget;

// A separate selection for editing while the current tool keeps its inputs.
namespace V2ToolSelection {
bool Active(const V2Viewport& view);
bool HasTool(const V2Viewport& view);
bool Key(V2Viewport& view,int key,bool typing);
void Install(V2Viewport& view,QToolBar& bar);
void Begin(V2Viewport& view);
void End(V2Viewport& view);
void Refresh(V2Viewport& view);
void Reset(V2Viewport& view);
void Caption(QWidget& window,const QString& path);
}
