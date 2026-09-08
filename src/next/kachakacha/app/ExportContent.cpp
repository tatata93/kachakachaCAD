#include "kachakacha/app/ExportContent.h"

#include "kachakacha/exporters/PdfWriter.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/WireEdit.h"

#include <algorithm>
#include <string>
#include <utility>
#include <limits>

namespace kachakacha::v2::app {
namespace {

using base::MakeError;
using base::Result;
using geometry::CurveSegment;
using geometry::Vector3;

//! 中身の広がり。XY だけ見る。
struct Extent {
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = -std::numeric_limits<double>::max();
    double maxY = -std::numeric_limits<double>::max();

    [[nodiscard]] bool Empty() const noexcept { return minX > maxX; }
};

[[nodiscard]] Extent MeasureExtent(const std::vector<CurveSegment>& segments,
    double toleranceMm)
{
    Extent extent;
    for (const CurveSegment& segment : segments) {
        for (const auto& point : geometry::SampleCurve(segment, toleranceMm)) {
            extent.minX = std::min(extent.minX, point.position.x);
            extent.minY = std::min(extent.minY, point.position.y);
            extent.maxX = std::max(extent.maxX, point.position.x);
            extent.maxY = std::max(extent.maxY, point.position.y);
        }
    }
    return extent;
}

} // namespace

Result<exporters::PatternPage> BuildWirePatternPage(const WirePatternRequest& request,
    double toleranceMm)
{
    using Out = Result<exporters::PatternPage>;
    if (request.segments.empty()) {
        return Out::Failure(MakeError("EXP-015", "出すものが選ばれていません。",
            "ワイヤーが1本も選ばれていません。"));
    }
    if (!(request.marginMm >= 0.0)) {
        return Out::Failure(MakeError("EXP-P001", "ページの大きさが正しくありません。",
            "余白が負の数です。"));
    }
    const Extent extent = MeasureExtent(request.segments, toleranceMm);
    if (extent.Empty()) {
        return Out::Failure(MakeError("EXP-015", "出すものが選ばれていません。",
            "選ばれたワイヤーから点を1つも取れませんでした。"));
    }
    exporters::PatternPage page;
    page.widthMm = (extent.maxX - extent.minX) + 2.0 * request.marginMm;
    page.heightMm = (extent.maxY - extent.minY) + 2.0 * request.marginMm;
    if (!(page.widthMm > 0.0) || !(page.heightMm > 0.0)) {
        // 幅か高さが0の紙は作らない。線1本だけのときにここへ来る。
        return Out::Failure(MakeError("EXP-P001", "ページの大きさが正しくありません。",
            "選ばれたワイヤーに広がりがありません。余白を足してください。"));
    }
    // 紙の左下から余白ぶんだけ空ける。実寸は変えない。動かすだけである。
    const Vector3 delta{request.marginMm - extent.minX, request.marginMm - extent.minY,
        0.0};
    page.curves.reserve(request.segments.size());
    for (const CurveSegment& segment : request.segments) {
        page.curves.push_back(exporters::PatternCurve{exporters::PatternLine::Outline,
            geometry::TranslateCurve(segment, delta), false, std::string()});
    }
    return Out::Success(std::move(page));
}

Result<std::string> MakeWireContent(const WirePatternRequest& request, ExportFormat format,
    double toleranceMm)
{
    using Out = Result<std::string>;
    if (!ExportFormatAllowed(ExportTarget::SelectedWires, format)) {
        return Out::Failure(MakeError("EXP-017", "その形式では出せません。",
            std::string(ExportTargetNameJa(ExportTarget::SelectedWires)) + " を "
                + std::string(ExportFormatNameJa(format)) + " で出すことはできません。"));
    }
    const auto page = BuildWirePatternPage(request, toleranceMm);
    if (!page.HasValue()) {
        return Out::Failure(page.Diagnostics());
    }
    switch (format) {
    case ExportFormat::Svg:
        return exporters::WritePatternSvg(page.Value(), request.title);
    case ExportFormat::Dxf:
        return exporters::WritePatternDxf(page.Value());
    case ExportFormat::Pdf: {
        exporters::PdfMetadata metadata;
        metadata.title = request.title;
        return exporters::WritePatternPdf({page.Value()}, metadata);
    }
    case ExportFormat::Stl:
    case ExportFormat::Step:
    case ExportFormat::Kcd2:
        break;
    }
    return Out::Failure(MakeError("EXP-017", "その形式では出せません。",
        "ワイヤーからは立体を作れません。"));
}

Result<std::string> MakeProjectContent(const io::DocumentFile& file)
{
    return io::SaveDocument(file);
}

} // namespace kachakacha::v2::app
