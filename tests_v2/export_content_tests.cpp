// 書き出しの中身(ui-workflows §11、WP-11)。
#include "kachakacha/app/ExportContent.h"
#include "kachakacha/base/TestHarness.h"

#include <algorithm>
#include <string>

using kachakacha::v2::app::BuildWirePatternPage;
using kachakacha::v2::app::ExportFormat;
using kachakacha::v2::app::MakeProjectContent;
using kachakacha::v2::app::MakeWireContent;
using kachakacha::v2::app::WirePatternRequest;
using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kTolerance = 0.01;

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[nodiscard]] CurveSegment Line(Vector3 start, Vector3 end)
{
    const auto made = CurveSegment::MakeLine(start, end);
    Require(made.HasValue(), "線が作れる");
    return made.Value();
}

//! 100 x 50 の四角。左下は (200, 300)。原点から離れた場所に置く。
[[nodiscard]] WirePatternRequest Rectangle()
{
    WirePatternRequest request;
    request.title = "しかく";
    request.segments.push_back(Line({200, 300, 0}, {300, 300, 0}));
    request.segments.push_back(Line({300, 300, 0}, {300, 350, 0}));
    request.segments.push_back(Line({300, 350, 0}, {200, 350, 0}));
    request.segments.push_back(Line({200, 350, 0}, {200, 300, 0}));
    return request;
}

} // namespace

KACHA_V2_TEST(export_content, 紙の大きさは中身と余白から決まる)
{
    WirePatternRequest request = Rectangle();
    request.marginMm = 10.0;
    const auto page = BuildWirePatternPage(request, kTolerance);
    Require(page.HasValue(), "作れる");
    RequireNear(page.Value().widthMm, 120.0, 1.0e-9, "幅");
    RequireNear(page.Value().heightMm, 70.0, 1.0e-9, "高さ");
}

KACHA_V2_TEST(export_content, 原点から離れていても紙の左下へ寄せる)
{
    // 寄せないと、200mm 離れた場所にある形が紙からはみ出す。
    WirePatternRequest request = Rectangle();
    request.marginMm = 10.0;
    const auto page = BuildWirePatternPage(request, kTolerance);
    Require(page.HasValue(), "作れる");
    double minX = 1.0e18;
    double minY = 1.0e18;
    for (const auto& curve : page.Value().curves) {
        const Vector3 start = curve.segment.Evaluate(0.0);
        const Vector3 end = curve.segment.Evaluate(1.0);
        minX = std::min(minX, std::min(start.x, end.x));
        minY = std::min(minY, std::min(start.y, end.y));
    }
    RequireNear(minX, 10.0, 1.0e-9, "左の余白");
    RequireNear(minY, 10.0, 1.0e-9, "下の余白");
}

KACHA_V2_TEST(export_content, 実寸のまま出す)
{
    // 紙に入らないからといって縮めない。縮めたら型紙にならない。
    WirePatternRequest request = Rectangle();
    const auto page = BuildWirePatternPage(request, kTolerance);
    Require(page.HasValue(), "作れる");
    double longest = 0.0;
    for (const auto& curve : page.Value().curves) {
        longest = std::max(longest, curve.segment.TotalLength(kTolerance));
    }
    RequireNear(longest, 100.0, 1.0e-9, "いちばん長い辺は 100mm のまま");
}

KACHA_V2_TEST(export_content, 曲線は曲線のまま残る)
{
    WirePatternRequest request;
    const auto arc = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 20.0,
        0.0, 1.5707963267948966);
    Require(arc.HasValue(), "円弧が作れる");
    request.segments.push_back(arc.Value());
    request.segments.push_back(Line({0, 20, 0}, {0, 0, 0}));
    const auto page = BuildWirePatternPage(request, kTolerance);
    Require(page.HasValue(), "作れる");
    Require(page.Value().curves.front().segment.Kind()
            == kachakacha::v2::geometry::CurveKind::CircularArc,
        "円弧のまま");
}

