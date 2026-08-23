#include "kachakacha/model/Body.h"
#include "kachakacha/model/Project.h"
#include "kachakacha/io/ProjectScript.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using kachakacha::geometry::AlmostEqual;
using kachakacha::model::Body;
using kachakacha::model::JigSide;
using kachakacha::model::PlateSurfaceRange;
using kachakacha::model::Project;
using kachakacha::model::Surface;
using kachakacha::model::Wire;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main(int argc, char** argv)
{
    const Surface surface = Surface::Ruled(
        Wire::Line({0.0, 0.0, 0.0}, {0.0, 10.0, 0.0}),
        Wire::Line({20.0, 0.0, 2.0}, {20.0, 10.0, 4.0}));
    const Body positive = Body::SurfaceJig(
        surface, PlateSurfaceRange{}, JigSide::Positive, 0.25, 3.0);
    Require(AlmostEqual(positive.ContactOffset(), 0.25), "positive jig contact offset");
    Require(AlmostEqual(positive.BackingOffset(), 3.25), "positive jig backing offset");
    Require(AlmostEqual(
        (positive.Evaluate(0.5, 0.5, 1.0) - positive.Evaluate(0.5, 0.5, 0.0)).Length(),
        3.0, 1.0e-7), "jig keeps requested wall thickness");

    const Body negative = Body::SurfaceJig(
        surface,
        PlateSurfaceRange{0.2, 0.8, 0.1, 0.9},
        JigSide::Negative,
        0.4,
        2.0);
    Require(AlmostEqual(negative.ContactOffset(), -0.4), "negative jig contact offset");
    Require(AlmostEqual(negative.BackingOffset(), -2.4), "negative jig backing offset");
    Require(AlmostEqual(negative.SourceU(0.0), 0.2), "jig range minimum U");
    Require(AlmostEqual(negative.SourceV(1.0), 0.9), "jig range maximum V");

    const auto accepted = positive.AnalyzePrintability(1.2);
    Require(accepted.meetsMinimumWall, "jig meets minimum wall");
    Require(AlmostEqual(accepted.minimumWallMillimeters, 3.0), "jig reports minimum wall");
    const auto rejected = positive.AnalyzePrintability(3.5);
    Require(!rejected.meetsMinimumWall, "jig rejects insufficient wall");

    Project project;
    project.AddWire("section_a", Wire::Line({0.0, 0.0, 0.0}, {0.0, 10.0, 0.0}));
    project.AddWire("section_b", Wire::Line({20.0, 0.0, 2.0}, {20.0, 10.0, 4.0}));
    project.AddRuledSurface("nose", "section_a", "section_b");
    project.AddSurfaceJig(
        "nose_jig", "nose", PlateSurfaceRange{}, JigSide::Negative, 0.3, 4.0);
    Require(project.Bodies().size() == 1, "project adds surface jig");
    const auto beforeUpdate = project.Bodies().front().body.Evaluate(1.0, 1.0, 0.0);
    project.UpdateWire("section_b", Wire::Line({24.0, 0.0, 3.0}, {24.0, 10.0, 6.0}));
    const auto afterUpdate = project.Bodies().front().body.Evaluate(1.0, 1.0, 0.0);
    Require(!AlmostEqual(beforeUpdate, afterUpdate, 1.0e-6), "jig follows source section edit");

    bool dependencyRejected = false;
    try {
        static_cast<void>(project.RemoveSurface("nose"));
    } catch (const std::invalid_argument&) {
        dependencyRejected = true;
    }
    Require(dependencyRejected, "project protects jig source surface");
    Require(project.RemoveBody("nose_jig"), "project removes jig");
    Require(project.RemoveSurface("nose"), "surface can be removed after jig");

    Project savedProject;
    savedProject.AddWire("a", Wire::Line({0.0, 0.0, 0.0}, {0.0, 5.0, 0.0}));
    savedProject.AddWire("b", Wire::Line({10.0, 0.0, 1.0}, {10.0, 5.0, 2.0}));
    savedProject.AddRuledSurface("skin", "a", "b");
    savedProject.AddSurfaceJig(
        "forming_jig", "skin", PlateSurfaceRange{0.1, 0.9, 0.2, 0.8},
        JigSide::Positive, 0.2, 3.5);
    savedProject.SetBodyVisible("forming_jig", false);
    std::ostringstream saved;
    kachakacha::io::WriteProjectScript(saved, savedProject);
    std::istringstream input(saved.str());
    const Project loaded = kachakacha::io::LoadProjectScript(input, "body roundtrip");
    Require(loaded.Bodies().size() == 1, "project script reloads jig");
    Require(!loaded.Bodies().front().visible, "project script reloads jig visibility");
    Require(AlmostEqual(
        loaded.Bodies().front().body.ClearanceMillimeters(), 0.2),
        "project script reloads jig clearance");

    if (argc >= 2) {
        try {
            std::ifstream acceptanceInput(argv[1]);
            Require(static_cast<bool>(acceptanceInput), "acceptance project opens");
            const Project acceptance = kachakacha::io::LoadProjectScript(
                acceptanceInput, "railway nose acceptance model");
            Require(acceptance.Surfaces().size() == 1, "acceptance project has the nose surface");
            Require(acceptance.Plates().size() == 2, "acceptance project has two user panels");
            Require(acceptance.Bodies().size() == 1, "acceptance project has the forming jig");
            Require(acceptance.Bodies().front().sourceSurfaceName == "nose_skin",
                "acceptance jig depends on the nose surface");
            Require(acceptance.Bodies().front().body.AnalyzePrintability(1.2).meetsMinimumWall,
                "acceptance jig meets minimum wall");
        } catch (const std::exception& error) {
            std::cerr << "FAILED: acceptance project: " << error.what() << '\n';
            return EXIT_FAILURE;
        }
    }

    std::cout << "body tests passed\n";
    return EXIT_SUCCESS;
}
