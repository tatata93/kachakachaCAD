#pragma once
#include "V2MainWindowTypes.h"
#include "V2RevolveAxisInput.h"
#include "kachakacha/modeling/SnapEngine.h"
#include "kachakacha/document/Document.h"
#include <functional>

class V2MainWindow;
namespace v2ui {
using RevolveGroups=std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>>;
kachakacha::v2::base::Result<RevolveGroups> RevolveWires(const V2SurfaceSnapshot&,
    const kachakacha::v2::modeling::SnapScene&,const kachakacha::v2::document::DocumentSnapshot&);
using AdoptRevolve=std::function<kachakacha::v2::base::EntityId(
    const kachakacha::v2::modeling::GuideTable&,const kachakacha::v2::modeling::GuideSurfaceResult&)>;
kachakacha::v2::base::Result<std::vector<kachakacha::v2::base::EntityId>> CommitRevolveOutput(
    V2MainWindow&,const V2SurfaceSnapshot&,RevolveOutput,const AdoptRevolve&);
}
