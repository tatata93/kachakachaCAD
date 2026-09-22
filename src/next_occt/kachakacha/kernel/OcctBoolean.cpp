#include "kachakacha/kernel/OcctBoolean.h"

#include <cmath>
#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAlgoAPI_Common.hxx>
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

//! 演算ごとに OCCT の道具で形を作る。止まったら空の形と、どこで止まったかの一言。
[[nodiscard]] TopoDS_Shape RunOperation(BooleanOperation operation, const TopoDS_Shape& base,
    const TopoDS_Shape& other, std::string& stoppedAt)
{
    if (operation == BooleanOperation::Union) {
        BRepAlgoAPI_Fuse fuse(base, other);
        fuse.Build();
        stoppedAt = fuse.IsDone() ? std::string() : "足すところで止まりました。";
        return fuse.IsDone() ? fuse.Shape() : TopoDS_Shape();
    }
    if (operation == BooleanOperation::Difference) {
        BRepAlgoAPI_Cut cut(base, other);
        cut.Build();
        stoppedAt = cut.IsDone() ? std::string() : "引くところで止まりました。";
        return cut.IsDone() ? cut.Shape() : TopoDS_Shape();
    }
    BRepAlgoAPI_Common common(base, other);
    common.Build();
    stoppedAt = common.IsDone() ? std::string() : "共通部分を求めるところで止まりました。";
    return common.IsDone() ? common.Shape() : TopoDS_Shape();
}

//! 何も残らなかったときの一言(引き切った / 重なっていない)。
[[nodiscard]] base::Diagnostic NothingLeft(BooleanOperation operation)
{
    if (operation == BooleanOperation::Intersection) {
        return MakeError(kBooleanNothingLeft, "2つの部品は重なっていないので、共通部分がありません。",
            "重なるように置いてから、もう一度ためしてください。");
    }
    return MakeError(kBooleanNothingLeft, "引き切って何も残りませんでした。",
        "相手が土台をすべて含んでいます。");
}

//! 何も変わらなかったときの一言。演算ごとに理由が違う(「触れていない」と一律に言わない)。
[[nodiscard]] base::Diagnostic NoChange(BooleanOperation operation)
{
    if (operation == BooleanOperation::Union) {
        return MakeError(kBooleanNoChange, "相手は土台の中に収まっているので、足しても何も変わりません。",
            "土台からはみ出るように置いてから、もう一度ためしてください。");
    }
    if (operation == BooleanOperation::Difference) {
        return MakeError(kBooleanNoChange, "2つの部品は重なっていないので、引いても何も変わりません。",
            "重なるように置いてから、もう一度ためしてください。");
    }
    return MakeError(kBooleanNoChange, "相手が土台をすべて含んでいるので、共通部分は土台のままです。",
        "土台からはみ出る部分がある相手を選んでください。");
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
        std::string stoppedAt;
        const TopoDS_Shape made = RunOperation(operation, base, other, stoppedAt);
        if (!stoppedAt.empty() || made.IsNull()) {
            return Out::Failure(MakeError(kBooleanFailed, "足し引きを行えませんでした。",
                stoppedAt.empty() ? std::string("形が出来ませんでした。") : stoppedAt));
        }
        const double after = VolumeOf(made);
        if (after <= 0.0) {
            // 引き切った・重ならないものを交差させた。消えた部品を持ち続けない。
            return Out::Failure(NothingLeft(operation));
        }
        const double scale = before > 0.0 ? before : 1.0;
        if (std::abs(after - before) / scale <= 1.0e-9) {
            // 何も変わらないのに「足しました」と言うのが V1 の悪癖だった。
            return Out::Failure(NoChange(operation));
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
