#include "kachakacha/exporters/PatternExport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace kachakacha::v2::exporters {

using base::MakeError;
using base::Result;
using geometry::CurveKind;
using geometry::Vector3;

namespace {

constexpr const char* kBadPage = "EXP-P001";
constexpr const char* kNotPlanar = "EXP-P002";
constexpr const char* kBadMesh = "EXP-M001";

//! 数を、地域設定に左右されない形で書く。小数点はいつも '.'。
[[nodiscard]] std::string Number(double value, int digits = 4)
{
    if (!geometry::IsFinite(value)) {
        return "0";
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    std::string text(buffer);
    // 末尾の0を落として短くする。決定的であることは変わらない。
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    if (text == "-0") {
        text = "0";
    }
    return text;
}

//! 型紙の座標は XY 平面。z が残っていたら、それは呼び出し側の誤り。
[[nodiscard]] bool OnPatternPlane(const CurveSegment& segment, double toleranceMm)
{
    const auto sampled = geometry::SampleCurve(segment, std::max(toleranceMm, 1.0e-3));
    for (const auto& point : sampled) {
        if (std::abs(point.position.z) > toleranceMm) {
            return false;
        }
    }
    return true;
}

//! SVG の y は下向き。型紙の y は上向きなので、ページの高さから引く。
[[nodiscard]] std::string SvgPoint(const Vector3& point, double pageHeight)
{
    return Number(point.x) + "," + Number(pageHeight - point.y);
}

[[nodiscard]] std::string SvgPathFor(const CurveSegment& segment, double pageHeight)
{
    std::string path = "M " + SvgPoint(segment.StartPoint(), pageHeight);
    switch (segment.Kind()) {
    case CurveKind::Line:
        path += " L " + SvgPoint(segment.EndPoint(), pageHeight);
        break;
    case CurveKind::CircularArc: {
        // SVG の円弧コマンド。折れ線へ落とさない。
        const double radius = segment.Radius();
        const double sweep = segment.SweepAngleRad();
        const bool largeArc = std::abs(sweep) > 3.14159265358979323846;
        // 型紙の y を反転しているので、回る向きも反転する。
        const bool sweepFlag = sweep < 0.0;
        path += " A " + Number(radius) + "," + Number(radius) + " 0 "
            + (largeArc ? "1" : "0") + "," + (sweepFlag ? "1" : "0") + " "
            + SvgPoint(segment.EndPoint(), pageHeight);
        break;
    }
    case CurveKind::Circle: {
        // 半円2つで1周にする。1つの円弧コマンドでは1周を書けない。
        const double radius = segment.Radius();
        const Vector3 middle = segment.Evaluate(0.5);
        path += " A " + Number(radius) + "," + Number(radius) + " 0 0,0 "
            + SvgPoint(middle, pageHeight);
        path += " A " + Number(radius) + "," + Number(radius) + " 0 0,0 "
            + SvgPoint(segment.EndPoint(), pageHeight);
        path += " Z";
        break;
    }
    case CurveKind::CubicBezier: {
        const auto& control = segment.ControlPoints();
        path += " C " + SvgPoint(control[1], pageHeight) + " "
            + SvgPoint(control[2], pageHeight) + " " + SvgPoint(control[3], pageHeight);
        break;
    }
    case CurveKind::CubicBSpline: {
        // B-spline は SVG に無いので、3次Bezierの列へ厳密に置き換える。
        // 折れ線へ落とすのとは違い、形は変わらない。
        const auto& control = segment.ControlPoints();
        for (std::size_t index = 0; index + 3 < control.size(); ++index) {
            const Vector3& p0 = control[index];
            const Vector3& p1 = control[index + 1];
            const Vector3& p2 = control[index + 2];
            const Vector3& p3 = control[index + 3];
            const Vector3 b0 = (p0 + p1 * 4.0 + p2) * (1.0 / 6.0);
            const Vector3 b1 = (p1 * 2.0 + p2) * (1.0 / 3.0);
            const Vector3 b2 = (p1 + p2 * 2.0) * (1.0 / 3.0);
            const Vector3 b3 = (p1 + p2 * 4.0 + p3) * (1.0 / 6.0);
            if (index == 0) {
                path = "M " + SvgPoint(b0, pageHeight);
            }
            path += " C " + SvgPoint(b1, pageHeight) + " " + SvgPoint(b2, pageHeight) + " "
                + SvgPoint(b3, pageHeight);
        }
        break;
    }
    }
    return path;
}

[[nodiscard]] std::string SvgStyleFor(PatternLine layer, bool mountain)
{
    switch (layer) {
    case PatternLine::Outline:
        return "fill:none;stroke:#000000;stroke-width:0.2";
    case PatternLine::Fold:
        return mountain ? "fill:none;stroke:#d02020;stroke-width:0.15;stroke-dasharray:3,1.5"
                        : "fill:none;stroke:#2050d0;stroke-width:0.15;"
                          "stroke-dasharray:1.5,1.5,0.4,1.5";
    case PatternLine::Cut:
        return "fill:none;stroke:#008040;stroke-width:0.2";
    case PatternLine::Opening:
        return "fill:none;stroke:#000000;stroke-width:0.2";
    case PatternLine::Annotation:
        return "fill:none;stroke:#808080;stroke-width:0.1";
    }
    return "fill:none;stroke:#000000;stroke-width:0.2";
}

//! DXF のグループコード1組。
void PutDxf(std::string& out, int code, const std::string& value)
{
    out += std::to_string(code);
    out += "\n";
    out += value;
    out += "\n";
}

void PutLittleEndian(std::string& out, std::uint32_t value)
{
    for (int index = 0; index < 4; ++index) {
        out.push_back(static_cast<char>((value >> (8 * index)) & 0xFF));
    }
}

void PutFloat(std::string& out, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    PutLittleEndian(out, bits);
}

} // namespace

std::string_view PatternLineLayerName(PatternLine line) noexcept
{
    // レイヤー名は機械が読むので、日本語にしない。
    switch (line) {
    case PatternLine::Outline:    return "OUTLINE";
    case PatternLine::Fold:       return "FOLD";
    case PatternLine::Cut:        return "RELIEF";
    case PatternLine::Opening:    return "OPENING";
    case PatternLine::Annotation: return "ANNOTATION";
    }
    return "OUTLINE";
}

Result<std::string> WritePatternSvg(const PatternPage& page, const std::string& title)
{
    if (!(page.widthMm > 0.0) || !(page.heightMm > 0.0)) {
        return Result<std::string>::Failure(MakeError(kBadPage,
            "ページの大きさが正しくありません。", {}));
    }
    for (const PatternCurve& curve : page.curves) {
        if (!OnPatternPlane(curve.segment, 1.0e-6)) {
            return Result<std::string>::Failure(MakeError(kNotPlanar,
                "型紙の面に載っていない線があります。",
                "型紙は平面です。3次元の線をそのまま渡していないか確かめてください。"));
        }
    }

    std::string svg;
    svg += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\"\n";
    svg += "     width=\"" + Number(page.widthMm) + "mm\" height=\""
        + Number(page.heightMm) + "mm\"\n";
    // 実寸で出す。viewBox を mm と1対1にしておく。
    svg += "     viewBox=\"0 0 " + Number(page.widthMm) + " " + Number(page.heightMm)
        + "\">\n";
    svg += "  <title>" + title + "</title>\n";

    // レイヤーごとにまとめる。順番は固定。
    const PatternLine order[]{PatternLine::Outline, PatternLine::Opening, PatternLine::Cut,
        PatternLine::Fold, PatternLine::Annotation};
    for (const PatternLine layer : order) {
        bool opened = false;
        for (const PatternCurve& curve : page.curves) {
            if (curve.layer != layer) {
                continue;
            }
            if (!opened) {
                svg += "  <g id=\"" + std::string(PatternLineLayerName(layer))
                    + "\" inkscape:label=\"" + std::string(PatternLineLayerName(layer))
                    + "\" inkscape:groupmode=\"layer\">\n";
                opened = true;
            }
            svg += "    <path d=\"" + SvgPathFor(curve.segment, page.heightMm) + "\" style=\""
                + SvgStyleFor(layer, curve.mountainFold) + "\"";
            if (!curve.label.empty()) {
                svg += " data-label=\"" + curve.label + "\"";
            }
            svg += "/>\n";
        }
        if (opened) {
            svg += "  </g>\n";
        }
    }
    svg += "</svg>\n";
    return Result<std::string>::Success(std::move(svg));
}

Result<std::string> WritePatternDxf(const PatternPage& page)
{
    if (!(page.widthMm > 0.0) || !(page.heightMm > 0.0)) {
        return Result<std::string>::Failure(MakeError(kBadPage,
            "ページの大きさが正しくありません。", {}));
    }
    for (const PatternCurve& curve : page.curves) {
        if (!OnPatternPlane(curve.segment, 1.0e-6)) {
            return Result<std::string>::Failure(MakeError(kNotPlanar,
                "型紙の面に載っていない線があります。", {}));
        }
    }
    std::string dxf;
    PutDxf(dxf, 0, "SECTION");
    PutDxf(dxf, 2, "ENTITIES");
    for (const PatternCurve& curve : page.curves) {
        const std::string layer(PatternLineLayerName(curve.layer));
        switch (curve.segment.Kind()) {
        case CurveKind::Line: {
            PutDxf(dxf, 0, "LINE");
            PutDxf(dxf, 8, layer);
            PutDxf(dxf, 10, Number(curve.segment.StartPoint().x, 6));
            PutDxf(dxf, 20, Number(curve.segment.StartPoint().y, 6));
            PutDxf(dxf, 11, Number(curve.segment.EndPoint().x, 6));
            PutDxf(dxf, 21, Number(curve.segment.EndPoint().y, 6));
            break;
        }
        case CurveKind::Circle: {
            PutDxf(dxf, 0, "CIRCLE");
            PutDxf(dxf, 8, layer);
            PutDxf(dxf, 10, Number(curve.segment.Center().x, 6));
            PutDxf(dxf, 20, Number(curve.segment.Center().y, 6));
            PutDxf(dxf, 40, Number(curve.segment.Radius(), 6));
            break;
        }
        case CurveKind::CircularArc: {
            // DXF の ARC は必ず反時計回り。掃引が負なら始点と終点を入れ替える。
            const Vector3 center = curve.segment.Center();
            const Vector3 start = curve.segment.StartPoint();
            const Vector3 end = curve.segment.EndPoint();
            const double startAngle =
                std::atan2(start.y - center.y, start.x - center.x) * 180.0 / 3.14159265358979323846;
            const double endAngle =
                std::atan2(end.y - center.y, end.x - center.x) * 180.0 / 3.14159265358979323846;
            const bool reversed = curve.segment.SweepAngleRad() < 0.0;
            PutDxf(dxf, 0, "ARC");
            PutDxf(dxf, 8, layer);
            PutDxf(dxf, 10, Number(center.x, 6));
            PutDxf(dxf, 20, Number(center.y, 6));
            PutDxf(dxf, 40, Number(curve.segment.Radius(), 6));
            PutDxf(dxf, 50, Number(reversed ? endAngle : startAngle, 6));
            PutDxf(dxf, 51, Number(reversed ? startAngle : endAngle, 6));
            break;
        }
        case CurveKind::CubicBezier:
        case CurveKind::CubicBSpline: {
            // SPLINE として制御点をそのまま出す。折れ線にしない。
            const auto& control = curve.segment.ControlPoints();
            PutDxf(dxf, 0, "SPLINE");
            PutDxf(dxf, 8, layer);
            PutDxf(dxf, 70, "8");   // planar
            PutDxf(dxf, 71, "3");   // 次数
            PutDxf(dxf, 72, std::to_string(control.size() + 4));   // ノット数
            PutDxf(dxf, 73, std::to_string(control.size()));
            PutDxf(dxf, 74, "0");
            for (std::size_t index = 0; index < control.size() + 4; ++index) {
                const double knot = index < 4 ? 0.0
                    : (index >= control.size() ? 1.0
                                               : static_cast<double>(index - 3)
                            / static_cast<double>(control.size() - 3));
                PutDxf(dxf, 40, Number(knot, 6));
            }
            for (const Vector3& point : control) {
                PutDxf(dxf, 10, Number(point.x, 6));
                PutDxf(dxf, 20, Number(point.y, 6));
                PutDxf(dxf, 30, "0");
            }
            break;
        }
        }
    }
    PutDxf(dxf, 0, "ENDSEC");
    PutDxf(dxf, 0, "EOF");
    return Result<std::string>::Success(std::move(dxf));
}

Result<std::string> WriteAsciiStl(const std::vector<Triangle>& triangles,
    const std::string& name)
{
    if (triangles.empty()) {
        return Result<std::string>::Failure(MakeError(kBadMesh,
            "三角形が1つもありません。", {}));
    }
    std::string stl = "solid " + name + "\n";
    for (const Triangle& triangle : triangles) {
        const Vector3 normal = geometry::Normalized(
            Cross(triangle.second - triangle.first, triangle.third - triangle.first));
        // 潰れた三角形は法線が零ベクトルになる。零ベクトルは「有限」なので、
        // IsFinite だけでは見逃す。長さで見る。
        if (!normal.IsFinite() || !(normal.Length() > 0.0)) {
            return Result<std::string>::Failure(MakeError(kBadMesh,
                "潰れた三角形が入っています。", {}));
        }
        stl += "  facet normal " + Number(normal.x, 6) + " " + Number(normal.y, 6) + " "
            + Number(normal.z, 6) + "\n";
        stl += "    outer loop\n";
        for (const Vector3& point : {triangle.first, triangle.second, triangle.third}) {
            stl += "      vertex " + Number(point.x, 6) + " " + Number(point.y, 6) + " "
                + Number(point.z, 6) + "\n";
        }
        stl += "    endloop\n";
        stl += "  endfacet\n";
    }
    stl += "endsolid " + name + "\n";
    return Result<std::string>::Success(std::move(stl));
}

Result<std::string> WriteBinaryStl(const std::vector<Triangle>& triangles)
{
    if (triangles.empty()) {
        return Result<std::string>::Failure(MakeError(kBadMesh,
            "三角形が1つもありません。", {}));
    }
    std::string stl;
    // 80バイトの見出し。日付を入れると同じ入力から違うバイト列が出るので入れない。
    stl.append(80, '\0');
    const std::string tag = "kachakachaCAD";
    for (std::size_t index = 0; index < tag.size(); ++index) {
        stl[index] = tag[index];
    }
    PutLittleEndian(stl, static_cast<std::uint32_t>(triangles.size()));
    for (const Triangle& triangle : triangles) {
        const Vector3 normal = geometry::Normalized(
            Cross(triangle.second - triangle.first, triangle.third - triangle.first));
        // 潰れた三角形は法線が零ベクトルになる。零ベクトルは「有限」なので、
        // IsFinite だけでは見逃す。長さで見る。
        if (!normal.IsFinite() || !(normal.Length() > 0.0)) {
            return Result<std::string>::Failure(MakeError(kBadMesh,
                "潰れた三角形が入っています。", {}));
        }
        for (const Vector3& point : {normal, triangle.first, triangle.second,
                 triangle.third}) {
            PutFloat(stl, static_cast<float>(point.x));
            PutFloat(stl, static_cast<float>(point.y));
            PutFloat(stl, static_cast<float>(point.z));
        }
        stl.push_back('\0');
        stl.push_back('\0');
    }
    return Result<std::string>::Success(std::move(stl));
}

} // namespace kachakacha::v2::exporters
