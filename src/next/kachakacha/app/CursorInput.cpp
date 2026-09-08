#include "kachakacha/app/CursorInput.h"

#include "kachakacha/geometry/Units.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::app {
namespace {

using base::Diagnostic;
using base::MakeError;
using base::Result;

constexpr double kPi = 3.14159265358979323846;
[[nodiscard]] std::vector<CursorField> MakePlanarLineFields()
{
    return {{"length", "長さ", QuantityKind::Length, true},
        {"angle", "平面内角度", QuantityKind::Angle, false},
        {"du", "du", QuantityKind::Length, false},
        {"dv", "dv", QuantityKind::Length, false}};
}

[[nodiscard]] std::vector<CursorField> MakeSpatialLineFields()
{
    return {{"length", "長さ", QuantityKind::Length, true},
        {"dx", "dX", QuantityKind::Length, false},
        {"dy", "dY", QuantityKind::Length, false},
        {"dz", "dZ", QuantityKind::Length, false},
        {"angle_x", "世界Xとの角度", QuantityKind::Angle, false},
        {"angle_y", "世界Yとの角度", QuantityKind::Angle, false},
        {"angle_z", "世界Zとの角度", QuantityKind::Angle, false}};
}

[[nodiscard]] std::vector<CursorField> MakeCircleFields()
{
    return {{"radius", "半径", QuantityKind::Length, true},
        {"diameter", "直径", QuantityKind::Length, false}};
}

[[nodiscard]] std::vector<CursorField> MakeArcFields()
{
    return {{"radius", "半径", QuantityKind::Length, true},
        {"sweep", "中心角", QuantityKind::Angle, false},
        {"arc_length", "円弧長", QuantityKind::Length, false}};
}

[[nodiscard]] std::vector<CursorField> MakeExtrudeFields()
{
    return {{"distance", "距離", QuantityKind::Length, true}};
}

[[nodiscard]] const std::vector<CursorField>& EmptyFields()
{
    static const std::vector<CursorField> empty;
    return empty;
}

} // namespace

const std::vector<CursorField>& CursorFieldsFor(DrawingTool tool, bool onWorkPlane)
{
    static const std::vector<CursorField> planarLine = MakePlanarLineFields();
    static const std::vector<CursorField> spatialLine = MakeSpatialLineFields();
    static const std::vector<CursorField> circle = MakeCircleFields();
    static const std::vector<CursorField> arc = MakeArcFields();
    static const std::vector<CursorField> extrude = MakeExtrudeFields();
    switch (tool) {
    case DrawingTool::Line:
    case DrawingTool::Polyline:
        return onWorkPlane ? planarLine : spatialLine;
    case DrawingTool::Circle:
        return circle;
    case DrawingTool::Arc:
        return arc;
    case DrawingTool::Move:
        return extrude;
    default:
        break;
    }
    return EmptyFields();
}

bool ToolUsesCursorInput(DrawingTool tool)
{
    return !CursorFieldsFor(tool, true).empty();
}

Result<CursorInputPanel> BeginCursorInput(DrawingTool tool, bool onWorkPlane)
{
    const std::vector<CursorField>& fields = CursorFieldsFor(tool, onWorkPlane);
    if (fields.empty()) {
        return Result<CursorInputPanel>::Failure(MakeError("UI-C001",
            "この道具では数値入力を使いません。",
            "点を置いて形を決める道具ではありません。"));
    }
    CursorInputPanel panel;
    panel.tool = tool;
    panel.onWorkPlane = onWorkPlane;
    panel.active = true;
    panel.fields = fields;
    panel.states.assign(fields.size(), CursorFieldState{});
    // 主要寸法欄へ自動で焦点を合わせる。無ければ先頭。
    panel.focusedIndex = 0;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (fields[index].primary) {
            panel.focusedIndex = index;
            break;
        }
    }
    return Result<CursorInputPanel>::Success(std::move(panel));
}

Result<CursorInputPanel> FocusNextField(const CursorInputPanel& panel, bool backward)
{
    if (!panel.active || panel.fields.empty()) {
        return Result<CursorInputPanel>::Failure(MakeError("UI-C002",
            "数値入力が出ていません。", "先に点を置いてください。"));
    }
    CursorInputPanel next = panel;
    const std::size_t count = panel.fields.size();
    next.focusedIndex = backward ? (panel.focusedIndex + count - 1) % count
                                 : (panel.focusedIndex + 1) % count;
    return Result<CursorInputPanel>::Success(std::move(next));
}

