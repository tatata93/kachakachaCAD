#include "kachakacha/app/DiagnosticReport.h"

#include <algorithm>

namespace kachakacha::v2::app {

std::string_view SnapKindNameJa(modeling::SnapKind kind) noexcept
{
    switch (kind) {
    case modeling::SnapKind::Intersection:       return "交点";
    case modeling::SnapKind::Endpoint:           return "端点";
    case modeling::SnapKind::Center:             return "中心";
    case modeling::SnapKind::DrawingPoint:       return "作図点";
    case modeling::SnapKind::Tangent:            return "接点";
    case modeling::SnapKind::Perpendicular:      return "垂足";
    case modeling::SnapKind::Midpoint:           return "中点";
    case modeling::SnapKind::Quadrant:           return "四半点";
    case modeling::SnapKind::Extension:          return "延長線上";
    case modeling::SnapKind::ProjectedOnPlane:   return "面へ投影";
    case modeling::SnapKind::ClosestOnCurve:     return "線上の最近点";
    case modeling::SnapKind::GridMajor:          return "主グリッド";
    case modeling::SnapKind::GridMinor:          return "副グリッド";
    case modeling::SnapKind::FreeOnPlane:        return "面上の自由点";
    case modeling::SnapKind::ScreenIntersection: return "画面上の交差";
    }
    return "不明";
}

namespace {

//! 空なら決まった置き換えを出す。空欄のままだと、
//! 「調べていない」のか「本当に無い」のかが読めない。
[[nodiscard]] std::string OrNone(const std::string& value)
{
    return value.empty() ? std::string("(なし)") : value;
}

void AddLine(std::string& text, const char* key, const std::string& value)
{
    text += key;
    text += ": ";
    text += value;
    text += "\n";
}

} // namespace

std::string FileNameOnly(const std::string& path)
{
    std::size_t start = 0;
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (path[index] == '/' || path[index] == '\\') {
            start = index + 1;
        }
    }
    return path.substr(start);
}

std::vector<std::string> DiagnosticMismatches(const DiagnosticSnapshot& snapshot)
{
    std::vector<std::string> found;
    const std::string& tool = snapshot.activeTool;
    if (tool.empty()) {
        return found;
    }
    // 空の欄は「見ていない」ということなので、ずれとは数えない。
    // 数えると、まだ繋いでいない欄のせいで毎回ずれが出る。
    const auto check = [&](const char* name, const std::string& value) {
        if (!value.empty() && value != tool) {
            found.push_back(std::string(name) + " が " + value + "(道具は " + tool + ")");
        }
    };
    check("rightPanelTool", snapshot.rightPanelTool);
    check("cursorMode", snapshot.cursorMode);
    check("previewOwner", snapshot.previewOwner);
    check("snapOwner", snapshot.snapOwner);
    return found;
}

std::string FormatDiagnosticReport(const DiagnosticSnapshot& snapshot)
{
    std::string text = "kachakachaCAD Diagnostic\n";
    AddLine(text, "timestamp", OrNone(snapshot.timestamp));
    AddLine(text, "version", OrNone(snapshot.version));
    AddLine(text, "commit", OrNone(snapshot.commit));
    AddLine(text, "branch", OrNone(snapshot.branch));
    if (snapshot.dirtyKnown) {
        AddLine(text, "dirty", snapshot.dirty ? "true" : "false");
    } else {
        AddLine(text, "dirty", "(不明)");
    }
    text += "\n";
    // 道の並びは、ここで必ず落とす。呼ぶ側の作法に任せない。
    AddLine(text, "document",
        snapshot.documentName.empty() ? std::string("(未保存)")
                                      : FileNameOnly(snapshot.documentName));
    AddLine(text, "activePart", OrNone(snapshot.activePart));
    AddLine(text, "activeWorkPlane", OrNone(snapshot.activeWorkPlane));
    text += "\n";
    AddLine(text, "activeTool", OrNone(snapshot.activeTool));
    AddLine(text, "rightPanelMode", OrNone(snapshot.rightPanelMode));
    AddLine(text, "rightPanelTool", OrNone(snapshot.rightPanelTool));
    AddLine(text, "cursorMode", OrNone(snapshot.cursorMode));
    AddLine(text, "previewOwner", OrNone(snapshot.previewOwner));
    AddLine(text, "snapOwner", OrNone(snapshot.snapOwner));
    AddLine(text, "snapType", OrNone(snapshot.snapType));
    AddLine(text, "snapTarget", OrNone(snapshot.snapTarget));
    text += "\n";
    AddLine(text, "selectionCount", std::to_string(snapshot.selectionCount));
    if (!snapshot.selection.empty()) {
        text += "selection:\n";
        const std::size_t shown =
            std::min(snapshot.selection.size(), kDiagnosticSelectionLimit);
        for (std::size_t index = 0; index < shown; ++index) {
            text += "- " + snapshot.selection[index] + "\n";
        }
        if (snapshot.selection.size() > shown) {
            text += "- (ほか " + std::to_string(snapshot.selection.size() - shown)
                + " 件)\n";
        }
    }
    // ずれているなら、貼り付けた人がすぐ気づけるように最後へ書く。
    const auto mismatches = DiagnosticMismatches(snapshot);
    if (!mismatches.empty()) {
        text += "\nmismatch:\n";
        for (const std::string& line : mismatches) {
            text += "- " + line + "\n";
        }
    }
    return text;
}

} // namespace kachakacha::v2::app
