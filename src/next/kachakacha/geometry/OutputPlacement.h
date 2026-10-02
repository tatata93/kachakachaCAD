#pragma once
#include "kachakacha/geometry/Vector3.h"
namespace kachakacha::v2::geometry {
struct OutputFrame {
    Vector3 origin{}, normal{0,0,1}, xDirection{1,0,0};
    [[nodiscard]] bool Valid() const {
        return origin.IsFinite() && normal.IsFinite() && xDirection.IsFinite()
            && normal.Length()>1e-12 && Cross(normal,xDirection).Length()>1e-12;
    }
};
struct OutputPlacement {
    bool keepPosition=true;
    OutputFrame source, destination;
    [[nodiscard]] Vector3 Direction(const Vector3& v) const {
        if(keepPosition)return v;
        const auto sz=Normalized(source.normal),sx=Normalized(source.xDirection-sz*Dot(source.xDirection,sz));
        const auto dz=Normalized(destination.normal),dx=Normalized(destination.xDirection-dz*Dot(destination.xDirection,dz));
        return dx*Dot(v,sx)+Cross(dz,dx)*Dot(v,Cross(sz,sx))+dz*Dot(v,sz);
    }
    [[nodiscard]] Vector3 Point(const Vector3& p) const {
        return keepPosition ? p : destination.origin+Direction(p-source.origin);
    }
    [[nodiscard]] bool Valid() const {return keepPosition || (source.Valid()&&destination.Valid());}
};
}
