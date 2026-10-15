#pragma once
#include "kachakacha/geometry/Vector3.h"
#include <cmath>

namespace kachakacha::v2::modeling {
enum class EndCapMode { Natural, Source, Reference, Angle };
struct ExtrudeEndCap {
    EndCapMode mode = EndCapMode::Natural;
    geometry::Vector3 referenceNormal{}; // selected-time tangent, independent of reference position
    double tiltRad = 0;
    double headingRad = 0;
    bool extendTargetPlane = false;
    bool operator==(const ExtrudeEndCap& other) const {
        return mode==other.mode && referenceNormal.x==other.referenceNormal.x
            && referenceNormal.y==other.referenceNormal.y && referenceNormal.z==other.referenceNormal.z
            && tiltRad==other.tiltRad && headingRad==other.headingRad
            && extendTargetPlane==other.extendTargetPlane;
    }
    bool Planar() const { return mode==EndCapMode::Reference || mode==EndCapMode::Angle; }
};
inline geometry::Vector3 EndCapNormal(const ExtrudeEndCap& cap, geometry::Vector3 sourceNormal) {
    using namespace geometry;
    if(cap.mode==EndCapMode::Reference) return Normalized(cap.referenceNormal,1e-12);
    const auto n=Normalized(sourceNormal,1e-12);
    const auto axis=std::abs(n.z)<0.9?Vector3{0,0,1}:Vector3{0,1,0};
    const auto u=Normalized(Cross(axis,n),1e-12),v=Cross(n,u);
    return n*std::cos(cap.tiltRad)+(u*std::cos(cap.headingRad)+v*std::sin(cap.headingRad))*std::sin(cap.tiltRad);
}
}