Result<CursorInputPanel> FocusField(const CursorInputPanel& panel, std::string_view fieldId)
{
    if (!panel.active) {
        return Result<CursorInputPanel>::Failure(MakeError("UI-C002",
            "数値入力が出ていません。", "先に点を置いてください。"));
    }
    for (std::size_t index = 0; index < panel.fields.size(); ++index) {
        if (panel.fields[index].id == fieldId) {
            CursorInputPanel next = panel;
            next.focusedIndex = index;
            return Result<CursorInputPanel>::Success(std::move(next));
        }
    }
    return Result<CursorInputPanel>::Failure(MakeError("UI-C003", "その欄がありません。",
        std::string(fieldId) + " という欄はこの道具にありません。"));
}

Result<CursorInputPanel> SetFieldText(const CursorInputPanel& panel, std::size_t index,
    std::string_view text)
{
    if (!panel.active || index >= panel.fields.size()) {
        return Result<CursorInputPanel>::Failure(MakeError("UI-C003", "その欄がありません。",
            "欄の番号が範囲の外です。"));
    }
    CursorInputPanel next = panel;
    // 全角で打っても通す。見た目を直すだけで、意味は変えない。
    next.states[index].text = geometry::NormalizeFullWidth(text);
    next.states[index].error = false;
    next.states[index].messageJa.clear();
    next.focusedIndex = index;
    // 打っている途中でも、読めた分だけ評価して見せる(§7.1「数式と評価値を同時表示」)。
    // 途中の「30d」のような読めない形では、値を出さずに式だけを見せる。
    // ここでは赤くしない。打ち終わる前に赤くすると、打つたびに画面が騒がしくなる。
    if (next.states[index].text.empty()) {
        next.states[index].hasValue = false;
    } else {
        const auto evaluated = geometry::EvaluateExpression(next.states[index].text,
            panel.fields[index].kind);
        if (evaluated.HasValue()) {
            next.states[index].value = evaluated.Value().value;
            next.states[index].hasValue = true;
        } else {
            next.states[index].hasValue = false;
        }
    }
    return Result<CursorInputPanel>::Success(std::move(next));
}

CursorInputPanel CancelCursorInput(const CursorInputPanel& panel)
{
    CursorInputPanel next = panel;
    next.active = false;
    next.states.assign(panel.fields.size(), CursorFieldState{});
    next.focusedIndex = 0;
    for (std::size_t index = 0; index < next.fields.size(); ++index) {
        if (next.fields[index].primary) {
            next.focusedIndex = index;
            break;
        }
    }
    return next;
}

std::string FieldDisplayJa(const CursorField& field, const CursorFieldState& state)
{
    if (!state.hasValue) {
        return state.text;
    }
    const bool isAngle = field.kind == QuantityKind::Angle;
    const double shown = isAngle ? state.value * 180.0 / kPi : state.value;
    const char* unit = isAngle ? " deg" : (field.kind == QuantityKind::Length ? " mm" : "");
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%g", shown);
    if (state.text.empty()) {
        return std::string(buffer) + unit;
    }
    return state.text + " = " + buffer + unit;
}

CursorPanelPlacement PlaceCursorPanel(double cursorXPx, double cursorYPx,
    double panelWidthPx, double panelHeightPx, double viewWidthPx, double viewHeightPx)
{
    CursorPanelPlacement placement;
    placement.xPx = cursorXPx + kCursorPanelOffsetPx;
    placement.yPx = cursorYPx + kCursorPanelOffsetPx;
    // 右下に出すと画面からはみ出すなら、左または上へ寄せる。
    if (placement.xPx + panelWidthPx > viewWidthPx) {
        placement.xPx = cursorXPx - kCursorPanelOffsetPx - panelWidthPx;
        placement.flippedHorizontally = true;
    }
    if (placement.yPx + panelHeightPx > viewHeightPx) {
        placement.yPx = cursorYPx - kCursorPanelOffsetPx - panelHeightPx;
        placement.flippedVertically = true;
    }
    // それでも入らない小さな画面では、画面の中へ押し込む。
    placement.xPx = std::clamp(placement.xPx, 0.0, std::max(0.0, viewWidthPx - panelWidthPx));
    placement.yPx = std::clamp(placement.yPx, 0.0,
        std::max(0.0, viewHeightPx - panelHeightPx));
    return placement;
}

} // namespace kachakacha::v2::app
