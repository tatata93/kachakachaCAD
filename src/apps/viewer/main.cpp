#include "kachakacha/io/ProjectScript.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "kachakacha/model/Sketch.h"
#include "kachakacha/model/Project.h"
#include "kachakacha/model/Wire.h"
#include "kachakacha/model/WorkPlane.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using kachakacha::geometry::Vector3;
using kachakacha::geometry::AlmostEqual;
using kachakacha::io::LoadProjectScript;
using kachakacha::model::NamedWire;
using kachakacha::model::NamedWorkPlane;
using kachakacha::model::Project;
using kachakacha::model::Sketch;
using kachakacha::model::Wire;
using kachakacha::model::WirePlanePolicy;
using kachakacha::model::WorkPlane;

namespace {

constexpr int InfoPanelWidth = 360;
constexpr std::size_t MaxUndoSteps = 64;

struct Camera {
    double yaw = -0.72;
    double pitch = 0.58;
    double zoom = 36.0;
    Vector3 target = {5.0, 3.0, 2.5};
};

struct ScreenPoint {
    int x = 0;
    int y = 0;
    double depth = 0.0;
};

enum class SelectionKind {
    None,
    WorkPlane,
    Wire,
};

struct Selection {
    SelectionKind kind = SelectionKind::None;
    std::size_t index = 0;
};

struct PanelRow {
    std::wstring text;
    Selection selection;
    bool selectable = false;
};

struct DrawableWire {
    std::wstring name;
    Wire wire;
    WirePlanePolicy planePolicy = WirePlanePolicy::Free3D;
    std::optional<std::wstring> sourcePlaneName;
    COLORREF color = RGB(0, 0, 0);
    int width = 2;
};

struct DrawablePlane {
    std::wstring name;
    WorkPlane plane;
    double halfSize = 6.0;
    COLORREF color = RGB(190, 205, 220);
    COLORREF gridColor = RGB(226, 233, 239);
};

struct Scene {
    std::wstring title;
    std::vector<DrawablePlane> planes;
    std::vector<DrawableWire> wires;
    std::vector<Vector3> points;
};

struct EditorSnapshot {
    int sceneIndex = 0;
    std::vector<Scene> scenes;
    Selection selection;
};

struct AppState {
    Camera camera;
    int sceneIndex = 0;
    std::vector<Scene> scenes;
    Selection selection;
    std::vector<EditorSnapshot> undoStack;
    std::vector<EditorSnapshot> redoStack;
    std::wstring statusLine = L"Ready";
    bool dragging = false;
    bool movedWhileDragging = false;
    POINT mouseDown = {};
    POINT lastMouse = {};
};

ScreenPoint ProjectPoint(const Vector3& point, const Camera& camera, int width, int height)
{
    const Vector3 translated = point - camera.target;

    const double cy = std::cos(camera.yaw);
    const double sy = std::sin(camera.yaw);
    const double cp = std::cos(camera.pitch);
    const double sp = std::sin(camera.pitch);

    const double x1 = cy * translated.x - sy * translated.y;
    const double y1 = sy * translated.x + cy * translated.y;
    const double z1 = translated.z;

    const double y2 = cp * y1 - sp * z1;
    const double z2 = sp * y1 + cp * z1;

    return {
        static_cast<int>(std::lround(static_cast<double>(width) * 0.5 + x1 * camera.zoom)),
        static_cast<int>(std::lround(static_cast<double>(height) * 0.53 - y2 * camera.zoom)),
        z2,
    };
}

void WithPen(HDC hdc, COLORREF color, int width, const auto& draw)
{
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    draw();
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DrawLine3D(HDC hdc, const Camera& camera, int width, int height, const Vector3& a, const Vector3& b)
{
    const ScreenPoint pa = ProjectPoint(a, camera, width, height);
    const ScreenPoint pb = ProjectPoint(b, camera, width, height);

    MoveToEx(hdc, pa.x, pa.y, nullptr);
    LineTo(hdc, pb.x, pb.y);
}

void DrawTextLine(HDC hdc, int x, int y, std::wstring_view text)
{
    TextOutW(hdc, x, y, text.data(), static_cast<int>(text.size()));
}

std::wstring ToWide(std::string_view text)
{
    std::wstring output;
    output.reserve(text.size());
    for (char character : text) {
        output.push_back(static_cast<unsigned char>(character));
    }

    return output;
}

std::optional<std::wstring> ToWideOptional(const std::optional<std::string>& text)
{
    if (!text.has_value()) {
        return std::nullopt;
    }

    return ToWide(*text);
}

std::wstring Ellipsize(std::wstring_view text, std::size_t maxCharacters)
{
    if (text.size() <= maxCharacters) {
        return std::wstring(text);
    }

    if (maxCharacters <= 3) {
        return std::wstring(text.substr(0, maxCharacters));
    }

    std::wstring output(text.substr(0, maxCharacters - 3));
    output += L"...";
    return output;
}

void DrawPanelLine(HDC hdc, int x, int y, std::wstring_view text, std::size_t maxCharacters = 43)
{
    const std::wstring clipped = Ellipsize(text, maxCharacters);
    DrawTextLine(hdc, x, y, clipped);
}

std::wstring FormatDouble(double value)
{
    std::wostringstream output;
    output << std::fixed << std::setprecision(2) << value;
    return output.str();
}

std::wstring FormatVector(const Vector3& value)
{
    return L"X " + FormatDouble(value.x)
        + L"  Y " + FormatDouble(value.y)
        + L"  Z " + FormatDouble(value.z);
}

std::wstring WireKindName(kachakacha::model::WireKind kind)
{
    switch (kind) {
    case kachakacha::model::WireKind::Line:
        return L"Line";
    case kachakacha::model::WireKind::Polyline:
        return L"Polyline";
    case kachakacha::model::WireKind::CubicBezier:
        return L"Cubic Bezier";
    case kachakacha::model::WireKind::Circle:
        return L"Circle";
    case kachakacha::model::WireKind::CircularArc:
        return L"Circular Arc";
    }

    return L"Wire";
}

std::wstring PlanePolicyName(WirePlanePolicy policy)
{
    switch (policy) {
    case WirePlanePolicy::Free3D:
        return L"free";
    case WirePlanePolicy::ReferenceOnly:
        return L"reference";
    case WirePlanePolicy::LockedToPlane:
        return L"locked";
    }

    return L"unknown";
}

double ApproximateWireLength(const Wire& wire)
{
    constexpr int steps = 96;

    double length = 0.0;
    Vector3 previous = wire.Evaluate(0.0);
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const Vector3 current = wire.Evaluate(t);
        length += (current - previous).Length();
        previous = current;
    }

    return length;
}

bool IsSelectionValid(const Scene& scene, Selection selection)
{
    if (selection.kind == SelectionKind::WorkPlane) {
        return selection.index < scene.planes.size();
    }
    if (selection.kind == SelectionKind::Wire) {
        return selection.index < scene.wires.size();
    }

    return false;
}

void DrawWire(HDC hdc, const Camera& camera, int width, int height, const DrawableWire& drawable, bool selected)
{
    const auto drawSegments = [&](COLORREF color, int penWidth) {
        WithPen(hdc, color, penWidth, [&]() {
            constexpr int steps = 72;
            ScreenPoint previous = ProjectPoint(drawable.wire.Evaluate(0.0), camera, width, height);

            for (int i = 1; i <= steps; ++i) {
                const double t = static_cast<double>(i) / static_cast<double>(steps);
                const ScreenPoint current = ProjectPoint(drawable.wire.Evaluate(t), camera, width, height);
                MoveToEx(hdc, previous.x, previous.y, nullptr);
                LineTo(hdc, current.x, current.y);
                previous = current;
            }
        });
    };

    if (selected) {
        drawSegments(RGB(255, 210, 72), drawable.width + 5);
        drawSegments(RGB(190, 38, 63), drawable.width + 2);
        return;
    }

    drawSegments(drawable.color, drawable.width);
}

void DrawPoint(HDC hdc, const Camera& camera, int width, int height, const Vector3& point, COLORREF color)
{
    const ScreenPoint projected = ProjectPoint(point, camera, width, height);
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    Ellipse(hdc, projected.x - 4, projected.y - 4, projected.x + 5, projected.y + 5);

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    SelectObject(hdc, oldBrush);
    DeleteObject(brush);
}

void DrawSelectedPoint(HDC hdc, const Camera& camera, int width, int height, const Vector3& point)
{
    const ScreenPoint projected = ProjectPoint(point, camera, width, height);
    HBRUSH brush = CreateSolidBrush(RGB(255, 210, 72));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(190, 38, 63));
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    Ellipse(hdc, projected.x - 6, projected.y - 6, projected.x + 7, projected.y + 7);

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    SelectObject(hdc, oldBrush);
    DeleteObject(brush);
}

