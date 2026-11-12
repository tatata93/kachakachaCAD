// Attached source revision 953, SHA256 84c716b3e6af1ae8f87da4ee4df92226ef326b03e1e44f8c6ce7f84152125ede.
// The ten original chain ranges, directions and duplicate Bezier endpoint are retained.
modeling::GuideSurfaceRequest OwnerTopDomeRequest() {
    using C=geometry::CurveSegment;
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::BoundaryFill;
    request.fourEdgeStyle=modeling::FourEdgeStyle::Dome;
    const auto add=[&](geometry::CurveSegment curve) {
        modeling::GuideChain chain;chain.role=modeling::ChainRole::BoundarySide;
        chain.index=static_cast<int>(request.chains.size()+1);chain.segments.push_back(std::move(curve));
        request.chains.push_back(std::move(chain));
    };
    add(C::MakeLine({18,-22.080243102889174,27},{18,-20.044156168947911,27}).Value());
    add(C::MakeLine({18,-20.044156168947911,27},{18,-20,27}).Value());
    add(C::MakeLine({18,-20,27},{18,0,27}).Value());
    add(C::MakeCircularArc({13.5,0,27},{0,1,0},{1,0,0},4.5,0,-1.4594553124530221).Value());
    add(geometry::ReverseCurve(C::MakeCircularArc({-2.0818902157770935e-12,0,-63.357280665426231},{0,1,0},{-0.14605046067248964,0,0.98927714162278801},95.857280665426245,0.14657473619151001,0.14657473619151001).Value()).Value());
    add(geometry::ReverseCurve(C::MakeLine({0,-10,32.5},{1.9539925233402755e-14,0,32.500000000000021}).Value()).Value());
    add(geometry::ReverseCurve(C::MakeCubicBezier({{0,-27,27},{0,-27,32.5},{0,-10,32.5},{0,-10,32.5}}).Value()).Value());
    add(C::MakeLine({0,-27,27},{6,-27,27}).Value());
    add(geometry::ReverseCurve(C::MakeLine({15.606101452386682,-25.018378741251496,27},{6,-27,27}).Value()).Value());
    add(geometry::ReverseCurve(C::MakeCircularArc({15,-22.080243102889174,27},{0,0,1},{1,0,0},3,0,-1.3673622083094905).Value()).Value());
    return request;
}
