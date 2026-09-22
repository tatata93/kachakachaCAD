#include "kachakacha/kernel/OcctTransform.h"

#include <cmath>
#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#endif

namespace kachakacha::v2::kernel {

using base::MakeError;
using base::Result;
using modeling::KernelShapeHandle;

namespace {

[[nodiscard]] double LengthOf(const geometry::Vector3& value)
{
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

//! 変換の中身が読めるか。読めなければ理由(空なら読める)。
[[nodiscard]] std::string BadTransformReason(const ShapeTransform& transform)
{
    const double length = LengthOf(transform.vector);
    if (!std::isfinite(length) || !std::isfinite(transform.angleRad)
        || !std::isfinite(transform.point.x) || !std::isfinite(transform.point.y)
        || !std::isfinite(transform.point.z)) {
        return "変換の数が読めません。";
    }
    switch (transform.kind) {
    case ShapeTransformKind::Translate:
        return length < 1.0e-9 ? "動かす量が 0mm です。" : std::string();
    case ShapeTransformKind::Rotate:
        if (length < 1.0e-9) {
            return "回転の軸の向きが決まっていません。";
        }
        return std::abs(transform.angleRad) < 1.0e-12 ? "回す角度が 0° です。" : std::string();
    case ShapeTransformKind::Mirror:
        return length < 1.0e-9 ? "鏡の面の向きが決まっていません。" : std::string();
    }
    return "変換の種類が分かりません。";
}

} // namespace

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

//! 向きつきの体積(裏返った立体は負になる)。
[[nodiscard]] double SignedVolumeOf(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

[[nodiscard]] gp_Trsf TrsfOf(const ShapeTransform& transform)
{
    gp_Trsf trsf;
    const gp_Pnt point(transform.point.x, transform.point.y, transform.point.z);
    const gp_Vec vector(transform.vector.x, transform.vector.y, transform.vector.z);
    switch (transform.kind) {
    case ShapeTransformKind::Translate:
        trsf.SetTranslation(vector);
        break;
    case ShapeTransformKind::Rotate:
        trsf.SetRotation(gp_Ax1(point, gp_Dir(vector)), transform.angleRad);
        break;
    case ShapeTransformKind::Mirror:
        // gp_Ax2 の主軸を法線とする面(点を通る)に対して写す。
        trsf.SetMirror(gp_Ax2(point, gp_Dir(vector)));
        break;
    }
    return trsf;
}

} // namespace

Result<ShapeTransformResult> TransformShape(KernelShapeHandle source,
    const ShapeTransform& transform)
{
    using Out = Result<ShapeTransformResult>;
    TopoDS_Shape shape;
    if (!source.Valid() || !LookupShape(source, shape)) {
        return Out::Failure(MakeError(kPlaceSourceMissing, "元の部品の形が見つかりません。",
            "元の部品が作り直せていないか、消えています。"));
    }
    if (const std::string why = BadTransformReason(transform); !why.empty()) {
        return Out::Failure(MakeError(kPlaceBadTransform, "動かし方が決まっていません。", why));
    }
    try {
        const double before = SignedVolumeOf(shape);
        BRepBuilderAPI_Transform builder(shape, TrsfOf(transform), Standard_True);
        if (!builder.IsDone()) {
            return Out::Failure(MakeError(kPlaceFailed, "部品を動かせませんでした。",
                "変換を掛けるところで止まりました。"));
        }
        TopoDS_Shape made = builder.Shape();
        double after = SignedVolumeOf(made);
        // 鏡に写すと向きが裏返ることがある。裏返っていたら向きを戻す(形は同じ)。
        if (before * after < 0.0) {
            made.Reverse();
            after = SignedVolumeOf(made);
        }
        const double scale = std::abs(before) > 0.0 ? std::abs(before) : 1.0;
        if (std::abs(std::abs(after) - std::abs(before)) / scale > 1.0e-6 || before * after < 0.0) {
            return Out::Failure(MakeError(kPlaceVolumeChanged,
                "動かしたら体積が変わったので、作れたことにしません。",
                "元 " + std::to_string(std::abs(before)) + " mm3 → " + std::to_string(after)
                    + " mm3"));
        }
        ShapeTransformResult result;
        result.handle = StoreShape(made);
        result.volumeMm3 = std::abs(after);
        return Out::Success(result);
    } catch (const Standard_Failure& failure) {
        return Out::Failure(MakeError(kPlaceFailed, "部品を動かせませんでした。",
            std::string(failure.GetMessageString())));
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kPlaceFailed, "部品を動かせませんでした。", error.what()));
    } catch (...) {
        return Out::Failure(MakeError(kPlaceFailed, "部品を動かせませんでした。",
            "理由が分かりません。"));
    }
}

#else

Result<ShapeTransformResult> TransformShape(KernelShapeHandle source,
    const ShapeTransform& transform)
{
    (void)source;
    if (const std::string why = BadTransformReason(transform); !why.empty()) {
        return Result<ShapeTransformResult>::Failure(
            MakeError(kPlaceBadTransform, "動かし方が決まっていません。", why));
    }
    return Result<ShapeTransformResult>::Failure(MakeError(kPlaceUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

#endif // KACHACAD_V2_WITH_OCCT

} // namespace kachakacha::v2::kernel
