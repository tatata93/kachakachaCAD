#include "kachakacha/kernel/OcctExtrude.h"

#include "kachakacha/modeling/SubshapeKey.h"

#include <algorithm>
#include <cmath>
#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_Surface.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#endif

namespace kachakacha::v2::kernel {

using base::Diagnostic;
using base::MakeError;
using base::MakeWarning;
using base::Result;
using geometry::CurveSegment;
using geometry::GeometryTolerance;
using geometry::Vector3;
using modeling::ExtrudeAnalysis;
using modeling::ExtrudeBooleanMode;
using modeling::ExtrudeExtentMode;
using modeling::ExtrudeRequest;
using modeling::ExtrudeTargetKind;
using modeling::KernelShapeHandle;

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

template<class Function>
[[nodiscard]] auto Guarded(Function&& body, const char* what) -> decltype(body())
{
    using ResultType = decltype(body());
    try {
        return body();
    } catch (const Standard_Failure& failure) {
        return ResultType::Failure(MakeError(kExtrudeBuildFailed,
            "幾何カーネルが押し出しを行えませんでした。",
            std::string(what) + ": " + std::string(failure.GetMessageString())));
    } catch (const std::exception& error) {
        return ResultType::Failure(MakeError(kExtrudeBuildFailed,
            "幾何カーネルが押し出しを行えませんでした。",
            std::string(what) + ": " + error.what()));
    } catch (...) {
        return ResultType::Failure(MakeError(kExtrudeBuildFailed,
            "幾何カーネルが押し出しを行えませんでした。", what));
    }
}

[[nodiscard]] std::size_t CountOf(const TopoDS_Shape& shape, TopAbs_ShapeEnum kind)
{
    std::size_t count = 0;
    for (TopExp_Explorer explorer(shape, kind); explorer.More(); explorer.Next()) {
        ++count;
    }
    return count;
}

[[nodiscard]] double VolumeOf(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return std::abs(properties.Mass());
}

[[nodiscard]] double DiagonalOf(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) {
        return 0.0;
    }
    double x0 = 0.0;
    double y0 = 0.0;
    double z0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    double z1 = 0.0;
    box.Get(x0, y0, z0, x1, y1, z1);
    return std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)
        + (z1 - z0) * (z1 - z0));
}

//! 外周1つと、それに属する穴から、輪郭の面を1枚作る。
[[nodiscard]] Result<TopoDS_Face> ProfileFace(const ExtrudeRequest& request,
    const ExtrudeAnalysis& analysis, const modeling::ExtrudeLoop& loop,
    const GeometryTolerance& tolerance)
{
    const gp_Pln plane(ToPoint(analysis.profilePlane.origin),
        gp_Dir(ToVector(analysis.profilePlane.normal)));
    auto outer = ToWire(request.profiles[loop.profileIndex].segments,
        tolerance.modelLinearMm);
    if (!outer.HasValue()) {
        return Result<TopoDS_Face>::Failure(outer.Diagnostics());
    }
    BRepBuilderAPI_MakeFace maker(plane, outer.Value(), Standard_True);
    if (!maker.IsDone()) {
        return Result<TopoDS_Face>::Failure(MakeError(kExtrudeBuildFailed,
            "輪郭から面を作れませんでした。", {}));
    }
    for (const std::size_t holeIndex : loop.holes) {
        auto hole = ToWire(request.profiles[holeIndex].segments, tolerance.modelLinearMm);
        if (!hole.HasValue()) {
            return Result<TopoDS_Face>::Failure(hole.Diagnostics());
        }
        TopoDS_Wire wire = hole.Value();
        wire.Reverse();
        maker.Add(wire);
        if (!maker.IsDone()) {
            return Result<TopoDS_Face>::Failure(MakeError(kExtrudeBuildFailed,
                "穴を輪郭から抜けませんでした。", {}));
        }
    }
    return Result<TopoDS_Face>::Success(maker.Face());
}

