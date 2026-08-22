#include "PlatePdfExport.h"
#include "kachakacha/io/ProjectScript.h"

#include <QFile>
#include <QGuiApplication>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

using kachakacha::io::PlateFlatPattern;
using kachakacha::io::PlateFlatPatternPath;
using kachakacha::io::BuildPlateFlatPattern;
using kachakacha::io::LoadProjectScript;
using kachakacha::qtio::CalculatePlatePdfLayout;
using kachakacha::qtio::PlatePdfOrientation;
using kachakacha::qtio::PlatePdfOptions;
using kachakacha::qtio::WritePlateFlatPatternPdf;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PlateFlatPattern RectanglePattern(double width, double height)
{
    PlateFlatPattern pattern;
    pattern.plateName = "pdf_test_plate";
    pattern.outerBoundary = {
        "pdf_test_plate",
        {
            {0.0, 0.0},
            {width, 0.0},
            {width, height},
            {0.0, height},
            {0.0, 0.0},
        },
    };
    pattern.openings.push_back(PlateFlatPatternPath{
        "opening",
        {
            {10.0, 10.0},
            {20.0, 10.0},
            {20.0, 20.0},
            {10.0, 20.0},
            {10.0, 10.0},
        },
    });
    return pattern;
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    try {
        Require(argc >= 2, "PDF output path argument is required");

        const PlateFlatPattern small = RectanglePattern(100.0, 50.0);
        const auto smallLayout = CalculatePlatePdfLayout(small);
        Require(smallLayout.PageCount() == 1, "small plate fits on one A4 page");

        const PlateFlatPattern large = RectanglePattern(400.0, 300.0);
        const auto automaticLayout = CalculatePlatePdfLayout(large);
        Require(automaticLayout.orientation == QPageLayout::Landscape, "automatic layout selects fewer landscape pages");
        Require(automaticLayout.columns == 2 && automaticLayout.rows == 2,
            "large plate is tiled onto four A4 pages");

        PlatePdfOptions portraitOptions;
        portraitOptions.orientation = PlatePdfOrientation::Portrait;
        const auto portraitLayout = CalculatePlatePdfLayout(large, portraitOptions);
        Require(portraitLayout.columns == 3 && portraitLayout.rows == 2,
            "forced portrait layout uses six A4 pages");

        const QString outputPath = QString::fromLocal8Bit(argv[1]);
        WritePlateFlatPatternPdf(outputPath, large);
        QFile pdf(outputPath);
        Require(pdf.open(QIODevice::ReadOnly), "generated PDF opens");
        const QByteArray contents = pdf.readAll();
        Require(contents.startsWith("%PDF-"), "generated file has a PDF header");
        Require(contents.size() > 2000, "generated PDF contains vector drawing data");
        Require(contents.count("/Type /Page") >= automaticLayout.PageCount(),
            "generated PDF contains every tiled page");
        pdf.close();

        if (argc >= 3) {
            std::ifstream sampleInput(argv[2]);
            Require(static_cast<bool>(sampleInput), "curved plate sample opens");
            const auto sample = LoadProjectScript(sampleInput, argv[2]);
            Require(!sample.Plates().empty(), "curved plate sample contains a plate");
            const PlateFlatPattern samplePattern = BuildPlateFlatPattern(sample, sample.Plates().front());
            Require(samplePattern.openings.size() == 1, "sample PDF keeps its light opening");
            WritePlateFlatPatternPdf(outputPath, samplePattern);
            QFile samplePdf(outputPath);
            Require(samplePdf.open(QIODevice::ReadOnly), "sample PDF opens after writing");
            Require(samplePdf.read(5) == QByteArray("%PDF-"), "sample output remains a valid PDF");
        }
    } catch (const std::exception& error) {
        std::cerr << "plate_pdf_tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "plate PDF tests passed\n";
    return EXIT_SUCCESS;
}