void DrawWireControlPoints(HDC hdc, const Camera& camera, int width, int height, const DrawableWire& drawable)
{
    for (const Vector3& controlPoint : drawable.wire.ControlPoints()) {
        DrawSelectedPoint(hdc, camera, width, height, controlPoint);
    }
}

void DrawPlane(HDC hdc, const Camera& camera, int width, int height, const DrawablePlane& drawable, bool selected)
{
    const WorkPlane& plane = drawable.plane;
    const double s = drawable.halfSize;

    const std::vector<Vector3> corners = {
        plane.ToWorld(-s, -s),
        plane.ToWorld(s, -s),
        plane.ToWorld(s, s),
        plane.ToWorld(-s, s),
    };

    POINT polygon[4] = {};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const ScreenPoint projected = ProjectPoint(corners[i], camera, width, height);
        polygon[i] = {projected.x, projected.y};
    }

    HBRUSH brush = CreateSolidBrush(selected ? RGB(255, 249, 226) : RGB(248, 250, 252));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HPEN outlinePen = CreatePen(PS_SOLID, selected ? 3 : 1, selected ? RGB(216, 139, 27) : drawable.color);
    HGDIOBJ oldPen = SelectObject(hdc, outlinePen);
    Polygon(hdc, polygon, 4);
    SelectObject(hdc, oldPen);
    DeleteObject(outlinePen);
    SelectObject(hdc, oldBrush);
    DeleteObject(brush);

    WithPen(hdc, drawable.gridColor, 1, [&]() {
        for (int i = -static_cast<int>(s); i <= static_cast<int>(s); ++i) {
            const double d = static_cast<double>(i);
            DrawLine3D(hdc, camera, width, height, plane.ToWorld(-s, d), plane.ToWorld(s, d));
            DrawLine3D(hdc, camera, width, height, plane.ToWorld(d, -s), plane.ToWorld(d, s));
        }
    });

    WithPen(hdc, selected ? RGB(216, 139, 27) : RGB(80, 112, 150), selected ? 3 : 2, [&]() {
        DrawLine3D(hdc, camera, width, height, plane.Origin(), plane.ToWorld(2.0, 0.0));
        DrawLine3D(hdc, camera, width, height, plane.Origin(), plane.ToWorld(0.0, 2.0));
    });
}

bool SameSelection(Selection lhs, Selection rhs)
{
    return lhs.kind == rhs.kind && lhs.index == rhs.index;
}

