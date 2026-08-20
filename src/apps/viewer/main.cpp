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
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using kachakacha::geometry::Vector3;
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

struct DrawableWire {
    Wire wire;
    COLORREF color = RGB(0, 0, 0);
    int width = 2;
};

struct DrawablePlane {
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
    std::vector<std::wstring> infoLines;
};

struct AppState {
    Camera camera;
    int sceneIndex = 0;
    std::vector<Scene> scenes;
    bool dragging = false;
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

void DrawWire(HDC hdc, const Camera& camera, int width, int height, const DrawableWire& drawable)
{
    WithPen(hdc, drawable.color, drawable.width, [&]() {
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
}

void DrawInfoPanel(HDC hdc, int width, int height, const Scene& scene)
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
    DrawTextLine(hdc, panelLeft + 16, 18, L"Project");
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

    int y = 48;
    const int lineHeight = 19;
    const int maxY = height - 24;
    for (const std::wstring& line : scene.infoLines) {
        if (y > maxY) {
            DrawTextLine(hdc, panelLeft + 16, y, L"...");
            break;
        }

        DrawTextLine(hdc, panelLeft + 16, y, line);
        y += lineHeight;
    }

    SelectObject(hdc, oldFont);
    DeleteObject(bodyFont);
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

void DrawPlane(HDC hdc, const Camera& camera, int width, int height, const DrawablePlane& drawable)
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

    HBRUSH brush = CreateSolidBrush(RGB(248, 250, 252));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HPEN outlinePen = CreatePen(PS_SOLID, 1, drawable.color);
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

    WithPen(hdc, RGB(80, 112, 150), 2, [&]() {
        DrawLine3D(hdc, camera, width, height, plane.Origin(), plane.ToWorld(2.0, 0.0));
        DrawLine3D(hdc, camera, width, height, plane.Origin(), plane.ToWorld(0.0, 2.0));
    });
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
        {{xyPlane, 8.0, RGB(180, 195, 210), RGB(232, 237, 242)}},
        {{
            Wire::Line({0.0, 0.0, 0.0}, {2.0, 7.0, 4.0}),
            RGB(34, 108, 214),
            3,
        }},
        {{0.0, 0.0, 0.0}, {2.0, 7.0, 4.0}},
    });

    scenes.push_back({
        L"Sketch on X=10 work plane",
        {
            {xyPlane, 8.0, RGB(198, 207, 218), RGB(236, 240, 244)},
            {verticalPlane, 7.0, RGB(160, 190, 218), RGB(222, 235, 246)},
        },
        {
            {
                verticalSketch.MakeLine({2.0, 1.0}, {5.0, 6.0}),
                RGB(25, 128, 91),
                3,
            },
            {
                verticalSketch.MakeCubicBezier({0.0, 0.0}, {1.0, 4.0}, {5.0, 4.0}, {6.0, 0.0}),
                RGB(203, 86, 48),
                3,
            },
        },
        {{10.0, 0.0, 0.0}},
    });

