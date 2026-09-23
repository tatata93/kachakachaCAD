#include "kachakacha/app/GptSurface.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctGptSurface.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/io/DocumentFile.h"
#include <limits>
#include <cmath>

using namespace kachakacha::v2;
using test::Require;
using test::RequireNear;
namespace {
geometry::CurveSegment Line(geometry::Vector3 a, geometry::Vector3 b)
{
    return geometry::CurveSegment::MakeLine(a, b).Value();
}
app::GptSurfaceRequest Rectangle()
{
    app::GptSurfaceRequest request;
    // Deliberately unordered and partly reversed.
    request.curves = {{{Line({40,20,0}, {0,20,0}), Line({0,0,0}, {40,0,0}),
        Line({0,0,0}, {0,20,0}), Line({40,20,0}, {40,0,0})}, "outer", true, app::kGptBoundaryRole}};
    return request;
}
app::GptSurfaceCurve Section(double z)
{
    return {{Line({0,0,z}, {20,10,z}), Line({20,10,z}, {40,0,z})},
        "section", false, app::kGptSectionRole};
}
}

KACHA_V2_TEST(gpt_surface, validates_every_input)
{
    auto request = Rectangle();
    const auto ordered = app::ValidateGptSurface(request, {});
    Require(ordered.HasValue(), "scrambled boundary connects");
    Require(ordered.Value().curves.front().closed, "boundary is closed");
    request.curves.front().segments.pop_back();
    Require(!app::ValidateGptSurface(request, {}).HasValue(), "gap is rejected");
    request = Rectangle();
    request.curves.push_back(Section(20));
    Require(!app::ValidateGptSurface(request, {}).HasValue(), "incompatible role is not ignored");
    request = Rectangle();
    request.curves.front().segments.push_back(Line({100,0,0}, {120,0,0}));
    Require(!app::ValidateGptSurface(request, {}).HasValue(), "disconnected extra line rejected");
    request = Rectangle();
    request.maximumDeviationMm = std::numeric_limits<double>::quiet_NaN();
    Require(!app::ValidateGptSurface(request, {}).HasValue(), "nonfinite tolerance rejected");
}

KACHA_V2_TEST(gpt_surface, sections_need_consistent_open_or_closed)
{
    app::GptSurfaceRequest request;
    request.loft = true;
    request.curves = {Section(0)};
    Require(!app::ValidateGptSurface(request, {}).HasValue(), "one section rejected");
    request.curves.push_back(Section(20));
    Require(app::ValidateGptSurface(request, {}).HasValue(), "two composite sections accepted");
    auto closed = Rectangle().curves.front();
    closed.role = app::kGptSectionRole;
    request.curves.push_back(closed);
    Require(!app::ValidateGptSurface(request, {}).HasValue(), "mixed open and closed rejected");
}

