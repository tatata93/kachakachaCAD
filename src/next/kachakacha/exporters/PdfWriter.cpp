#include "kachakacha/exporters/PdfWriter.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <cmath>
#include <cstdio>

namespace kachakacha::v2::exporters {

using base::MakeError;
using base::Result;
using geometry::CurveKind;
using geometry::CurveSegment;
using geometry::Vector3;

namespace {

constexpr const char* kBadPage = "EXP-D001";
constexpr const char* kNotPlanar = "EXP-D002";

//! PDF の単位は 1/72 インチ。mm から直す。
[[nodiscard]] double ToPoints(double millimeters)
{
    return millimeters * 72.0 / 25.4;
}

[[nodiscard]] std::string Number(double value, int digits = 3)
{
    if (!geometry::IsFinite(value)) {
        return "0";
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    std::string text(buffer);
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text == "-0" ? "0" : text;
}

[[nodiscard]] std::string Point(const Vector3& point)
{
    // PDF の y は上向き。型紙も上向きなので、そのまま。
    return Number(ToPoints(point.x)) + " " + Number(ToPoints(point.y));
}

//! 円弧を3次Bezierの列にする。折れ線にはしない。
//! 90度ごとに区切れば、誤差は線の太さよりずっと小さくなる。
void AppendArc(std::string& content, const CurveSegment& arc)
{
    const double sweep = arc.SweepAngleRad();
    const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / 1.5707963267948966)));
    for (int index = 0; index < pieces; ++index) {
        const double t0 = static_cast<double>(index) / static_cast<double>(pieces);
        const double t1 = static_cast<double>(index + 1) / static_cast<double>(pieces);
        const Vector3 p0 = arc.Evaluate(t0);
        const Vector3 p3 = arc.Evaluate(t1);
        // 端点での接線から制御点を出す。円弧のBezier近似の定石。
        const double segmentSweep = sweep * (t1 - t0);
        const double handle = 4.0 / 3.0 * std::tan(segmentSweep / 4.0) * arc.Radius();
        const Vector3 tangent0 = geometry::Normalized(arc.FirstDerivative(t0));
        const Vector3 tangent1 = geometry::Normalized(arc.FirstDerivative(t1));
        const Vector3 p1 = p0 + tangent0 * handle;
        const Vector3 p2 = p3 - tangent1 * handle;
        content += Point(p1) + " " + Point(p2) + " " + Point(p3) + " c\n";
    }
}

void AppendCurve(std::string& content, const CurveSegment& segment)
{
    content += Point(segment.StartPoint()) + " m\n";
    switch (segment.Kind()) {
    case CurveKind::Line:
        content += Point(segment.EndPoint()) + " l\n";
        break;
    case CurveKind::CircularArc:
    case CurveKind::Circle:
        AppendArc(content, segment);
        break;
    case CurveKind::CubicBezier: {
        const auto& control = segment.ControlPoints();
        content += Point(control[1]) + " " + Point(control[2]) + " " + Point(control[3])
            + " c\n";
        break;
    }
    case CurveKind::CubicBSpline: {
        // 3次Bezierの列へ厳密に置き換える。形は変わらない。
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
                content = content.substr(0, content.size());
                content += Point(b0) + " m\n";
            }
            content += Point(b1) + " " + Point(b2) + " " + Point(b3) + " c\n";
        }
        break;
    }
    }
    content += "S\n";
}

//! レイヤーごとの線の見た目。
void AppendStyle(std::string& content, PatternLine layer, bool mountain)
{
    switch (layer) {
    case PatternLine::Outline:
        content += "0 0 0 RG\n0.2 w\n[] 0 d\n";
        break;
    case PatternLine::Fold:
        if (mountain) {
            content += "0.82 0.13 0.13 RG\n0.15 w\n[3 1.5] 0 d\n";
        } else {
            content += "0.13 0.31 0.82 RG\n0.15 w\n[1.5 1.5 0.4 1.5] 0 d\n";
        }
        break;
    case PatternLine::Cut:
        content += "0 0.5 0.25 RG\n0.2 w\n[] 0 d\n";
        break;
    case PatternLine::Opening:
        content += "0 0 0 RG\n0.2 w\n[] 0 d\n";
        break;
    case PatternLine::Annotation:
        content += "0.5 0.5 0.5 RG\n0.1 w\n[1 1] 0 d\n";
        break;
    }
}

