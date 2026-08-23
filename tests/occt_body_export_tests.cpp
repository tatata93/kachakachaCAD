#include "kachakacha/occt/BodyExport.h"
#include "kachakacha/io/ProjectScript.h"
#include "kachakacha/model/Body.h"
#include "kachakacha/model/Surface.h"
#include "kachakacha/model/Wire.h"

#include <BRepCheck_Analyzer.hxx>
#include <STEPControl_Reader.hxx>
#include <StlAPI_Reader.hxx>
#include <TopoDS_Shape.hxx>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using kachakacha::geometry::Vector3;
using kachakacha::model::Body;
using kachakacha::model::JigSide;
using kachakacha::model::Surface;
using kachakacha::model::Wire;

namespace {

void VerifyExports(
    const std::filesystem::path& outputDirectory,
    const std::string& prefix,
    const Body& body)
{
    const auto analysis = kachakacha::occt::AnalyzeBodyShape(body, 1.2);
    if (!analysis.validBRep || !analysis.closedSolid || !analysis.meetsMinimumWall
        || analysis.volumeCubicMillimeters <= 1.0) {
        throw std::runtime_error(
            prefix + " OCCT jig analysis did not produce a printable solid: " + analysis.message);
    }

    const std::filesystem::path stlPath = outputDirectory / (prefix + ".stl");
    const std::filesystem::path stepPath = outputDirectory / (prefix + ".step");
    kachakacha::occt::WriteBodyStl(stlPath, body);
    kachakacha::occt::WriteBodyStep(stepPath, body);
    if (!std::filesystem::exists(stlPath) || std::filesystem::file_size(stlPath) < 1024
        || !std::filesystem::exists(stepPath) || std::filesystem::file_size(stepPath) < 1024) {
        throw std::runtime_error(prefix + " OCCT body exports are unexpectedly small.");
    }

    TopoDS_Shape stlShape;
    StlAPI_Reader stlReader;
    stlReader.Read(stlShape, stlPath.string().c_str());
    if (stlShape.IsNull()) {
        throw std::runtime_error(prefix + " exported STL cannot be read back.");
    }
    STEPControl_Reader stepReader;
    if (stepReader.ReadFile(stepPath.string().c_str()) != IFSelect_RetDone
        || stepReader.TransferRoots() <= 0) {
        throw std::runtime_error(prefix + " exported STEP cannot be read back.");
    }
    const TopoDS_Shape stepShape = stepReader.OneShape();
    if (stepShape.IsNull() || !BRepCheck_Analyzer(stepShape, true).IsValid()) {
        throw std::runtime_error(prefix + " exported STEP did not preserve valid boundary geometry.");
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "expected output directory and acceptance project\n";
        return 2;
    }
    try {
        const Surface surface = Surface::Loft({
            Wire::CubicBSpline({
                Vector3{-16.0, 0.0, -7.0}, Vector3{-12.0, 0.0, 4.0},
                Vector3{0.0, 0.0, 11.0}, Vector3{12.0, 0.0, 4.0}, Vector3{16.0, 0.0, -7.0}}),
            Wire::CubicBSpline({
                Vector3{-15.0, 8.0, -6.0}, Vector3{-11.0, 8.0, 5.0},
                Vector3{0.0, 8.0, 12.5}, Vector3{11.0, 8.0, 5.0}, Vector3{15.0, 8.0, -6.0}}),
            Wire::CubicBSpline({
                Vector3{-13.0, 18.0, -4.0}, Vector3{-9.0, 18.0, 5.0},
                Vector3{0.0, 18.0, 10.0}, Vector3{9.0, 18.0, 5.0}, Vector3{13.0, 18.0, -4.0}}),
        });
        const Body body = Body::SurfaceJig(surface, {}, JigSide::Negative, 0.15, 3.0);
        const std::filesystem::path outputDirectory = argv[1];
        std::filesystem::create_directories(outputDirectory);
        VerifyExports(outputDirectory, "jig-test", body);

        const Body positiveBody = Body::SurfaceJig(
            surface, {}, JigSide::Positive, 0.2, 2.5);
        const auto positiveAnalysis = kachakacha::occt::AnalyzeBodyShape(positiveBody, 1.2);
        if (!positiveAnalysis.validBRep || !positiveAnalysis.closedSolid
            || !positiveAnalysis.meetsMinimumWall || positiveAnalysis.volumeCubicMillimeters <= 1.0) {
            throw std::runtime_error(
                "Positive-side jig did not produce a printable solid: " + positiveAnalysis.message);
        }

        std::ifstream acceptanceInput(argv[2]);
        if (!acceptanceInput) {
            throw std::runtime_error("Could not open the railway nose acceptance project.");
        }
        const auto acceptance = kachakacha::io::LoadProjectScript(
            acceptanceInput, "railway nose acceptance project");
        if (acceptance.Bodies().size() != 1) {
            throw std::runtime_error("Acceptance project must contain one forming jig.");
        }
        VerifyExports(outputDirectory, "railway-nose-forming-jig", acceptance.Bodies().front().body);

        std::cout << "occt body export tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
