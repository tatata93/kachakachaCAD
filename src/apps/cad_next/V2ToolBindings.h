#pragma once

#include "kachakacha/modeling/ToolController.h"
#include <array>
#include <string_view>

namespace v2ui {
using kachakacha::v2::modeling::DrawingTool;
struct ToolBinding {
    std::string_view commandId;
    DrawingTool tool;
};

inline constexpr std::array<ToolBinding, 22> kToolBindings{{
    {"selection.activate", DrawingTool::Select},
    {"grid.move_origin", DrawingTool::SetGridOrigin},
    {"draw.point", DrawingTool::Point},
    {"draw.line", DrawingTool::Line},
    {"draw.polyline", DrawingTool::Polyline},
    {"draw.rectangle", DrawingTool::Rectangle},
    {"draw.circle", DrawingTool::Circle},
    {"draw.arc", DrawingTool::Arc},
    {"draw.bezier", DrawingTool::Bezier},
    {"draw.spline", DrawingTool::Spline},
    {"wire.trim", DrawingTool::Trim},
    {"wire.extend", DrawingTool::Extend},
    {"wire.move", DrawingTool::Move},
    {"wire.copy", DrawingTool::Copy},
    {"wire.mirror", DrawingTool::Mirror},
    {"wire.rotate", DrawingTool::Rotate},
    {"wire.scale", DrawingTool::Scale},
    {"measure.open", DrawingTool::Measure},
    // 部品の配置(P-18)。線と同じ道具・同じ点の置き方で、選んだ部品を動かす。
    {"part.move", DrawingTool::Move},
    {"part.copy", DrawingTool::Copy},
    {"part.mirror", DrawingTool::Mirror},
    {"part.rotate", DrawingTool::Rotate},
}};

} // namespace v2ui