std::vector<PanelRow> BuildPanelRows(const Scene& scene, Selection selection)
{
    std::vector<PanelRow> rows;

    const auto add = [&](std::wstring text) {
        rows.push_back({std::move(text), {}, false});
    };
    const auto addSelectable = [&](std::wstring text, Selection rowSelection) {
        rows.push_back({std::move(text), rowSelection, true});
    };

    add(L"Selected");
    if (IsSelectionValid(scene, selection)) {
        if (selection.kind == SelectionKind::WorkPlane) {
            const DrawablePlane& plane = scene.planes[selection.index];
            add(L"  work plane  " + plane.name);
            add(L"  move  A/D X  W/S Y  Q/E Z");
            add(L"  C copy  Delete remove");
            add(L"  O " + FormatVector(plane.plane.Origin()));
            add(L"  U " + FormatVector(plane.plane.UAxis()));
            add(L"  V " + FormatVector(plane.plane.VAxis()));
            add(L"  N " + FormatVector(plane.plane.Normal()));
        } else if (selection.kind == SelectionKind::Wire) {
            const DrawableWire& wire = scene.wires[selection.index];
            add(L"  wire  " + wire.name);
            add(L"  move  A/D X  W/S Y  Q/E Z");
            add(L"  C copy  Delete remove");
            add(L"  kind  " + WireKindName(wire.wire.Kind()));
            add(L"  policy  " + PlanePolicyName(wire.planePolicy));
            if (wire.sourcePlaneName.has_value()) {
                add(L"  plane  " + *wire.sourcePlaneName);
            }
            add(L"  start  " + FormatVector(wire.wire.Start()));
            add(L"  end    " + FormatVector(wire.wire.End()));
            add(L"  length about  " + FormatDouble(ApproximateWireLength(wire.wire)));
            add(L"  points  " + std::to_wstring(wire.wire.ControlPoints().size()));
        }
    } else {
        add(L"  click a wire, plane, or list row");
    }

    add(L"");
    add(L"Work planes");
    for (std::size_t i = 0; i < scene.planes.size(); ++i) {
        const DrawablePlane& plane = scene.planes[i];
        const Selection rowSelection{SelectionKind::WorkPlane, i};
        const std::wstring prefix = SameSelection(selection, rowSelection) ? L"> " : L"  ";
        addSelectable(prefix + plane.name, rowSelection);
        add(L"    O " + FormatVector(plane.plane.Origin()));
    }

    add(L"");
    add(L"Wires");
    for (std::size_t i = 0; i < scene.wires.size(); ++i) {
        const DrawableWire& wire = scene.wires[i];
        const Selection rowSelection{SelectionKind::Wire, i};
        const std::wstring prefix = SameSelection(selection, rowSelection) ? L"> " : L"  ";
        std::wstring label = prefix + wire.name
            + L"  " + WireKindName(wire.wire.Kind())
            + L"  " + PlanePolicyName(wire.planePolicy);
        if (wire.sourcePlaneName.has_value()) {
            label += L"  @" + *wire.sourcePlaneName;
        }
        addSelectable(label, rowSelection);
    }

    return rows;
}

std::vector<PanelRow> BuildInfoPanelRows(const Scene& scene, const AppState& state, Selection selection)
{
    std::vector<PanelRow> rows;
    rows.push_back({L"Edit", {}, false});
    rows.push_back({L"  " + state.statusLine, {}, false});
    rows.push_back({L"  undo " + std::wstring(state.undoStack.empty() ? L"no" : L"yes")
            + L"  redo " + std::wstring(state.redoStack.empty() ? L"no" : L"yes"),
        {},
        false});
    rows.push_back({L"", {}, false});

    const std::vector<PanelRow> inspectorRows = BuildPanelRows(scene, selection);
    rows.insert(rows.end(), inspectorRows.begin(), inspectorRows.end());
    return rows;
}

void DrawInfoPanel(HDC hdc, int width, int height, const Scene& scene, const AppState& state, Selection selection)
{
    const int panelLeft = std::max(0, width - InfoPanelWidth);
    RECT panelRect = {panelLeft, 0, width, height};
    HBRUSH panelBrush = CreateSolidBrush(RGB(247, 249, 251));
    FillRect(hdc, &panelRect, panelBrush);
    DeleteObject(panelBrush);

    WithPen(hdc, RGB(210, 218, 226), 1, [&]() {
        MoveToEx(hdc, panelLeft, 0, nullptr);
        LineTo(hdc, panelLeft, height);
    });

    HFONT titleFont = CreateFontW(
        17,
        0,
        0,
        0,
        FW_SEMIBOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        VARIABLE_PITCH,
        L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, titleFont);
    SetTextColor(hdc, RGB(36, 45, 55));
    DrawTextLine(hdc, panelLeft + 16, 18, L"Inspector");
    SelectObject(hdc, oldFont);
    DeleteObject(titleFont);

    HFONT bodyFont = CreateFontW(
        14,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        VARIABLE_PITCH,
        L"Segoe UI");
    oldFont = SelectObject(hdc, bodyFont);
    SetTextColor(hdc, RGB(62, 72, 84));

    const std::vector<PanelRow> rows = BuildInfoPanelRows(scene, state, selection);

    int y = 48;
    const int lineHeight = 19;
    const int maxY = height - 24;
    for (const PanelRow& row : rows) {
        if (y > maxY) {
            DrawPanelLine(hdc, panelLeft + 16, y, L"...");
            break;
        }

        const bool selectedRow = row.selectable && SameSelection(row.selection, selection);
        SetTextColor(hdc, selectedRow ? RGB(190, 38, 63) : RGB(62, 72, 84));
        DrawPanelLine(hdc, panelLeft + 16, y, row.text);
        y += lineHeight;
    }

    SelectObject(hdc, oldFont);
    DeleteObject(bodyFont);
}

void RefreshScenePoints(Scene& scene)
{
    scene.points.clear();
    for (const DrawablePlane& plane : scene.planes) {
        scene.points.push_back(plane.plane.Origin());
    }

    for (const DrawableWire& wire : scene.wires) {
        for (const Vector3& controlPoint : wire.wire.ControlPoints()) {
            scene.points.push_back(controlPoint);
        }
    }
}

void RefreshAllScenePoints(std::vector<Scene>& scenes)
{
    for (Scene& scene : scenes) {
        RefreshScenePoints(scene);
    }
}