KACHA_V2_TEST(export_content, ワイヤーが無ければ断る)
{
    const auto page = BuildWirePatternPage(WirePatternRequest{}, kTolerance);
    Require(!page.HasValue(), "断る");
    RequireEqual(std::string("EXP-015"), FirstCode(page.Diagnostics()), "コード");
}

KACHA_V2_TEST(export_content, 余白が負なら断る)
{
    WirePatternRequest request = Rectangle();
    request.marginMm = -1.0;
    const auto page = BuildWirePatternPage(request, kTolerance);
    Require(!page.HasValue(), "断る");
    RequireEqual(std::string("EXP-P001"), FirstCode(page.Diagnostics()), "コード");
}

KACHA_V2_TEST(export_content, 広がりのない形は紙にできない)
{
    // 縦線1本。余白を入れなければ幅0の紙になる。作らずに断る。
    WirePatternRequest request;
    request.marginMm = 0.0;
    request.segments.push_back(Line({5, 0, 0}, {5, 40, 0}));
    const auto page = BuildWirePatternPage(request, kTolerance);
    Require(!page.HasValue(), "断る");
    RequireEqual(std::string("EXP-P001"), FirstCode(page.Diagnostics()), "コード");
}

KACHA_V2_TEST(export_content, 余白を足せば線1本でも出せる)
{
    WirePatternRequest request;
    request.marginMm = 10.0;
    request.segments.push_back(Line({5, 0, 0}, {5, 40, 0}));
    const auto page = BuildWirePatternPage(request, kTolerance);
    Require(page.HasValue(), "作れる");
    RequireNear(page.Value().widthMm, 20.0, 1.0e-9, "余白だけの幅");
}

KACHA_V2_TEST(export_content, SVGが出せる)
{
    const auto content = MakeWireContent(Rectangle(), ExportFormat::Svg, kTolerance);
    Require(content.HasValue(), "作れる");
    Require(content.Value().find("<svg") != std::string::npos, "SVG である");
    Require(content.Value().find("mm") != std::string::npos, "単位が入る");
}

KACHA_V2_TEST(export_content, DXFが出せる)
{
    const auto content = MakeWireContent(Rectangle(), ExportFormat::Dxf, kTolerance);
    Require(content.HasValue(), "作れる");
    Require(content.Value().find("SECTION") != std::string::npos, "DXF である");
}

KACHA_V2_TEST(export_content, PDFが出せる)
{
    const auto content = MakeWireContent(Rectangle(), ExportFormat::Pdf, kTolerance);
    Require(content.HasValue(), "作れる");
    Require(content.Value().rfind("%PDF", 0) == 0, "PDF である");
}

KACHA_V2_TEST(export_content, 立体の形式はワイヤーからは出せない)
{
    for (ExportFormat format : {ExportFormat::Stl, ExportFormat::Step,
             ExportFormat::Kcd2}) {
        const auto content = MakeWireContent(Rectangle(), format, kTolerance);
        Require(!content.HasValue(), "断る");
        RequireEqual(std::string("EXP-017"), FirstCode(content.Diagnostics()), "コード");
    }
}

KACHA_V2_TEST(export_content, 同じ入力からは同じバイト列が出る)
{
    // 版管理に載せられるように、毎回同じものが出る。
    const auto first = MakeWireContent(Rectangle(), ExportFormat::Svg, kTolerance);
    const auto second = MakeWireContent(Rectangle(), ExportFormat::Svg, kTolerance);
    Require(first.HasValue() && second.HasValue(), "作れる");
    RequireEqual(first.Value(), second.Value(), "同じ");
}

KACHA_V2_TEST(export_content, 文書がkcd2になる)
{
    kachakacha::v2::io::DocumentFile file;
    file.metadata.title = "しけん";
    const auto content = MakeProjectContent(file);
    Require(content.HasValue(), "作れる");
    Require(!content.Value().empty(), "中身がある");
    // zip なので PK で始まる。
    Require(content.Value().rfind("PK", 0) == 0, "zip である");
}

