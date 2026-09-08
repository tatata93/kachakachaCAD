// 型紙のPDF。印刷して切るものなので、原寸で出ることが最優先。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/exporters/PdfWriter.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::exporters::PatternCurve;
using kachakacha::v2::exporters::PatternLine;
using kachakacha::v2::exporters::PatternPage;
using kachakacha::v2::exporters::PdfMetadata;
using kachakacha::v2::exporters::WritePatternPdf;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kPi = 3.14159265358979323846;

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

[[nodiscard]] std::size_t CountOf(const std::string& text, const std::string& needle)
{
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

[[nodiscard]] PatternPage SamplePage()
{
    PatternPage page;
    page.widthMm = 210.0;
    page.heightMm = 297.0;
    page.curves.push_back(
        PatternCurve{PatternLine::Outline, Line({10, 10, 0}, {90, 10, 0}), false, "1"});
    page.curves.push_back(PatternCurve{PatternLine::Outline,
        CurveSegment::MakeCircularArc({90, 30, 0}, {0, 0, 1}, {0, -1, 0}, 20.0, 0.0,
            kPi / 2.0)
            .Value(),
        false, "1"});
    page.curves.push_back(PatternCurve{PatternLine::Opening,
        CurveSegment::MakeCircle({50, 45, 0}, {0, 0, 1}, {1, 0, 0}, 8.0).Value(), false,
        "窓1"});
    page.curves.push_back(
        PatternCurve{PatternLine::Fold, Line({10, 40, 0}, {90, 40, 0}), true, "F1"});
    page.curves.push_back(
        PatternCurve{PatternLine::Cut, Line({30, 10, 0}, {30, 25, 0}), false, "C1"});
    return page;
}

} // namespace

KACHA_V2_TEST(pdf, PDFとして読める形になっている)
{
    const auto pdf = WritePatternPdf({SamplePage()}, {"テスト", 87.0});
    Require(pdf.HasValue(), "書き出せること");
    Require(pdf.Value().compare(0, 8, "%PDF-1.4") == 0, "先頭が %PDF");
    Require(pdf.Value().find("%%EOF") != std::string::npos, "末尾に %%EOF");
    Require(pdf.Value().find("/Type /Catalog") != std::string::npos, "カタログ");
    Require(pdf.Value().find("/Type /Pages") != std::string::npos, "ページの親");
    Require(pdf.Value().find("/Type /Page ") != std::string::npos, "ページ");
    Require(pdf.Value().find("xref") != std::string::npos, "相互参照表");
    Require(pdf.Value().find("startxref") != std::string::npos, "開始位置");
}

KACHA_V2_TEST(pdf, 原寸で出る)
{
    // A4 は 210 × 297 mm = 595.276 × 841.89 pt。
    const auto pdf = WritePatternPdf({SamplePage()}, {"テスト", 0.0});
    Require(pdf.HasValue(), "書き出せること");
    const std::size_t position = pdf.Value().find("/MediaBox [0 0 ");
    Require(position != std::string::npos, "MediaBox があること");
    const std::string box = pdf.Value().substr(position, 40);
    Require(box.find("595.276") != std::string::npos, "幅が A4 (" + box + ")");
    Require(box.find("841.89") != std::string::npos, "高さが A4 (" + box + ")");
}

KACHA_V2_TEST(pdf, 拡大縮小を掛けない)
{
    // 内容に cm(座標変換)が入っていないこと。入っていると寸法が狂う。
    const auto pdf = WritePatternPdf({SamplePage()}, {"テスト", 0.0});
    Require(pdf.HasValue(), "書き出せること");
    Require(CountOf(pdf.Value(), " cm\n") == 0, "座標変換を使っていないこと");
}

KACHA_V2_TEST(pdf, 線の座標が正しい)
{
    PatternPage page;
    page.widthMm = 100.0;
    page.heightMm = 100.0;
    page.curves.push_back(
        PatternCurve{PatternLine::Outline, Line({0, 0, 0}, {25.4, 0, 0}), false, ""});
    const auto pdf = WritePatternPdf({page}, {"テスト", 0.0});
    Require(pdf.HasValue(), "書き出せること");
    // 25.4mm = 1インチ = 72pt。
    Require(pdf.Value().find("0 0 m") != std::string::npos, "始点");
    Require(pdf.Value().find("72 0 l") != std::string::npos, "終点が 72pt");
}