std::vector<Scene> BuildScenes()
{
    const WorkPlane xyPlane = WorkPlane::FromThreePoints(
        {0.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0});

    const WorkPlane verticalPlane = WorkPlane::FromPointNormal(
        {10.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0});

    const WorkPlane tiltedPlane = WorkPlane::FromThreePoints(
        {0.0, 0.0, 1.0},
        {8.0, 1.0, 2.2},
        {1.0, 6.0, 5.0});

    const Sketch verticalSketch(verticalPlane);
    const Sketch tiltedSketch(tiltedPlane);

    std::vector<Scene> scenes;

    scenes.push_back({
        L"3D wire: origin to X2 Y7 Z4",
        {{L"floor", xyPlane, 8.0, RGB(180, 195, 210), RGB(232, 237, 242)}},
        {{
            L"origin_to_point",
            Wire::Line({0.0, 0.0, 0.0}, {2.0, 7.0, 4.0}),
            WirePlanePolicy::Free3D,
            std::nullopt,
            RGB(34, 108, 214),
            3,
        }},
        {{0.0, 0.0, 0.0}, {2.0, 7.0, 4.0}},
    });

    scenes.push_back({
        L"Sketch on X=10 work plane",
        {
            {L"floor", xyPlane, 8.0, RGB(198, 207, 218), RGB(236, 240, 244)},
            {L"front", verticalPlane, 7.0, RGB(160, 190, 218), RGB(222, 235, 246)},
        },
        {
            {
                L"front_window_bottom",
                verticalSketch.MakeLine({2.0, 1.0}, {5.0, 6.0}),
                WirePlanePolicy::ReferenceOnly,
                std::optional<std::wstring>{L"front"},
                RGB(25, 128, 91),
                3,
            },
            {
                L"front_nose_curve",
                verticalSketch.MakeCubicBezier({0.0, 0.0}, {1.0, 4.0}, {5.0, 4.0}, {6.0, 0.0}),
                WirePlanePolicy::ReferenceOnly,
                std::optional<std::wstring>{L"front"},
                RGB(203, 86, 48),
                3,
            },
        },
        {{10.0, 0.0, 0.0}},
    });

    scenes.push_back({
        L"Tilted plane from three selected points",
        {
            {L"floor", xyPlane, 8.0, RGB(205, 210, 217), RGB(238, 240, 243)},
            {L"free_paper", tiltedPlane, 7.0, RGB(183, 162, 214), RGB(233, 225, 244)},
        },
        {
            {
                L"free_plane_line",
                tiltedSketch.MakeLine({-4.0, -2.0}, {5.0, 3.0}),
                WirePlanePolicy::ReferenceOnly,
                std::optional<std::wstring>{L"free_paper"},
                RGB(43, 103, 169),
                3,
            },
            {
                L"free_plane_curve",
                tiltedSketch.MakeCubicBezier({-4.0, 3.0}, {-1.0, 6.0}, {3.0, -2.0}, {5.0, 1.0}),
                WirePlanePolicy::ReferenceOnly,
                std::optional<std::wstring>{L"free_paper"},
                RGB(181, 77, 119),
                3,
            },
        },
        {{0.0, 0.0, 1.0}, {8.0, 1.0, 2.2}, {1.0, 6.0, 5.0}},
    });

    return scenes;
}

COLORREF PickWireColor(std::size_t index)
{
    constexpr COLORREF palette[] = {
        RGB(34, 108, 214),
        RGB(203, 86, 48),
        RGB(25, 128, 91),
        RGB(181, 77, 119),
        RGB(112, 76, 164),
        RGB(160, 112, 32),
    };

    return palette[index % std::size(palette)];
}

Scene BuildSceneFromProject(const Project& project, const std::filesystem::path& projectPath)
{
    Scene scene;
    scene.title = L"KCD project: " + projectPath.filename().wstring();

    for (const NamedWorkPlane& workPlane : project.WorkPlanes()) {
        scene.planes.push_back({
            ToWide(workPlane.name),
            workPlane.plane,
            7.0,
            RGB(160, 190, 218),
            RGB(224, 234, 244),
        });
    }

    std::size_t wireIndex = 0;
    for (const NamedWire& wire : project.Wires()) {
        scene.wires.push_back({
            ToWide(wire.name),
            wire.wire,
            wire.metadata.planePolicy,
            ToWideOptional(wire.metadata.sourcePlaneName),
            PickWireColor(wireIndex),
            wire.metadata.planePolicy == WirePlanePolicy::LockedToPlane ? 4 : 3,
        });
        ++wireIndex;
    }

    RefreshScenePoints(scene);
    return scene;
}

std::vector<Scene> LoadScenesFromProjectFile(const std::filesystem::path& projectPath)
{
    std::ifstream input(projectPath);
    if (!input) {
        throw std::runtime_error("Could not open project file.");
    }

    const Project project = LoadProjectScript(input, projectPath.string());
    return {BuildSceneFromProject(project, projectPath)};
}

void DrawAxes(HDC hdc, const Camera& camera, int width, int height)
{
    WithPen(hdc, RGB(205, 55, 55), 2, [&]() {
        DrawLine3D(hdc, camera, width, height, {0.0, 0.0, 0.0}, {8.0, 0.0, 0.0});
    });
    WithPen(hdc, RGB(55, 135, 80), 2, [&]() {
        DrawLine3D(hdc, camera, width, height, {0.0, 0.0, 0.0}, {0.0, 8.0, 0.0});
    });
    WithPen(hdc, RGB(65, 85, 205), 2, [&]() {
        DrawLine3D(hdc, camera, width, height, {0.0, 0.0, 0.0}, {0.0, 0.0, 8.0});
    });
}

void DrawScene(HDC hdc, int width, int height, const AppState& state)
{
    if (state.scenes.empty()) {
        return;
    }

    const Scene& scene = state.scenes[
        static_cast<std::size_t>(std::clamp(state.sceneIndex, 0, static_cast<int>(state.scenes.size() - 1)))];
    const Selection selection = IsSelectionValid(scene, state.selection) ? state.selection : Selection{};
    const int viewportWidth = std::max(320, width - InfoPanelWidth);

    RECT background = {0, 0, width, height};
    HBRUSH backgroundBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &background, backgroundBrush);
    DeleteObject(backgroundBrush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(38, 45, 53));

    DrawAxes(hdc, state.camera, viewportWidth, height);

    for (std::size_t i = 0; i < scene.planes.size(); ++i) {
        const bool selected = selection.kind == SelectionKind::WorkPlane && selection.index == i;
        DrawPlane(hdc, state.camera, viewportWidth, height, scene.planes[i], selected);
    }

    for (std::size_t i = 0; i < scene.wires.size(); ++i) {
        if (selection.kind == SelectionKind::Wire && selection.index == i) {
            continue;
        }
        DrawWire(hdc, state.camera, viewportWidth, height, scene.wires[i], false);
    }

    for (const Vector3& point : scene.points) {
        DrawPoint(hdc, state.camera, viewportWidth, height, point, RGB(25, 25, 25));
    }

    if (selection.kind == SelectionKind::Wire) {
        const DrawableWire& wire = scene.wires[selection.index];
        DrawWire(hdc, state.camera, viewportWidth, height, wire, true);
        DrawWireControlPoints(hdc, state.camera, viewportWidth, height, wire);
    }

    HFONT font = CreateFontW(
        18,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        VARIABLE_PITCH,
        L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);

    DrawTextLine(hdc, 18, 16, L"kachakachaCAD viewer");
    DrawTextLine(hdc, 18, 40, scene.title);
    DrawTextLine(hdc, 18, 66, L"click select  |  drag rotate  |  wheel zoom  |  Tab next  |  Esc clear  |  R reset");
    DrawTextLine(hdc, 18, 90, L"move selected: A/D X  W/S Y  Q/E Z  |  Shift 5mm  |  Ctrl 0.1mm");
    DrawTextLine(hdc, 18, 114, L"edit selected: C copy  |  Delete remove  |  Ctrl+Z undo  |  Ctrl+Y redo");

    SelectObject(hdc, oldFont);
    DeleteObject(font);

    DrawInfoPanel(hdc, width, height, scene, state, selection);
}