[[nodiscard]] TopoDS_Shape MovedAlong(const TopoDS_Shape& shape, const Vector3& direction,
    double distance)
{
    if (std::abs(distance) <= Precision::Confusion()) {
        return shape;
    }
    gp_Trsf move;
    move.SetTranslation(ToVector(direction) * distance);
    return BRepBuilderAPI_Transform(shape, move, Standard_True).Shape();
}

//! 押し出す先の面を、押し出しの範囲を覆う大きさで作る。
//! 覆いきれない大きさで作ると、trim が黙って途中で終わる。
[[nodiscard]] Result<TopoDS_Face> TargetFace(const ExtrudeRequest& request,
    double reach, const GeometryTolerance& tolerance)
{
    using Out = Result<TopoDS_Face>;
    const double margin = std::max(reach * 4.0, 1000.0);
    if (request.targetKind == ExtrudeTargetKind::Plane) {
        const gp_Pln plane(ToPoint(request.targetPlane.origin),
            gp_Dir(ToVector(request.targetPlane.normal)));
        BRepBuilderAPI_MakeFace maker(plane, -margin, margin, -margin, margin);
        if (!maker.IsDone()) {
            return Out::Failure(MakeError(kExtrudeBuildFailed,
                "押し出す先の平面を作れませんでした。", {}));
        }
        return Out::Success(maker.Face());
    }
    const auto& surface = request.targetSurface;
    const gp_Ax3 axis(ToPoint(surface.origin), gp_Dir(ToVector(surface.axis)),
        gp_Dir(ToVector(surface.reference)));
    occ::handle<Geom_Surface> geometry;
    double v0 = -margin;
    double v1 = margin;
    switch (surface.kind) {
    case fabrication::AnalyticSurfaceKind::Plane:
        geometry = new Geom_Plane(axis);
        break;
    case fabrication::AnalyticSurfaceKind::Cylinder:
        geometry = new Geom_CylindricalSurface(axis, surface.radiusMm);
        break;
    case fabrication::AnalyticSurfaceKind::Cone:
        geometry = new Geom_ConicalSurface(axis, surface.halfAngleRad, surface.radiusMm);
        break;
    case fabrication::AnalyticSurfaceKind::Sphere:
        geometry = new Geom_SphericalSurface(axis, surface.radiusMm);
        v0 = -M_PI_2;
        v1 = M_PI_2;
        break;
    default:
        // トーラスや正体不明の面までは、厳密に切れない。近い形で誤魔化さずに断る。
        return Out::Failure(MakeError(kExtrudeUnsupported,
            "この種類の面までは、まだ厳密に押し出せません。",
            "平面・円筒・円錐・球のいずれかを選んでください。"));
    }
    BRepBuilderAPI_MakeFace maker(geometry, 0.0, 2.0 * M_PI, v0, v1,
        std::max(tolerance.modelLinearMm, Precision::Confusion()));
    if (!maker.IsDone()) {
        return Out::Failure(MakeError(kExtrudeBuildFailed,
            "押し出す先の面を作れませんでした。", {}));
    }
    return Out::Success(maker.Face());
}

//! 先の面より手前だけを残す。半空間で切る。
[[nodiscard]] Result<TopoDS_Shape> TrimToTarget(const TopoDS_Shape& prism,
    const TopoDS_Face& target, const Vector3& insidePoint)
{
    using Out = Result<TopoDS_Shape>;
    BRepPrimAPI_MakeHalfSpace halfSpace(target, ToPoint(insidePoint));
    if (!halfSpace.IsDone()) {
        return Out::Failure(MakeError(kExtrudeBuildFailed,
            "押し出す先で切るための領域を作れませんでした。", {}));
    }
    BRepAlgoAPI_Common common(prism, halfSpace.Solid());
    common.Build();
    if (!common.IsDone()) {
        return Out::Failure(MakeError(kExtrudeBuildFailed,
            "押し出す先で切れませんでした。", {}));
    }
    const TopoDS_Shape result = common.Shape();
    if (CountOf(result, TopAbs_SOLID) == 0) {
        return Out::Failure(MakeError(kExtrudeBuildFailed,
            "押し出す先で切ったら、何も残りませんでした。", {}));
    }
    return Out::Success(result);
}