KACHA_V2_TEST(pdf, 円弧がBezierで出る)
{
    const auto pdf = WritePatternPdf({SamplePage()}, {"テスト", 0.0});
    Require(pdf.HasValue(), "書き出せること");
    // c(3次Bezier)が使われていること。折れ線(l だけ)ではない。
    Require(CountOf(pdf.Value(), " c\n") >= 3, "Bezierがあること");
}

KACHA_V2_TEST(pdf, 円弧の近似が細かい)
{
    // 90度ごとに区切るので、半径20mmの90度円弧なら1区間。
    // 実際に通る点との差が、線の太さ(0.2mm)よりずっと小さいこと。
    const double radius = 20.0;
    const auto arc = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, radius,
        0.0, kPi / 2.0)
                         .Value();
    // Bezier の中点と、円弧の中点のずれ。定石の近似では r * 2.7e-4 程度。
    const Vector3 p0 = arc.Evaluate(0.0);
    const Vector3 p3 = arc.Evaluate(1.0);
    const double handle = 4.0 / 3.0 * std::tan(kPi / 8.0) * radius;
    const Vector3 t0 = kachakacha::v2::geometry::Normalized(arc.FirstDerivative(0.0));
    const Vector3 t1 = kachakacha::v2::geometry::Normalized(arc.FirstDerivative(1.0));
    const Vector3 p1 = p0 + t0 * handle;
    const Vector3 p2 = p3 - t1 * handle;
    // Bezier の t=0.5 の点。
    const Vector3 middle = (p0 + p1 * 3.0 + p2 * 3.0 + p3) * 0.125;
    const double error = std::abs(middle.Length() - radius);
    Require(error < 0.01, "ずれが 0.01mm 未満 (" + std::to_string(error) + ")");
}

KACHA_V2_TEST(pdf, 山折りと谷折りを描き分ける)
{
    PatternPage page;
    page.widthMm = 100.0;
    page.heightMm = 100.0;
    page.curves.push_back(
        PatternCurve{PatternLine::Fold, Line({0, 10, 0}, {50, 10, 0}), true, ""});
    page.curves.push_back(
        PatternCurve{PatternLine::Fold, Line({0, 20, 0}, {50, 20, 0}), false, ""});
    const auto pdf = WritePatternPdf({page}, {"テスト", 0.0});
    Require(pdf.HasValue(), "書き出せること");
    Require(pdf.Value().find("0.82 0.13 0.13 RG") != std::string::npos, "山折りの色");
    Require(pdf.Value().find("0.13 0.31 0.82 RG") != std::string::npos, "谷折りの色");
    Require(CountOf(pdf.Value(), "] 0 d\n") >= 2, "破線の指定");
}

KACHA_V2_TEST(pdf, 複数ページを1つにまとめられる)
{
    const auto pdf = WritePatternPdf({SamplePage(), SamplePage(), SamplePage()},
        {"テスト", 0.0});
    Require(pdf.HasValue(), "書き出せること");
    RequireCount(CountOf(pdf.Value(), "/Type /Page "), 3, "ページの数");
    Require(pdf.Value().find("/Count 3") != std::string::npos, "ページ数の宣言");
}

KACHA_V2_TEST(pdf, 縮尺を注記に入れる)
{
    const auto with = WritePatternPdf({SamplePage()}, {"テスト", 87.0});
    Require(with.HasValue(), "書き出せること");
    Require(with.Value().find("scale 1/87") != std::string::npos, "縮尺");
    Require(with.Value().find("actual size") != std::string::npos, "原寸だと書くこと");

    const auto without = WritePatternPdf({SamplePage()}, {"テスト", 0.0});
    Require(without.Value().find("/Subject") == std::string::npos,
        "縮尺が無ければ注記しないこと");
}