const Scene* CurrentScene(const AppState& state)
{
    if (state.scenes.empty()) {
        return nullptr;
    }

    const auto index = static_cast<std::size_t>(
        std::clamp(state.sceneIndex, 0, static_cast<int>(state.scenes.size() - 1)));
    return &state.scenes[index];
}

Scene* CurrentScene(AppState& state)
{
    if (state.scenes.empty()) {
        return nullptr;
    }

    const auto index = static_cast<std::size_t>(
        std::clamp(state.sceneIndex, 0, static_cast<int>(state.scenes.size() - 1)));
    return &state.scenes[index];
}

EditorSnapshot CaptureSnapshot(const AppState& state)
{
    return {state.sceneIndex, state.scenes, state.selection};
}

void RestoreSnapshot(AppState& state, EditorSnapshot snapshot)
{
    state.sceneIndex = snapshot.sceneIndex;
    state.scenes = std::move(snapshot.scenes);
    state.selection = snapshot.selection;
    RefreshAllScenePoints(state.scenes);

    const Scene* scene = CurrentScene(state);
    if (scene == nullptr || !IsSelectionValid(*scene, state.selection)) {
        state.selection = {};
    }
}

void PushUndo(AppState& state)
{
    if (state.undoStack.size() >= MaxUndoSteps) {
        state.undoStack.erase(state.undoStack.begin());
    }

    state.undoStack.push_back(CaptureSnapshot(state));
    state.redoStack.clear();
}

bool UndoEdit(AppState& state)
{
    if (state.undoStack.empty()) {
        state.statusLine = L"Nothing to undo";
        return false;
    }

    state.redoStack.push_back(CaptureSnapshot(state));
    EditorSnapshot snapshot = std::move(state.undoStack.back());
    state.undoStack.pop_back();
    RestoreSnapshot(state, std::move(snapshot));
    state.statusLine = L"Undo";
    return true;
}

bool RedoEdit(AppState& state)
{
    if (state.redoStack.empty()) {
        state.statusLine = L"Nothing to redo";
        return false;
    }

    state.undoStack.push_back(CaptureSnapshot(state));
    EditorSnapshot snapshot = std::move(state.redoStack.back());
    state.redoStack.pop_back();
    RestoreSnapshot(state, std::move(snapshot));
    state.statusLine = L"Redo";
    return true;
}

double DistanceToSegment(POINT point, ScreenPoint a, ScreenPoint b)
{
    const double px = static_cast<double>(point.x);
    const double py = static_cast<double>(point.y);
    const double ax = static_cast<double>(a.x);
    const double ay = static_cast<double>(a.y);
    const double bx = static_cast<double>(b.x);
    const double by = static_cast<double>(b.y);

    const double dx = bx - ax;
    const double dy = by - ay;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1.0e-9) {
        const double sx = px - ax;
        const double sy = py - ay;
        return std::sqrt(sx * sx + sy * sy);
    }

    const double t = std::clamp(((px - ax) * dx + (py - ay) * dy) / lengthSquared, 0.0, 1.0);
    const double cx = ax + t * dx;
    const double cy = ay + t * dy;
    const double sx = px - cx;
    const double sy = py - cy;
    return std::sqrt(sx * sx + sy * sy);
}

bool IsInsidePolygon(POINT point, const std::vector<POINT>& polygon)
{
    if (polygon.size() < 3) {
        return false;
    }

    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const POINT a = polygon[i];
        const POINT b = polygon[j];
        const bool crosses = (a.y > point.y) != (b.y > point.y);
        if (!crosses) {
            continue;
        }

        const double x = static_cast<double>(b.x - a.x) * static_cast<double>(point.y - a.y)
                / static_cast<double>(b.y - a.y)
            + static_cast<double>(a.x);
        if (static_cast<double>(point.x) < x) {
            inside = !inside;
        }
    }

    return inside;
}

Selection HitTestScene(const Scene& scene, const Camera& camera, int viewportWidth, int height, POINT point)
{
    if (point.x < 0 || point.y < 0 || point.x >= viewportWidth || point.y >= height) {
        return {};
    }

    constexpr double wirePickDistance = 10.0;
    double bestDistance = wirePickDistance;
    Selection bestSelection;

    for (std::size_t i = 0; i < scene.wires.size(); ++i) {
        const DrawableWire& wire = scene.wires[i];
        constexpr int steps = 96;
        ScreenPoint previous = ProjectPoint(wire.wire.Evaluate(0.0), camera, viewportWidth, height);
        for (int step = 1; step <= steps; ++step) {
            const double t = static_cast<double>(step) / static_cast<double>(steps);
            const ScreenPoint current = ProjectPoint(wire.wire.Evaluate(t), camera, viewportWidth, height);
            const double distance = DistanceToSegment(point, previous, current);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestSelection = {SelectionKind::Wire, i};
            }
            previous = current;
        }
    }

    if (bestSelection.kind != SelectionKind::None) {
        return bestSelection;
    }

    double bestDepth = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < scene.planes.size(); ++i) {
        const DrawablePlane& plane = scene.planes[i];
        const double s = plane.halfSize;
        const std::vector<Vector3> corners = {
            plane.plane.ToWorld(-s, -s),
            plane.plane.ToWorld(s, -s),
            plane.plane.ToWorld(s, s),
            plane.plane.ToWorld(-s, s),
        };

        std::vector<POINT> polygon;
        polygon.reserve(corners.size());
        double depth = 0.0;
        for (const Vector3& corner : corners) {
            const ScreenPoint projected = ProjectPoint(corner, camera, viewportWidth, height);
            polygon.push_back({projected.x, projected.y});
            depth += projected.depth;
        }
        depth /= static_cast<double>(corners.size());

        if (IsInsidePolygon(point, polygon) && depth > bestDepth) {
            bestDepth = depth;
            bestSelection = {SelectionKind::WorkPlane, i};
        }
    }

    return bestSelection;
}

