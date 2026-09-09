#include "kachakacha/kernel/OcctBoolean.h"

#include <cmath>
#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>

#endif

namespace kachakacha::v2::kernel {

using base::MakeError;
using base::Result;
using modeling::KernelShapeHandle;

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

[[nodiscard]] double VolumeOf(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return std::abs(properties.Mass());
}

} // namespace

Result<BooleanBuildResult> BuildBoolean(BooleanOperation operation,
    KernelShapeHandle first, KernelShapeHandle second, double toleranceMm)
{
    using Out = Result<BooleanBuildResult>;
    (void)toleranceMm;
    TopoDS_Shape base;
    TopoDS_Shape other;
    if (!first.Valid() || !LookupShape(first, base)) {
        return Out::Failure(MakeError(kBooleanTargetMissing,
            "土台になる部品が見つかりません。", "1つ目に選んだ部品です。"));
    }
    if (!second.Valid() || !LookupShape(second, other)) {
        return Out::Failure(MakeError(kBooleanTargetMissing,
            "相手の部品が見つかりません。", "2つ目に選んだ部品です。"));
    }
    try {
        const double before = VolumeOf(base);
        TopoDS_Shape made;
        if (operation == BooleanOperation::Union) {
            BRepAlgoAPI_Fuse fuse(base, other);
            fuse.Build();
            if (!fuse.IsDone()) {
                return Out::Failure(MakeError(kBooleanFailed,
                    "足し引きを行えませんでした。", "足すところで止まりました。"));
            }
            made = fuse.Shape();
        } else {
            BRepAlgoAPI_Cut cut(base, other);
            cut.Build();
            if (!cut.IsDone()) {
                return Out::Failure(MakeError(kBooleanFailed,
                    "足し引きを行えませんでした。", "引くところで止まりました。"));
            }
            made = cut.Shape();
        }
        const double after = VolumeOf(made);
        if (after <= 0.0) {
            // 引き切って何も残らない。消えた部品を持ち続けない。
            return Out::Failure(MakeError(kBooleanNothingLeft,
                "引き切って何も残りませんでした。",
                "相手が土台をすべて含んでいます。"));
        }
        const double scale = before > 0.0 ? before : 1.0;
        if (std::abs(after - before) / scale <= 1.0e-9) {
            // 触れていない2つを足しても体積は変わらない。
            // それを「足しました」と言うのが V1 の悪癖だった。
            return Out::Failure(MakeError(kBooleanNoChange,
                "2つの部品は触れていないので、何も変わりません。",
                "重なるように置いてから、もう一度ためしてください。"));
        }
        BooleanBuildResult built;
        built.handle = StoreShape(made);
        built.volumeMm3 = after;
        built.previousVolumeMm3 = before;
        return Out::Success(built);
    } catch (const Standard_Failure& failure) {
        return Out::Failure(MakeError(kBooleanFailed,
            "足し引きを行えませんでした。",
            std::string(failure.GetMessageString())));
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kBooleanFailed,
            "足し引きを行えませんでした。", error.what()));
    } catch (...) {
        return Out::Failure(MakeError(kBooleanFailed,
            "足し引きを行えませんでした。", "理由が分かりません。"));
    }
}

#else

Result<BooleanBuildResult> BuildBoolean(BooleanOperation operation,
    KernelShapeHandle first, KernelShapeHandle second, double toleranceMm)
{
    (void)operation;
    (void)first;
    (void)second;
    (void)toleranceMm;
    return Result<BooleanBuildResult>::Failure(MakeError(kBooleanUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

#endif // KACHACAD_V2_WITH_OCCT

} // namespace kachakacha::v2::kernel