KACHA_V2_TEST(export_content, 中身は空にならない)
{
    // 空を書くと0バイトのファイルが残る。RunExport が断るが、ここでも見る。
    for (ExportFormat format : {ExportFormat::Svg, ExportFormat::Dxf, ExportFormat::Pdf}) {
        const auto content = MakeWireContent(Rectangle(), format, kTolerance);
        Require(content.HasValue(), "作れる");
        Require(!content.Value().empty(), "空でない");
    }
}

namespace {
auto OwnedPages()
{
    using namespace kachakacha::v2;
    const auto id=*base::EntityId::Parse("b0000000-0000-4000-8000-000000000711");
    std::vector<exporters::PatternPage> pages(3);
    for(std::size_t i=0;i<pages.size();++i) {
        pages[i].sourceModelId=id;
        for(const auto layer:{exporters::PatternLine::Outline,exporters::PatternLine::Opening,
            exporters::PatternLine::Fold,exporters::PatternLine::Cut}) {
            exporters::PatternCurve curve{layer,Line({10.0+i*20,10,0},{15.0+i*20,15,0}),false,"same display label",i};
            pages[i].curves.push_back(curve);
        }
    }
    return pages;
}
}

KACHA_V2_TEST(export_content, selected_panels_keep_all_layers_and_original_page_coordinates)
{
    using namespace kachakacha::v2;
    const auto pages=OwnedPages();
    const auto selected=app::SelectPatternPanels(pages,pages.front().sourceModelId,3,{1},1,false);
    Require(selected.HasValue()&&selected.Value().size()==1,"current page is filtered");
    const auto& page=selected.Value().front();
    Require(page.curves.size()==4,"outline opening fold and cut remain together");
    for(const auto& curve:page.curves){Require(curve.ownerPanelIndex==1,"only chosen owner");RequireNear(curve.segment.StartPoint().x,30,1e-9,"placement unchanged");}
    Require(pages[0].curves.size()==4&&pages[2].curves.size()==4,"source pages unchanged");
    Require(!app::SelectPatternPanels(pages,pages[0].sourceModelId,3,{1},0,false).HasValue(),"current page without target refuses rather than switching pages");
}

KACHA_V2_TEST(export_content, selected_pdf_pages_keep_order_skip_empty_and_do_not_duplicate)
{
    using namespace kachakacha::v2;const auto pages=OwnedPages();
    const auto selected=app::SelectPatternPanels(pages,pages[0].sourceModelId,3,{2,0,2},0,true);
    Require(selected.HasValue()&&selected.Value().size()==2,"only occupied PDF pages");
    Require(selected.Value()[0].curves.front().ownerPanelIndex==0&&selected.Value()[1].curves.front().ownerPanelIndex==2,"source page order, not selection order");
    const auto all=app::SelectPatternPanels(pages,pages[0].sourceModelId,3,{},0,true);
    Require(all.HasValue()&&all.Value().size()==3,"explicit all keeps every page");
}

KACHA_V2_TEST(export_content, selected_patterns_reject_stale_or_unowned_pages_and_bad_indices)
{
    using namespace kachakacha::v2;auto pages=OwnedPages();const auto model=pages[0].sourceModelId;
    const auto other=*base::EntityId::Parse("b0000000-0000-4000-8000-000000000712");
    Require(!app::SelectPatternPanels(pages,other,3,{0},0,true).HasValue(),"another model cannot reuse indices");
    Require(!app::SelectPatternPanels(pages,model,3,{3},0,true).HasValue(),"out of range rejected");
    Require(!app::SelectPatternPanels(pages,model,3,{0},9,false).HasValue(),"bad page rejected");
    pages[1].curves.front().ownerPanelIndex.reset();
    Require(!app::SelectPatternPanels(pages,model,3,{0},0,true).HasValue(),"missing ownership never guessed from label");
    pages=OwnedPages();pages[1].curves.front().ownerPanelIndex=5;
    Require(!app::SelectPatternPanels(pages,model,3,{0},0,true).HasValue(),"old subdivision indices rejected");
}

KACHA_V2_TEST_MAIN("export_content_tests")