//! PDF の文字列。括弧と逆斜線を逃がす。
[[nodiscard]] std::string PdfString(const std::string& text)
{
    std::string escaped = "(";
    for (const char character : text) {
        if (character == '(' || character == ')' || character == '\\') {
            escaped.push_back('\\');
        }
        // 非ASCIIはそのまま入れない(PDFの既定の文字集合に無い)。
        if (static_cast<unsigned char>(character) < 0x80) {
            escaped.push_back(character);
        }
    }
    escaped.push_back(')');
    return escaped;
}

[[nodiscard]] bool OnPlane(const CurveSegment& segment, double toleranceMm)
{
    const auto sampled = geometry::SampleCurve(segment, std::max(toleranceMm, 1.0e-3));
    for (const auto& point : sampled) {
        if (std::abs(point.position.z) > toleranceMm) {
            return false;
        }
    }
    return true;
}

} // namespace

Result<std::string> WritePatternPdf(const std::vector<PatternPage>& pages,
    const PdfMetadata& metadata)
{
    if (pages.empty()) {
        return Result<std::string>::Failure(MakeError(kBadPage, "ページがありません。", {}));
    }
    for (const PatternPage& page : pages) {
        if (!(page.widthMm > 0.0) || !(page.heightMm > 0.0)) {
            return Result<std::string>::Failure(MakeError(kBadPage,
                "ページの大きさが正しくありません。", {}));
        }
        for (const PatternCurve& curve : page.curves) {
            if (!OnPlane(curve.segment, 1.0e-6)) {
                return Result<std::string>::Failure(MakeError(kNotPlanar,
                    "型紙の面に載っていない線があります。", {}));
            }
        }
    }

    // ページごとの内容を作る。
    std::vector<std::string> contents;
    for (const PatternPage& page : pages) {
        std::string content;
        content += "1 J\n1 j\n";   // 線の端と角を丸く
        const PatternLine order[]{PatternLine::Outline, PatternLine::Opening,
            PatternLine::Cut, PatternLine::Fold, PatternLine::Annotation};
        for (const PatternLine layer : order) {
            for (const PatternCurve& curve : page.curves) {
                if (curve.layer != layer) {
                    continue;
                }
                AppendStyle(content, layer, curve.mountainFold);
                AppendCurve(content, curve.segment);
            }
        }
        contents.push_back(std::move(content));
    }

    // オブジェクトを組み立てる。
    // 1: カタログ、2: ページの親、3..: ページとその内容。
    std::vector<std::string> objects;
    const std::size_t pageCount = pages.size();
    const std::size_t firstPageObject = 3;

    std::string kids;
    for (std::size_t index = 0; index < pageCount; ++index) {
        kids += std::to_string(firstPageObject + index * 2) + " 0 R ";
    }

    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    objects.push_back("<< /Type /Pages /Count " + std::to_string(pageCount) + " /Kids ["
        + kids + "] >>");
    for (std::size_t index = 0; index < pageCount; ++index) {
        const std::size_t contentObject = firstPageObject + index * 2 + 1;
        objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
            + Number(ToPoints(pages[index].widthMm)) + " "
            + Number(ToPoints(pages[index].heightMm)) + "] /Contents "
            + std::to_string(contentObject) + " 0 R /Resources << >> >>");
        objects.push_back("<< /Length " + std::to_string(contents[index].size())
            + " >>\nstream\n" + contents[index] + "endstream");
    }
    const std::size_t infoObject = objects.size() + 1;
    std::string info = "<< /Title " + PdfString(metadata.title.empty() ? "pattern"
                                                                      : metadata.title);
    if (metadata.referenceScaleDenominator > 0.0) {
        info += " /Subject " + PdfString("scale 1/"
            + Number(metadata.referenceScaleDenominator, 0) + " actual size");
    }
    info += " /Producer " + PdfString("kachakachaCAD") + " >>";
    objects.push_back(info);

    // 本体を書き出しつつ、各オブジェクトの位置を覚える。
    std::string pdf = "%PDF-1.4\n";
    std::vector<std::size_t> offsets;
    for (std::size_t index = 0; index < objects.size(); ++index) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(index + 1) + " 0 obj\n";
        pdf += objects[index];
        pdf += "\nendobj\n";
    }
    const std::size_t xrefStart = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    for (const std::size_t offset : offsets) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%010zu 00000 n \n", offset);
        pdf += buffer;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1)
        + " /Root 1 0 R /Info " + std::to_string(infoObject) + " 0 R >>\n";
    pdf += "startxref\n" + std::to_string(xrefStart) + "\n%%EOF\n";
    return Result<std::string>::Success(std::move(pdf));
}

} // namespace kachakacha::v2::exporters