//! 出来た solid を1つずつ取り出す。
[[nodiscard]] std::vector<TopoDS_Shape> SolidsOf(const TopoDS_Shape& shape)
{
    std::vector<TopoDS_Shape> solids;
    for (TopExp_Explorer explorer(shape, TopAbs_SOLID); explorer.More(); explorer.Next()) {
        solids.push_back(explorer.Current());
    }
    if (solids.empty()) {
        solids.push_back(shape);
    }
    return solids;
}

//! 押し出しで出来た面と、その意味的キーの対応。
//! OCCT の面番号は保存しない。番号は再計算のたびに変わるからである。
struct FaceKeyMap {
    std::vector<TopoDS_Shape> faces;
    std::vector<std::string> keys;

    [[nodiscard]] std::string Find(const TopoDS_Shape& face) const
    {
        for (std::size_t index = 0; index < faces.size(); ++index) {
            if (faces[index].IsSame(face)) {
                return keys[index];
            }
        }
        return std::string();
    }
};

//! 押し出し器が「どの辺から、どの面を作ったか」を答えられる。
//! それを使ってキーを付ける。近い面へ当てずっぽうで付けない。
void CollectFaceKeys(BRepPrimAPI_MakePrism& prism, const TopoDS_Shape& profileFace,
    const modeling::ExtrudeRequest& request, const modeling::ExtrudeLoop& loop,
    FaceKeyMap& out)
{
    // 蓋。押し出す前の面と、押し出した先の面。
    out.faces.push_back(profileFace);
    out.keys.push_back(modeling::MakeExtrudeCapStart().ToString());
    const TopoDS_Shape endCap = prism.LastShape();
    if (!endCap.IsNull()) {
        out.faces.push_back(endCap);
        out.keys.push_back(modeling::MakeExtrudeCapEnd().ToString());
    }
    const TopoDS_Shape startCap = prism.FirstShape();
    if (!startCap.IsNull()) {
        out.faces.push_back(startCap);
        out.keys.push_back(modeling::MakeExtrudeCapStart().ToString());
    }

    // 側面。輪郭の線1本につき1枚。
    const auto addSides = [&](std::size_t profileIndex) {
        const modeling::ExtrudeProfile& profile = request.profiles[profileIndex];
        std::size_t segmentIndex = 0;
        for (TopExp_Explorer explorer(profileFace, TopAbs_EDGE); explorer.More();
            explorer.Next()) {
            // OCCT は「その辺から作られた形」を一覧で返す。ふつうは1枚。
            const auto& generatedList = prism.Generated(explorer.Current());
            if (generatedList.IsEmpty()) {
                ++segmentIndex;
                continue;
            }
            const TopoDS_Shape generated = generatedList.First();
            if (generated.IsNull()) {
                ++segmentIndex;
                continue;
            }
            base::SegmentId id;
            if (segmentIndex < profile.segmentIds.size()) {
                id = profile.segmentIds[segmentIndex];
            }
            out.faces.push_back(generated);
            out.keys.push_back(modeling::MakeExtrudeSide(id).ToString());
            ++segmentIndex;
        }
    };
    addSides(loop.profileIndex);
}

} // namespace

Result<double> ShapeVolume(KernelShapeHandle handle)
{
    TopoDS_Shape shape;
    if (!LookupShape(handle, shape)) {
        return Result<double>::Failure(MakeError(kExtrudeBooleanTargetMissing,
            "その番号の形は表にありません。", {}));
    }
    return Guarded([&]() -> Result<double> {
        return Result<double>::Success(VolumeOf(shape));
    }, "体積を測る");
}