std::optional<Selection> HitTestInfoPanel(
    const Scene& scene,
    const AppState& state,
    int width,
    int height,
    POINT point)
{
    const int panelLeft = std::max(0, width - InfoPanelWidth);
    if (point.x < panelLeft || point.x >= width || point.y < 48 || point.y >= height) {
        return std::nullopt;
    }

    constexpr int lineHeight = 19;
    const int maxY = height - 24;
    if (point.y > maxY) {
        return std::nullopt;
    }

    const Selection validSelection = IsSelectionValid(scene, state.selection) ? state.selection : Selection{};
    const std::vector<PanelRow> rows = BuildInfoPanelRows(scene, state, validSelection);
    const std::size_t rowIndex = static_cast<std::size_t>((point.y - 48) / lineHeight);
    if (rowIndex >= rows.size() || !rows[rowIndex].selectable) {
        return std::nullopt;
    }

    return rows[rowIndex].selection;
}

void SelectNext(AppState& state)
{
    const Scene* scene = CurrentScene(state);
    if (scene == nullptr) {
        state.selection = {};
        return;
    }

    const std::size_t total = scene->wires.size() + scene->planes.size();
    if (total == 0) {
        state.selection = {};
        return;
    }

    std::size_t current = total - 1;
    if (state.selection.kind == SelectionKind::Wire && state.selection.index < scene->wires.size()) {
        current = state.selection.index;
    } else if (state.selection.kind == SelectionKind::WorkPlane && state.selection.index < scene->planes.size()) {
        current = scene->wires.size() + state.selection.index;
    }

    const std::size_t next = (current + 1) % total;
    if (next < scene->wires.size()) {
        state.selection = {SelectionKind::Wire, next};
    } else {
        state.selection = {SelectionKind::WorkPlane, next - scene->wires.size()};
    }
}

double CurrentNudgeStep()
{
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        return 5.0;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        return 0.1;
    }

    return 0.5;
}

bool NudgeSelection(AppState& state, Vector3 delta)
{
    Scene* scene = CurrentScene(state);
    if (scene == nullptr || !IsSelectionValid(*scene, state.selection)) {
        state.statusLine = L"Select an object before moving";
        return false;
    }

    PushUndo(state);
    if (state.selection.kind == SelectionKind::Wire) {
        DrawableWire& wire = scene->wires[state.selection.index];
        wire.wire = wire.wire.Translated(delta);
        RefreshScenePoints(*scene);
        state.statusLine = L"Moved wire " + wire.name;
        return true;
    }

    if (state.selection.kind == SelectionKind::WorkPlane) {
        DrawablePlane& plane = scene->planes[state.selection.index];
        plane.plane = plane.plane.Translated(delta);
        RefreshScenePoints(*scene);
        state.statusLine = L"Moved plane " + plane.name;
        return true;
    }

    return false;
}

bool NameExists(const Scene& scene, SelectionKind kind, const std::wstring& name)
{
    if (kind == SelectionKind::Wire) {
        return std::any_of(scene.wires.begin(), scene.wires.end(), [&](const DrawableWire& wire) {
            return wire.name == name;
        });
    }

    if (kind == SelectionKind::WorkPlane) {
        return std::any_of(scene.planes.begin(), scene.planes.end(), [&](const DrawablePlane& plane) {
            return plane.name == name;
        });
    }

    return false;
}

std::wstring MakeCopyName(const Scene& scene, SelectionKind kind, const std::wstring& originalName)
{
    const std::wstring base = originalName.empty() ? L"copy" : originalName + L"_copy";
    if (!NameExists(scene, kind, base)) {
        return base;
    }

    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::wstring candidate = base + std::to_wstring(suffix);
        if (!NameExists(scene, kind, candidate)) {
            return candidate;
        }
    }

    return base + L"_many";
}

Vector3 DuplicateOffsetForWire(const Scene& scene, const DrawableWire& wire)
{
    if (wire.sourcePlaneName.has_value()) {
        for (const DrawablePlane& plane : scene.planes) {
            if (plane.name == *wire.sourcePlaneName) {
                return (plane.plane.UAxis() + plane.plane.VAxis()) * 0.5;
            }
        }
    }

    return {0.5, 0.5, 0.0};
}

Selection SelectionAfterErase(SelectionKind kind, std::size_t erasedIndex, std::size_t remainingCount)
{
    if (remainingCount == 0) {
        return {};
    }

    return {kind, std::min(erasedIndex, remainingCount - 1)};
}

bool DuplicateSelection(AppState& state)
{
    Scene* scene = CurrentScene(state);
    if (scene == nullptr || !IsSelectionValid(*scene, state.selection)) {
        state.statusLine = L"Select an object before copying";
        return false;
    }

    PushUndo(state);
    if (state.selection.kind == SelectionKind::Wire) {
        const DrawableWire source = scene->wires[state.selection.index];
        DrawableWire copy = source;
        copy.name = MakeCopyName(*scene, SelectionKind::Wire, source.name);
        copy.wire = copy.wire.Translated(DuplicateOffsetForWire(*scene, source));
        scene->wires.push_back(std::move(copy));
        state.selection = {SelectionKind::Wire, scene->wires.size() - 1};
        RefreshScenePoints(*scene);
        state.statusLine = L"Copied wire " + scene->wires.back().name;
        return true;
    }

    if (state.selection.kind == SelectionKind::WorkPlane) {
        const DrawablePlane source = scene->planes[state.selection.index];
        DrawablePlane copy = source;
        copy.name = MakeCopyName(*scene, SelectionKind::WorkPlane, source.name);
        copy.plane = copy.plane.Translated(copy.plane.Normal() * 0.75);
        scene->planes.push_back(std::move(copy));
        state.selection = {SelectionKind::WorkPlane, scene->planes.size() - 1};
        RefreshScenePoints(*scene);
        state.statusLine = L"Copied plane " + scene->planes.back().name;
        return true;
    }

    return false;
}