KACHA_V2_TEST(gpt_surface, saved_references_hidden_sources_and_duplicate_rejection)
{
    base::DeterministicIdGenerator ids{300};
    document::Document document(ids.NextTyped<base::IdKind::Document>());
    domain::Feature wireFeature;
    wireFeature.id = ids.NextTyped<base::IdKind::Feature>();
    wireFeature.type = domain::FeatureType::CreateWire;
    domain::CreateWireDefinition wireDefinition;
    wireDefinition.segments = Rectangle().curves.front().segments;
    for (std::size_t i = 0; i < wireDefinition.segments.size(); ++i) {
        wireDefinition.segmentIds.push_back(ids.NextTyped<base::IdKind::Segment>());
    }
    wireFeature.definition = wireDefinition;
    domain::Entity wire;
    wire.id = ids.NextTyped<base::IdKind::Entity>();
    wire.kind = domain::EntityKind::Wire;
    wire.createdBy = wireFeature.id;
    wire.visibility = domain::Visibility::Hidden;
    wireFeature.outputs.push_back({"wire", wire.id, wire.kind});
    Require(document.Run(document::AddFeatureCommand(wireFeature, {wire}, "wire")).committed, "wire added");
    domain::CreateGuideSurfaceDefinition definition;
    definition.gptBuilder = true;
    definition.gptToleranceMm = 0.002;
    definition.method = app::kGptBoundaryMethod;
    definition.chains = {{{{wire.id}}, {false}}};
    definition.roles = {app::kGptBoundaryRole};
    Require(app::ResolveGptSurface(document, {}, definition).HasValue(), "hidden stored wire is resolved");
    auto duplicate = definition;
    duplicate.chains.push_back(definition.chains.front());
    duplicate.roles.push_back(app::kGptInteriorRole);
    Require(!app::ResolveGptSurface(document, {}, duplicate).HasValue(), "duplicate source rejected across roles");
    domain::Feature surfaceFeature;
    surfaceFeature.id = ids.NextTyped<base::IdKind::Feature>();
    surfaceFeature.type = domain::FeatureType::CreateGuideSurface;
    surfaceFeature.definition = definition;
    surfaceFeature.inputEntityIds = {wire.id};
    domain::Entity surface;
    surface.id = ids.NextTyped<base::IdKind::Entity>();
    surface.kind = domain::EntityKind::GuideSurface;
    surface.createdBy = surfaceFeature.id;
    surfaceFeature.outputs.push_back({"surface", surface.id, surface.kind});
    Require(document.Run(document::AddFeatureCommand(surfaceFeature, {surface}, "surface")).committed, "surface added");
    io::DocumentFile file;
    file.snapshot = document.Snapshot();
    const auto saved = io::SaveDocument(file);
    Require(saved.HasValue(), saved.FirstSummaryJa());
    const auto loaded = io::LoadDocument(saved.Value());
    Require(loaded.HasValue(), loaded.FirstSummaryJa());
    const auto& restored = std::get<domain::CreateGuideSurfaceDefinition>(loaded.Value().snapshot.features.back().definition);
    Require(restored.gptBuilder, "independent builder flag survives save/reopen");
    RequireNear(restored.gptToleranceMm, .002, 1e-12, "tolerance survives save/reopen");
    Require(restored.chains.front().segments.front().entityId == wire.id, "UUID source preserved");
}

#ifdef KACHACAD_V2_WITH_OCCT
KACHA_V2_TEST(gpt_surface, planar_boundary_area_and_all_constraints)
{
    auto request = Rectangle();
    request.curves.push_back({{Line({0,10,0}, {40,10,0})}, "interior", false, app::kGptInteriorRole});
    const auto made = kernel::BuildGptSurface(request, {});
    Require(made.HasValue(), made.FirstSummaryJa());
    RequireNear(made.Value().areaMm2, 800.0, 1e-5, "rectangle area");
    Require(made.Value().maximumDeviationMm < 1e-6, "every input is on the face");
    kernel::ReleaseShape(made.Value().handle);
    request.curves.back().segments = {Line({50,10,0}, {60,10,0})};
    const auto outside = kernel::BuildGptSurface(request, {});
    Require(!outside.HasValue(), "an outside interior line must not be silently ignored");
}

KACHA_V2_TEST(gpt_surface, nonplanar_interior_constraint)
{
    auto request = Rectangle();
    request.curves.push_back({{geometry::CurveSegment::MakeCubicBezier(
        {{0,10,0}, {12,10,5}, {28,10,5}, {40,10,0}}).Value()}, "raised interior", false, app::kGptInteriorRole});
    const auto made = kernel::BuildGptSurface(request, {});
    Require(made.HasValue(), made.FirstSummaryJa());
    Require(made.Value().maximumDeviationMm <= request.maximumDeviationMm, "raised interior honored");
    Require(made.Value().areaMm2 > 800.0, "surface is not the flat boundary plane");
    kernel::ReleaseShape(made.Value().handle);
}

KACHA_V2_TEST(gpt_surface, composite_sections_create_real_surface)
{
    app::GptSurfaceRequest request;
    request.loft = true;
    request.curves = {Section(0), Section(15), Section(30)};
    const auto made = kernel::BuildGptSurface(request, {});
    Require(made.HasValue(), made.FirstSummaryJa());
    RequireNear(made.Value().areaMm2, 2.0 * std::sqrt(500.0) * 30.0, 0.01, "prismatic loft area");
    Require(made.Value().maximumDeviationMm <= request.maximumDeviationMm, "all sections measured");
    kernel::ReleaseShape(made.Value().handle);
}
#else
KACHA_V2_TEST(gpt_surface, missing_kernel_is_explicit)
{
    const auto made = kernel::BuildGptSurface(Rectangle(), {});
    Require(!made.HasValue(), "no fabricated success without OCCT");
    Require(made.FirstDiagnostic().code == "GPT-S006", "missing kernel diagnostic");
}
#endif

KACHA_V2_TEST_MAIN("gpt_surface_tests")