Result<ExtrudeBuildResult> BuildExtrude(const ExtrudeRequest& request,
    const ExtrudeAnalysis& analysis, const GeometryTolerance& tolerance,
    KernelShapeHandle booleanTarget)
{
    using Out = Result<ExtrudeBuildResult>;

    TopoDS_Shape targetPart;
    if (request.outputs.part && request.booleanMode != ExtrudeBooleanMode::NewPart) {
        if (!booleanTarget.Valid() || !LookupShape(booleanTarget, targetPart)) {
            return Out::Failure(MakeError(kExtrudeBooleanTargetMissing,
                "足す/引く相手の部品が見つかりません。", {}));
        }
    }

    return Guarded([&]() -> Out {
        ExtrudeBuildResult built;
        std::vector<Diagnostic> warnings;

        // ---- 外周ごとに輪郭の面を作る ----
        std::vector<TopoDS_Face> profileFaces;
        std::vector<const modeling::ExtrudeLoop*> profileLoops;
        for (const modeling::ExtrudeLoop& loop : analysis.loops) {
            if (loop.isHole) {
                continue;
            }
            auto face = ProfileFace(request, analysis, loop, tolerance);
            if (!face.HasValue()) {
                return Out::Failure(face.Diagnostics());
            }
            profileFaces.push_back(face.Value());
            profileLoops.push_back(&loop);
        }
        if (profileFaces.empty()) {
            return Out::Failure(MakeError(kExtrudeBuildFailed,
                "押し出す輪郭の面が作れませんでした。", {}));
        }

        // ---- 押し出す長さを決める ----
        double startOffset = analysis.startOffsetMm;
        double length = analysis.endOffsetMm - analysis.startOffsetMm;
        if (request.extent == ExtrudeExtentMode::ToTarget) {
            // 先の面まで確実に届く長さで押してから、先の面で切る。
            startOffset = 0.0;
            length = analysis.maximumReachMm * 1.5 + 1.0;
        } else if (request.extent == ExtrudeExtentMode::ThroughAll) {
            const double diagonal = DiagonalOf(targetPart);
            if (!(diagonal > 0.0)) {
                return Out::Failure(MakeError(kExtrudeBuildFailed,
                    "貫く相手の大きさが取れませんでした。", {}));
            }
            startOffset = -diagonal;
            length = 2.0 * diagonal;
        }
        if (std::abs(length) <= Precision::Confusion()) {
            return Out::Failure(MakeError(kExtrudeBuildFailed,
                "押し出す長さが0です。", {}));
        }

        // ---- 押し出す ----
        std::vector<TopoDS_Shape> prisms;
        FaceKeyMap faceKeys;
        for (std::size_t index = 0; index < profileFaces.size(); ++index) {
            const TopoDS_Shape moved =
                MovedAlong(profileFaces[index], analysis.direction, startOffset);
            BRepPrimAPI_MakePrism prism(moved, ToVector(analysis.direction) * length);
            prism.Build();
            if (!prism.IsDone()) {
                return Out::Failure(MakeError(kExtrudeBuildFailed,
                    "押し出せませんでした。", {}));
            }
            CollectFaceKeys(prism, moved, request, *profileLoops[index], faceKeys);
            prisms.push_back(prism.Shape());
        }

        // ---- 先の面で切る ----
        if (request.extent == ExtrudeExtentMode::ToTarget) {
            auto target = TargetFace(request, analysis.maximumReachMm, tolerance);
            if (!target.HasValue()) {
                return Out::Failure(target.Diagnostics());
            }
            // 輪郭の側に残す。輪郭の重心を「内側の点」として使う。
            const Vector3 inside = analysis.profilePlane.origin
                - analysis.direction * std::max(analysis.minimumReachMm * 0.01, 1.0e-3);
            std::vector<TopoDS_Shape> trimmed;
            for (const TopoDS_Shape& prism : prisms) {
                auto piece = TrimToTarget(prism, target.Value(), inside);
                if (!piece.HasValue()) {
                    return Out::Failure(piece.Diagnostics());
                }
                trimmed.push_back(piece.Value());
            }
            prisms = std::move(trimmed);
        }

        // ---- 足す / 引く ----
        std::vector<TopoDS_Shape> finished;
        if (!request.outputs.part) {
            finished = prisms;
        } else if (request.booleanMode == ExtrudeBooleanMode::NewPart) {
            finished = prisms;
        } else {
            TopoDS_Shape current = targetPart;
            for (const TopoDS_Shape& prism : prisms) {
                if (request.booleanMode == ExtrudeBooleanMode::AddToPart) {
                    BRepAlgoAPI_Fuse fuse(current, prism);
                    fuse.Build();
                    if (!fuse.IsDone()) {
                        return Out::Failure(MakeError(kExtrudeBuildFailed,
                            "足せませんでした。", {}));
                    }
                    current = fuse.Shape();
                } else {
                    BRepAlgoAPI_Cut cut(current, prism);
                    cut.Build();
                    if (!cut.IsDone()) {
                        return Out::Failure(MakeError(kExtrudeBuildFailed,
                            "引けませんでした。", {}));
                    }
                    current = cut.Shape();
                }
            }
            const std::size_t solids = CountOf(current, TopAbs_SOLID);
            if (solids == 0) {
                return Out::Failure(MakeError(kExtrudeBuildFailed,
                    "演算の結果、部品が無くなりました。",
                    "引く形が元の部品を全部覆っています。"));
            }
            if (request.booleanMode == ExtrudeBooleanMode::AddToPart && solids > 1) {
                // 足した結果が非連結なら拒否する(geometry-contract §8.4)。
                return Out::Failure(MakeError("EXT-005",
                    "足した結果が離ればなれになります。",
                    std::to_string(solids) + " 個に分かれます。"
                        + "つながる位置に置くか、別の部品として作ってください。"));
            }
            if (request.booleanMode == ExtrudeBooleanMode::SubtractFromPart
                && solids > 1) {
                // 分割は拒否しない。ただし置き換える前に個数を知らせる(§8.4)。
                warnings.push_back(MakeWarning("EXT-102",
                    "引いた結果、部品が分かれます。",
                    std::to_string(solids) + " 個になります。"));
            }
            finished.push_back(current);
        }

        // ---- 結果をまとめる ----
        for (const TopoDS_Shape& shape : finished) {
            for (const TopoDS_Shape& solid : SolidsOf(shape)) {
                ExtrudedPart part;
                part.volumeMm3 = VolumeOf(solid);
                part.faceCount = CountOf(solid, TopAbs_FACE);
                for (TopExp_Explorer explorer(solid, TopAbs_FACE); explorer.More();
                    explorer.Next()) {
                    std::string key = faceKeys.Find(explorer.Current());
                    if (key.empty()) {
                        // 演算で作り直された面。どこから来たかを演算の由来として残す。
                        key = modeling::MakeBooleanProvenance(
                            request.profiles.empty() ? base::EntityId{}
                                                     : request.profiles.front()
                                                           .sourceEntityId,
                            modeling::MakeExtrudeSide(base::SegmentId{}).ToString())
                                  .ToString();
                    }
                    part.faceKeys.push_back(std::move(key));
                }
                part.handle = StoreShape(solid);
                built.totalVolumeMm3 += part.volumeMm3;
                built.totalFaceCount += part.faceCount;
                built.parts.push_back(std::move(part));
            }
        }

        // ---- 輪郭ワイヤーと側面の境界ワイヤー ----
        // 部品と別々に計算しない。同じ押し出しの結果から取り出す(§8.4)。
        if (request.outputs.endProfileWire) {
            for (const TopoDS_Face& face : profileFaces) {
                const TopoDS_Shape moved =
                    MovedAlong(face, analysis.direction, analysis.endOffsetMm);
                const TopoDS_Wire outer = BRepTools::OuterWire(TopoDS::Face(moved));
                if (outer.IsNull()) {
                    continue;
                }
                auto segments = FromWire(outer, tolerance.modelLinearMm);
                if (!segments.HasValue()) {
                    warnings.push_back(MakeWarning("KER-E100",
                        "押し出し先の輪郭を、曲線の種類を保ったまま取り出せませんでした。",
                        {}));
                    continue;
                }
                built.endProfileWires.push_back(segments.Value());
            }
        }
        if (request.outputs.sideBoundaryWires) {
            for (const ExtrudedPart& part : built.parts) {
                TopoDS_Shape solid;
                if (!LookupShape(part.handle, solid)) {
                    continue;
                }
                for (TopExp_Explorer explorer(solid, TopAbs_FACE); explorer.More();
                    explorer.Next()) {
                    const TopoDS_Face face = TopoDS::Face(explorer.Current());
                    occ::handle<Geom_Surface> surface = BRep_Tool::Surface(face);
                    if (!surface.IsNull()
                        && surface->IsKind(STANDARD_TYPE(Geom_Plane))) {
                        const gp_Pln plane =
                            occ::handle<Geom_Plane>::DownCast(surface)->Pln();
                        const gp_Dir normal = plane.Axis().Direction();
                        const Vector3 value{normal.X(), normal.Y(), normal.Z()};
                        if (std::abs(std::abs(geometry::Dot(value, analysis.direction))
                                - 1.0) <= 1.0e-9) {
                            continue;   // 蓋は側面ではない
                        }
                    }
                    const TopoDS_Wire outer = BRepTools::OuterWire(face);
                    if (outer.IsNull()) {
                        continue;
                    }
                    auto segments = FromWire(outer, tolerance.modelLinearMm);
                    if (segments.HasValue()) {
                        built.sideBoundaryWires.push_back(segments.Value());
                    }
                }
            }
        }

        // ---- 予測と突き合わせる ----
        // これが V1 との決定的な違い。合わなければ、この結果は使わない。
        if (request.outputs.part
            && request.booleanMode == ExtrudeBooleanMode::NewPart
            && (request.extent == ExtrudeExtentMode::Distance
                || request.extent == ExtrudeExtentMode::SymmetricDistance
                || request.extent == ExtrudeExtentMode::TwoDistances)) {
            const auto check = modeling::CheckExtrudeResult(analysis,
                built.totalVolumeMm3, built.totalFaceCount, built.parts.size());
            if (!check.partCountMatches) {
                return Out::Failure(MakeError(kExtrudeMismatch,
                    "出来た部品の数が、事前に示した数と違います。",
                    "予定 " + std::to_string(analysis.expectedPartCount) + " 個 / 実際 "
                        + std::to_string(built.parts.size()) + " 個。"));
            }
            if (!check.faceCountMatches) {
                return Out::Failure(MakeError(kExtrudeMismatch,
                    "出来た面の数が、予測と違います。",
                    "予測 " + std::to_string(analysis.predictedFaceCount) + " 枚 / 実際 "
                        + std::to_string(built.totalFaceCount) + " 枚。"
                        + "輪郭の曲線が別の形へ置き換わった可能性があります。"));
            }
            if (!check.volumeMatches) {
                return Out::Failure(MakeError(kExtrudeMismatch,
                    "出来た体積が、予測と合いません。",
                    "予測 " + std::to_string(analysis.predictedVolumeMm3) + " mm3 / 実際 "
                        + std::to_string(built.totalVolumeMm3) + " mm3(ずれ "
                        + std::to_string(check.volumeErrorRatio * 100.0) + " %)。"));
            }
        }

        return Out::Success(std::move(built), std::move(warnings));
    }, "押し出し");
}

#else // KACHACAD_V2_WITH_OCCT

Result<double> ShapeVolume(KernelShapeHandle handle)
{
    (void)handle;
    return Result<double>::Failure(MakeError(kExtrudeUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

Result<ExtrudeBuildResult> BuildExtrude(const ExtrudeRequest& request,
    const ExtrudeAnalysis& analysis, const GeometryTolerance& tolerance,
    KernelShapeHandle booleanTarget)
{
    (void)request;
    (void)analysis;
    (void)tolerance;
    (void)booleanTarget;
    return Result<ExtrudeBuildResult>::Failure(MakeError(kExtrudeUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。",
        "OCCT を有効にしてビルドしてください。"));
}

#endif // KACHACAD_V2_WITH_OCCT

} // namespace kachakacha::v2::kernel