bool DeleteSelection(AppState& state)
{
    Scene* scene = CurrentScene(state);
    if (scene == nullptr || !IsSelectionValid(*scene, state.selection)) {
        state.statusLine = L"Select an object before deleting";
        return false;
    }

    PushUndo(state);
    if (state.selection.kind == SelectionKind::Wire) {
        const std::wstring deletedName = scene->wires[state.selection.index].name;
        scene->wires.erase(scene->wires.begin() + static_cast<std::ptrdiff_t>(state.selection.index));
        state.selection = SelectionAfterErase(SelectionKind::Wire, state.selection.index, scene->wires.size());
        RefreshScenePoints(*scene);
        state.statusLine = L"Deleted wire " + deletedName;
        return true;
    }

    if (state.selection.kind == SelectionKind::WorkPlane) {
        const std::wstring deletedName = scene->planes[state.selection.index].name;
        scene->planes.erase(scene->planes.begin() + static_cast<std::ptrdiff_t>(state.selection.index));
        state.selection = SelectionAfterErase(SelectionKind::WorkPlane, state.selection.index, scene->planes.size());
        RefreshScenePoints(*scene);
        state.statusLine = L"Deleted plane " + deletedName + L"  wires kept";
        return true;
    }

    return false;
}

int RunEditSelfTest(std::vector<Scene> scenes)
{
    AppState state;
    state.scenes = std::move(scenes);
    if (state.scenes.empty()) {
        state.scenes = BuildScenes();
    }

    state.sceneIndex = 0;
    Scene* scene = CurrentScene(state);
    if (scene == nullptr || scene->wires.empty() || scene->planes.empty()) {
        return 20;
    }

    state.selection = {SelectionKind::Wire, 0};
    const std::size_t originalWireCount = scene->wires.size();
    const Vector3 originalWireStart = scene->wires[0].wire.Start();
    if (!NudgeSelection(state, {1.0, 0.0, 0.0})) {
        return 21;
    }

    scene = CurrentScene(state);
    if (scene == nullptr || !AlmostEqual(scene->wires[0].wire.Start(), originalWireStart + Vector3{1.0, 0.0, 0.0})) {
        return 22;
    }

    if (!UndoEdit(state)) {
        return 23;
    }

    scene = CurrentScene(state);
    if (scene == nullptr || !AlmostEqual(scene->wires[0].wire.Start(), originalWireStart)) {
        return 24;
    }

    if (!RedoEdit(state)) {
        return 25;
    }

    scene = CurrentScene(state);
    if (scene == nullptr || !AlmostEqual(scene->wires[0].wire.Start(), originalWireStart + Vector3{1.0, 0.0, 0.0})) {
        return 26;
    }

    if (!DuplicateSelection(state)) {
        return 27;
    }

    scene = CurrentScene(state);
    if (scene == nullptr || scene->wires.size() != originalWireCount + 1 || state.selection.kind != SelectionKind::Wire) {
        return 28;
    }

    if (!DeleteSelection(state)) {
        return 29;
    }

    scene = CurrentScene(state);
    if (scene == nullptr || scene->wires.size() != originalWireCount) {
        return 30;
    }

    if (!UndoEdit(state)) {
        return 31;
    }

    scene = CurrentScene(state);
    if (scene == nullptr || scene->wires.size() != originalWireCount + 1) {
        return 32;
    }

    state.selection = {SelectionKind::WorkPlane, 0};
    const std::size_t originalPlaneCount = scene->planes.size();
    if (!DuplicateSelection(state)) {
        return 33;
    }

    scene = CurrentScene(state);
    if (scene == nullptr || scene->planes.size() != originalPlaneCount + 1 || state.selection.kind != SelectionKind::WorkPlane) {
        return 34;
    }

    if (!DeleteSelection(state)) {
        return 35;
    }

    scene = CurrentScene(state);
    if (scene == nullptr || scene->planes.size() != originalPlaneCount) {
        return 36;
    }

    return 0;
}

bool SaveBitmap(std::wstring_view path, int width, int height, const void* pixels)
{
    BITMAPFILEHEADER fileHeader = {};
    BITMAPINFOHEADER infoHeader = {};

    const auto imageSize = static_cast<DWORD>(width * height * 4);

    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + imageSize;

    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = width;
    infoHeader.biHeight = -height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = imageSize;

    std::ofstream out(std::wstring(path), std::ios::binary);
    if (!out) {
        return false;
    }

    out.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    out.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));
    out.write(reinterpret_cast<const char*>(pixels), imageSize);

    return out.good();
}

