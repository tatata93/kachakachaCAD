#include "kachakacha/kernel/OcctSolid.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#endif

namespace kachakacha::v2::kernel {

using base::MakeError;
using base::Result;

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

using Out = Result<SolidBuildResult>;

[[nodiscard]] double VolumeOf(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return std::abs(properties.Mass());
}

//! 外周 1 つと、それに属する穴から、輪郭の面を 1 枚ずつ(押し出しと同じ読み方)。
[[nodiscard]] Result<std::vector<TopoDS_Face>> ProfileFaces(
    const std::vector<modeling::ExtrudeProfile>& profiles, const modeling::ExtrudeAnalysis& analysis,
    const geometry::GeometryTolerance& tolerance)
{
    using Faces = Result<std::vector<TopoDS_Face>>;
    const gp_Pln plane(ToPoint(analysis.profilePlane.origin),
        gp_Dir(ToVector(analysis.profilePlane.normal)));
    std::vector<TopoDS_Face> faces;
    for (const modeling::ExtrudeLoop& loop : analysis.loops) {
        if (loop.isHole) {
            continue;
        }
        auto outer = ToWire(profiles[loop.profileIndex].segments, tolerance.modelLinearMm);
        if (!outer.HasValue()) {
            return Faces::Failure(outer.Diagnostics());
        }
        BRepBuilderAPI_MakeFace maker(plane, outer.Value(), Standard_True);
        if (!maker.IsDone()) {
            return Faces::Failure(MakeError(kSolidBuildFailed, "輪郭から面を作れませんでした。", {}));
        }
        for (const std::size_t holeIndex : loop.holes) {
            auto hole = ToWire(profiles[holeIndex].segments, tolerance.modelLinearMm);
            if (!hole.HasValue()) {
                return Faces::Failure(hole.Diagnostics());
            }
            TopoDS_Wire wire = hole.Value();
            wire.Reverse();
            maker.Add(wire);
        }
        if (!maker.IsDone()) {
            return Faces::Failure(MakeError(kSolidBuildFailed, "穴を輪郭から抜けませんでした。", {}));
        }
        faces.push_back(maker.Face());
    }
    if (faces.empty()) {
        return Faces::Failure(MakeError(kSolidBuildFailed, "外周のある輪郭がありません。", {}));
    }
    return Faces::Success(std::move(faces));
}

//! 形を 1 つにまとめる(離れていれば束のまま。1 つなら そのもの)。
[[nodiscard]] TopoDS_Shape Combined(const std::vector<TopoDS_Shape>& shapes)
{
    if (shapes.size() == 1) {
        return shapes.front();
    }
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (const TopoDS_Shape& shape : shapes) {
        builder.Add(compound, shape);
    }
    return compound;
}

//! 立体として壊れていないか、体積が正かを見て、覚えて返す。
[[nodiscard]] Out Adopt(const TopoDS_Shape& shape, const char* what)
{
    if (shape.IsNull()) {
        return Out::Failure(MakeError(kSolidBuildFailed, std::string(what) + "を作れませんでした。", {}));
    }
    BRepCheck_Analyzer check(shape);
    if (!check.IsValid()) {
        return Out::Failure(MakeError(kSolidResultInvalid,
            std::string(what) + "の形が立体として壊れているので、作れたことにしません。",
            "輪郭が自分と重ならないか、経路が急に曲がっていないかを見てください。"));
    }
    const double volume = VolumeOf(shape);
    if (!(volume > 0.0)) {
        return Out::Failure(MakeError(kSolidResultInvalid,
            std::string(what) + "の体積が 0 なので、作れたことにしません。", {}));
    }
    SolidBuildResult result;
    result.handle = StoreShape(shape);
    result.volumeMm3 = volume;
    return Out::Success(result);
}

template <class Body>
[[nodiscard]] Out Guarded(Body&& body, const char* what)
{
    try {
        return body();
    } catch (const Standard_Failure& failure) {
        return Out::Failure(MakeError(kSolidBuildFailed, std::string(what) + "を作れませんでした。",
            std::string(failure.GetMessageString())));
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kSolidBuildFailed, std::string(what) + "を作れませんでした。",
            error.what()));
    } catch (...) {
        return Out::Failure(MakeError(kSolidBuildFailed, std::string(what) + "を作れませんでした。",
            "理由が分かりません。"));
    }
}

} // namespace

