#pragma once
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include <cmath>

// A valid smooth reference surface for cancellation during real kernel work.
// The former warped crease now fails safely before an expensive offset starts.
inline kachakacha::v2::base::Result<kachakacha::v2::modeling::KernelShapeHandle> HeavySmoothSurface() {
    using namespace kachakacha::v2;
    using Result=base::Result<modeling::KernelShapeHandle>;
    const std::vector<geometry::Vector3> points{
#include "../../../tests_v2/fixtures/vase_profile_points.inc"
    };
    modeling::GuideChain chain;chain.role=modeling::ChainRole::Section;
    chain.segments={geometry::CurveSegment::MakeCubicBSpline(points).Value()};
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::Revolve;
    request.chains={chain};request.revolveAxisDirection={0,0,1};request.revolveAngleRad=2*std::acos(-1.0);
    const auto analyzed=modeling::AnalyzeGuideSurfaceRequest(request,{});
    if(!analyzed.HasValue())return Result::Failure(analyzed.Diagnostics());
    const auto made=kernel::BuildGuideSurface(request,analyzed.Value(),{});
    return made.HasValue()?Result::Success(made.Value().handle):Result::Failure(made.Diagnostics());
}