int WriteSnapshot(std::wstring_view path, std::vector<Scene> scenes)
{
    constexpr int width = 1280;
    constexpr int height = 800;

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (bitmap == nullptr || pixels == nullptr) {
        return 2;
    }

    HDC hdc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldBitmap = SelectObject(hdc, bitmap);

    AppState state;
    state.scenes = std::move(scenes);
    state.sceneIndex = state.scenes.size() > 2 ? 2 : 0;
    DrawScene(hdc, width, height, state);

    const bool saved = SaveBitmap(path, width, height, pixels);

    SelectObject(hdc, oldBitmap);
    DeleteDC(hdc);
    DeleteObject(bitmap);

    return saved ? 0 : 3;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
    case WM_CREATE: {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (state != nullptr) {
            state->dragging = true;
            state->movedWhileDragging = false;
            state->mouseDown = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            state->lastMouse = state->mouseDown;
            SetCapture(hwnd);
        }
        return 0;

    case WM_LBUTTONUP:
        if (state != nullptr) {
            const POINT current = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (!state->movedWhileDragging) {
                RECT client = {};
                GetClientRect(hwnd, &client);
                const int width = client.right - client.left;
                const int height = client.bottom - client.top;
                const int panelLeft = std::max(0, width - InfoPanelWidth);
                const int viewportWidth = std::max(320, width - InfoPanelWidth);
                const Scene* scene = CurrentScene(*state);
                if (scene != nullptr) {
                    if (current.x >= panelLeft) {
                        const std::optional<Selection> panelSelection =
                            HitTestInfoPanel(*scene, *state, width, height, current);
                        if (panelSelection.has_value()) {
                            state->selection = *panelSelection;
                        }
                    } else {
                        state->selection = HitTestScene(*scene, state->camera, viewportWidth, height, current);
                    }
                } else {
                    state->selection = {};
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            state->dragging = false;
            ReleaseCapture();
        }
        return 0;

    case WM_MOUSEMOVE:
        if (state != nullptr && state->dragging) {
            const POINT current = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int dx = current.x - state->lastMouse.x;
            const int dy = current.y - state->lastMouse.y;
            const int totalDx = current.x - state->mouseDown.x;
            const int totalDy = current.y - state->mouseDown.y;
            if (totalDx * totalDx + totalDy * totalDy <= 9 && !state->movedWhileDragging) {
                return 0;
            }

            state->movedWhileDragging = true;
            state->camera.yaw += static_cast<double>(dx) * 0.008;
            state->camera.pitch = std::clamp(state->camera.pitch + static_cast<double>(dy) * 0.008, -1.25, 1.25);
            state->lastMouse = current;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (state != nullptr) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            state->camera.zoom *= (delta > 0) ? 1.1 : 0.9;
            state->camera.zoom = std::clamp(state->camera.zoom, 10.0, 120.0);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_KEYDOWN:
        if (state != nullptr) {
            bool handled = true;
            const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const double step = CurrentNudgeStep();
            if (ctrlDown && wParam == 'Z') {
                handled = shiftDown ? RedoEdit(*state) : UndoEdit(*state);
            } else if (ctrlDown && wParam == 'Y') {
                handled = RedoEdit(*state);
            } else if (wParam >= '1' && wParam <= '9') {
                const int requestedIndex = static_cast<int>(wParam - '1');
                if (requestedIndex < static_cast<int>(state->scenes.size())) {
                    state->sceneIndex = requestedIndex;
                    state->selection = {};
                    state->statusLine = L"Scene changed";
                }
            } else if (wParam == 'R') {
                state->camera = Camera{};
                state->statusLine = L"Camera reset";
            } else if (wParam == VK_TAB) {
                SelectNext(*state);
                state->statusLine = L"Selected next";
            } else if (wParam == VK_ESCAPE) {
                state->selection = {};
                state->statusLine = L"Selection cleared";
            } else if (wParam == 'C') {
                handled = DuplicateSelection(*state);
            } else if (wParam == VK_DELETE || wParam == VK_BACK) {
                handled = DeleteSelection(*state);
            } else if (wParam == 'A') {
                handled = NudgeSelection(*state, {-step, 0.0, 0.0});
            } else if (wParam == 'D') {
                handled = NudgeSelection(*state, {step, 0.0, 0.0});
            } else if (wParam == 'S') {
                handled = NudgeSelection(*state, {0.0, -step, 0.0});
            } else if (wParam == 'W') {
                handled = NudgeSelection(*state, {0.0, step, 0.0});
            } else if (wParam == 'Q') {
                handled = NudgeSelection(*state, {0.0, 0.0, -step});
            } else if (wParam == 'E') {
                handled = NudgeSelection(*state, {0.0, 0.0, step});
            } else if (wParam == VK_LEFT) {
                state->camera.yaw -= 0.1;
            } else if (wParam == VK_RIGHT) {
                state->camera.yaw += 0.1;
            } else if (wParam == VK_UP) {
                state->camera.pitch = std::clamp(state->camera.pitch + 0.1, -1.25, 1.25);
            } else if (wParam == VK_DOWN) {
                state->camera.pitch = std::clamp(state->camera.pitch - 0.1, -1.25, 1.25);
            } else {
                handled = false;
            }

            if (handled) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;

    case WM_PAINT:
        if (state != nullptr) {
            PAINTSTRUCT paint;
            HDC paintDc = BeginPaint(hwnd, &paint);

            RECT client = {};
            GetClientRect(hwnd, &client);
            const int width = client.right - client.left;
            const int height = client.bottom - client.top;

            HDC memoryDc = CreateCompatibleDC(paintDc);
            HBITMAP backBuffer = CreateCompatibleBitmap(paintDc, width, height);
            HGDIOBJ oldBitmap = SelectObject(memoryDc, backBuffer);

            DrawScene(memoryDc, width, height, *state);
            BitBlt(paintDc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);

            SelectObject(memoryDc, oldBitmap);
            DeleteObject(backBuffer);
            DeleteDC(memoryDc);

            EndPaint(hwnd, &paint);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

int RunWindow(std::vector<Scene> scenes)
{
    AppState state;
    state.scenes = std::move(scenes);
    if (state.scenes.empty()) {
        state.scenes = BuildScenes();
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"KachakachaCadViewerWindow";

    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (RegisterClassW(&windowClass) == 0) {
        return 4;
    }

    HWND hwnd = CreateWindowExW(
        0,
        className,
        L"kachakachaCAD Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        820,
        nullptr,
        nullptr,
        instance,
        &state);

    if (hwnd == nullptr) {
        return 5;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    try {
        std::optional<std::filesystem::path> snapshotPath;
        std::optional<std::filesystem::path> projectPath;
        bool selfTestEdits = false;

        for (int i = 1; i < argc; ++i) {
            const std::wstring_view argument = argv[i];
            if (argument == L"--snapshot") {
                if (i + 1 >= argc) {
                    return 6;
                }
                snapshotPath = std::filesystem::path(argv[++i]);
            } else if (argument == L"--project") {
                if (i + 1 >= argc) {
                    return 7;
                }
                projectPath = std::filesystem::path(argv[++i]);
            } else if (argument == L"--self-test-edits") {
                selfTestEdits = true;
            } else {
                return 8;
            }
        }

        std::vector<Scene> scenes = projectPath.has_value()
            ? LoadScenesFromProjectFile(*projectPath)
            : BuildScenes();

        if (selfTestEdits) {
            return RunEditSelfTest(std::move(scenes));
        }

        if (snapshotPath.has_value()) {
            return WriteSnapshot(snapshotPath->wstring(), std::move(scenes));
        }

        return RunWindow(std::move(scenes));
    } catch (const std::exception&) {
        return 1;
    }
}