Result<SolidBuildResult> BuildRevolveSolid(const modeling::RevolveSolidRequest& request,
    const modeling::RevolveSolidAnalysis& analysis, const geometry::GeometryTolerance& tolerance)
{
    return Guarded([&]() -> Out {
        auto faces = ProfileFaces(request.profiles, analysis.profile, tolerance);
        if (!faces.HasValue()) {
            return Out::Failure(faces.Diagnostics());
        }
        const gp_Ax1 axis(ToPoint(analysis.axisPoint), gp_Dir(ToVector(analysis.axisDirection)));
        std::vector<TopoDS_Shape> solids;
        for (TopoDS_Face face : faces.Value()) {
            if (std::abs(analysis.startAngleRad) > 1.0e-12) {
                // 対称: 輪郭をいったん -角度/2 まで回してから、角度ぶん回す。
                gp_Trsf start;
                start.SetRotation(axis, analysis.startAngleRad);
                face = TopoDS::Face(BRepBuilderAPI_Transform(face, start, Standard_True).Shape());
            }
            BRepPrimAPI_MakeRevol revol(face, axis, analysis.angleRad, Standard_True);
            revol.Build();
            if (!revol.IsDone()) {
                return Out::Failure(MakeError(kSolidBuildFailed, "回転体を作れませんでした。",
                    "輪郭を軸のまわりに回すところで止まりました。"));
            }
            solids.push_back(revol.Shape());
        }
        auto made = Adopt(Combined(solids), "回転体");
        if (made.HasValue() && !modeling::RevolveVolumeMatches(analysis, made.Value().volumeMm3)) {
            return Out::Failure(MakeError(kSolidResultInvalid,
                "回転体の体積が予測と合わないので、作れたことにしません。",
                "予測 " + std::to_string(analysis.predictedVolumeMm3) + " mm3、出来た形 "
                    + std::to_string(made.Value().volumeMm3) + " mm3。"));
        }
        return made;
    }, "回転体");
}

Result<SolidBuildResult> BuildLoftSolid(const modeling::LoftSolidRequest& request,
    const modeling::LoftSolidAnalysis& analysis, const geometry::GeometryTolerance& tolerance)
{
    (void)analysis;
    return Guarded([&]() -> Out {
        BRepOffsetAPI_ThruSections loft(Standard_True, Standard_False,
            std::max(tolerance.modelLinearMm, 1.0e-6));
        for (const auto& section : request.sections) {
            auto wire = ToWire(section.segments, tolerance.modelLinearMm);
            if (!wire.HasValue()) {
                return Out::Failure(wire.Diagnostics());
            }
            loft.AddWire(wire.Value());
        }
        // 断面の向きと始まりの点をそろえる(ねじれを減らす)。
        loft.CheckCompatibility(Standard_True);
        loft.Build();
        if (!loft.IsDone()) {
            return Out::Failure(MakeError(kSolidBuildFailed, "ロフト立体を作れませんでした。",
                "断面を順に通すところで止まりました。"));
        }
        return Adopt(loft.Shape(), "ロフト立体");
    }, "ロフト立体");
}

Result<SolidBuildResult> BuildSweepSolid(const modeling::SweepSolidRequest& request,
    const modeling::SweepSolidAnalysis& analysis, const geometry::GeometryTolerance& tolerance)
{
    return Guarded([&]() -> Out {
        auto faces = ProfileFaces(request.profiles, analysis.profile, tolerance);
        if (!faces.HasValue()) {
            return Out::Failure(faces.Diagnostics());
        }
        auto spine = ToWire(analysis.orderedPath, tolerance.modelLinearMm);
        if (!spine.HasValue()) {
            return Out::Failure(spine.Diagnostics());
        }
        std::vector<TopoDS_Shape> solids;
        for (const TopoDS_Face& face : faces.Value()) {
            BRepOffsetAPI_MakePipe pipe(spine.Value(), face);
            pipe.Build();
            if (!pipe.IsDone()) {
                return Out::Failure(MakeError(kSolidBuildFailed, "スイープを作れませんでした。",
                    "輪郭を経路に沿って運ぶところで止まりました。"));
            }
            solids.push_back(pipe.Shape());
        }
        return Adopt(Combined(solids), "スイープ");
    }, "スイープ");
}

#else

Result<SolidBuildResult> BuildRevolveSolid(const modeling::RevolveSolidRequest& request,
    const modeling::RevolveSolidAnalysis& analysis, const geometry::GeometryTolerance& tolerance)
{
    (void)request;
    (void)analysis;
    (void)tolerance;
    return Result<SolidBuildResult>::Failure(MakeError(kSolidUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

Result<SolidBuildResult> BuildLoftSolid(const modeling::LoftSolidRequest& request,
    const modeling::LoftSolidAnalysis& analysis, const geometry::GeometryTolerance& tolerance)
{
    (void)request;
    (void)analysis;
    (void)tolerance;
    return Result<SolidBuildResult>::Failure(MakeError(kSolidUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

Result<SolidBuildResult> BuildSweepSolid(const modeling::SweepSolidRequest& request,
    const modeling::SweepSolidAnalysis& analysis, const geometry::GeometryTolerance& tolerance)
{
    (void)request;
    (void)analysis;
    (void)tolerance;
    return Result<SolidBuildResult>::Failure(MakeError(kSolidUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

#endif // KACHACAD_V2_WITH_OCCT

} // namespace kachakacha::v2::kernel