KACHA_V2_TEST(pdf, 相互参照表の位置が合っている)
{
    const auto pdf = WritePatternPdf({SamplePage()}, {"テスト", 0.0});
    Require(pdf.HasValue(), "書き出せること");
    const std::size_t marker = pdf.Value().rfind("startxref\n");
    Require(marker != std::string::npos, "startxref があること");
    const std::size_t valueStart = marker + 10;
    const std::size_t valueEnd = pdf.Value().find('\n', valueStart);
    const std::size_t offset =
        std::stoul(pdf.Value().substr(valueStart, valueEnd - valueStart));
    Require(offset < pdf.Value().size(), "位置が中にあること");
    Require(pdf.Value().compare(offset, 4, "xref") == 0,
        "その位置に xref があること");
}

KACHA_V2_TEST(pdf, オブジェクトの位置が合っている)
{
    const auto pdf = WritePatternPdf({SamplePage()}, {"テスト", 0.0});
    Require(pdf.HasValue(), "書き出せること");
    // xref の各行が指す位置に、本当にオブジェクトがあること。
    const std::size_t xrefStart = pdf.Value().rfind("xref\n0 ");
    Require(xrefStart != std::string::npos, "xref があること");
    // "xref\n" の次の行が "0 N"、その次から各項目が20バイトずつ並ぶ。
    std::size_t line = pdf.Value().find('\n', pdf.Value().find('\n', xrefStart) + 1) + 1;
    line += 20;   // 0番目(free)を飛ばす
    for (int index = 1; index <= 4; ++index) {
        const std::string entry = pdf.Value().substr(line, 10);
        const std::size_t offset = std::stoul(entry);
        Require(offset < pdf.Value().size(),
            std::to_string(index) + " 番目の位置が中にあること");
        const std::string expected = std::to_string(index) + " 0 obj";
        Require(pdf.Value().compare(offset, expected.size(), expected) == 0,
            std::to_string(index) + " 番目のオブジェクトがあること");
        line += 20;
    }
}

KACHA_V2_TEST(pdf, 同じ入力からは同じバイト列が出る)
{
    const PatternPage page = SamplePage();
    const auto first = WritePatternPdf({page}, {"テスト", 87.0});
    for (int repeat = 0; repeat < 5; ++repeat) {
        Require(WritePatternPdf({page}, {"テスト", 87.0}).Value() == first.Value(),
            "毎回同じであること");
    }
    // 日付が入っていないこと。
    Require(first.Value().find("/CreationDate") == std::string::npos, "日付を入れない");
}

KACHA_V2_TEST(pdf, 括弧を含む題名でも壊れない)
{
    const auto pdf = WritePatternPdf({SamplePage()}, {"ER2 (front) \\ test", 0.0});
    Require(pdf.HasValue(), "書き出せること");
    Require(pdf.Value().find("\\(front\\)") != std::string::npos, "括弧を逃がすこと");
    Require(pdf.Value().find("\\\\") != std::string::npos, "逆斜線を逃がすこと");
}

KACHA_V2_TEST(pdf, おかしな入力を断る)
{
    Require(!WritePatternPdf({}, {"テスト", 0.0}).HasValue(), "ページが無い");

    PatternPage bad;
    bad.widthMm = 0.0;
    bad.heightMm = 100.0;
    Require(!WritePatternPdf({bad}, {"テスト", 0.0}).HasValue(), "幅0");

    PatternPage offPlane;
    offPlane.widthMm = 100.0;
    offPlane.heightMm = 100.0;
    offPlane.curves.push_back(
        PatternCurve{PatternLine::Outline, Line({0, 0, 0}, {10, 0, 5}), false, ""});
    const auto result = WritePatternPdf({offPlane}, {"テスト", 0.0});
    Require(!result.HasValue(), "面から外れた線");
    RequireEqual(result.Diagnostics().front().code, std::string("EXP-D002"), "診断コード");
}

KACHA_V2_TEST(pdf, 空のページでも出せる)
{
    PatternPage empty;
    empty.widthMm = 210.0;
    empty.heightMm = 297.0;
    const auto pdf = WritePatternPdf({empty}, {"空", 0.0});
    Require(pdf.HasValue(), "書き出せること");
    Require(pdf.Value().find("/Type /Page ") != std::string::npos, "ページがあること");
}

KACHA_V2_TEST_MAIN("pdf_tests")