    scenes.push_back({
        L"Tilted plane from three selected points",
        {
            {xyPlane, 8.0, RGB(205, 210, 217), RGB(238, 240, 243)},
            {tiltedPlane, 7.0, RGB(183, 162, 214), RGB(233, 225, 244)},
        },
        {
            {
                tiltedSketch.MakeLine({-4.0, -2.0}, {5.0, 3.0}),
                RGB(43, 103, 169),
                3,
            },
            {
                tiltedSketch.MakeCubicBezier({-4.0, 3.0}, {-1.0, 6.0}, {3.0, -2.0}, {5.0, 1.0}),
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
            workPlane.plane,
            7.0,
            RGB(160, 190, 218),
            RGB(224, 234, 244),
        });
        scene.points.push_back(workPlane.plane.Origin());
    }

    scene.infoLines.push_back(L"Work planes");
    for (const NamedWorkPlane& workPlane : project.WorkPlanes()) {
        scene.infoLines.push_back(L"  " + ToWide(workPlane.name));
        scene.infoLines.push_back(L"    O " + FormatVector(workPlane.plane.Origin()));
    }

    scene.infoLines.push_back(L"");
    scene.infoLines.push_back(L"Wires");

    std::size_t wireIndex = 0;
    for (const NamedWire& wire : project.Wires()) {
        scene.wires.push_back({
            wire.wire,
            PickWireColor(wireIndex),
            wire.metadata.planePolicy == WirePlanePolicy::LockedToPlane ? 4 : 3,
        });

        for (const Vector3& controlPoint : wire.wire.ControlPoints()) {
            scene.points.push_back(controlPoint);
        }

        std::wstring label = L"  " + ToWide(wire.name)
            + L"  " + WireKindName(wire.wire.Kind())
            + L"  " + PlanePolicyName(wire.metadata.planePolicy);
        if (wire.metadata.sourcePlaneName.has_value()) {
            label += L"  @" + ToWide(*wire.metadata.sourcePlaneName);
        }

        scene.infoLines.push_back(label);
        scene.infoLines.push_back(L"    S " + FormatVector(wire.wire.Start()));
        scene.infoLines.push_back(L"    E " + FormatVector(wire.wire.End()));

        ++wireIndex;
    }

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
    const int viewportWidth = std::max(320, width - InfoPanelWidth);

    RECT background = {0, 0, width, height};
    HBRUSH backgroundBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &background, backgroundBrush);
    DeleteObject(backgroundBrush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(38, 45, 53));

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
    DrawTextLine(hdc, 18, 66, L"1-9 scene  |  drag rotate  |  wheel zoom  |  R reset");

    SelectObject(hdc, oldFont);
    DeleteObject(font);

    DrawAxes(hdc, state.camera, viewportWidth, height);

    for (const DrawablePlane& plane : scene.planes) {
        DrawPlane(hdc, state.camera, viewportWidth, height, plane);
    }

    for (const DrawableWire& wire : scene.wires) {
        DrawWire(hdc, state.camera, viewportWidth, height, wire);
    }

    for (const Vector3& point : scene.points) {
        DrawPoint(hdc, state.camera, viewportWidth, height, point, RGB(25, 25, 25));
    }

    DrawInfoPanel(hdc, width, height, scene);
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
            state->lastMouse = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            SetCapture(hwnd);
        }
        return 0;

    case WM_LBUTTONUP:
        if (state != nullptr) {
            state->dragging = false;
            ReleaseCapture();
        }
        return 0;

    case WM_MOUSEMOVE:
        if (state != nullptr && state->dragging) {
            const POINT current = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int dx = current.x - state->lastMouse.x;
            const int dy = current.y - state->lastMouse.y;
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
            if (wParam >= '1' && wParam <= '9') {
                const int requestedIndex = static_cast<int>(wParam - '1');
                if (requestedIndex < static_cast<int>(state->scenes.size())) {
                    state->sceneIndex = requestedIndex;
                }
            } else if (wParam == 'R') {
                state->camera = Camera{};
            } else if (wParam == VK_LEFT) {
                state->camera.yaw -= 0.1;
            } else if (wParam == VK_RIGHT) {
                state->camera.yaw += 0.1;
            } else if (wParam == VK_UP) {
                state->camera.pitch = std::clamp(state->camera.pitch + 0.1, -1.25, 1.25);
            } else if (wParam == VK_DOWN) {
                state->camera.pitch = std::clamp(state->camera.pitch - 0.1, -1.25, 1.25);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
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
            } else {
                return 8;
            }
        }

        std::vector<Scene> scenes = projectPath.has_value()
            ? LoadScenesFromProjectFile(*projectPath)
            : BuildScenes();

        if (snapshotPath.has_value()) {
            return WriteSnapshot(snapshotPath->wstring(), std::move(scenes));
        }

        return RunWindow(std::move(scenes));
    } catch (const std::exception&) {
        return 1;
    }
}
