#include "kachakacha/kernel/OcctBoolean.h"

#include <cmath>
#include <algorithm>
#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/kernel/OcctGuideSurface.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepGProp.hxx>
#include "kachakacha/kernel/OcctMassProperties.h"
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>
#include <TopTools_ListOfShape.hxx>

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
    detail::VolumeProperties(shape, properties);
    return properties.Mass();
}

template<class Operation>
TopoDS_Shape NonDestructiveOperation(const TopoDS_Shape& base,const TopoDS_Shape& other,
    std::string& stoppedAt,const char* failure)
{
    TopTools_ListOfShape arguments,tools;arguments.Append(base);tools.Append(other);
    Operation operation;operation.SetArguments(arguments);operation.SetTools(tools);
    operation.SetNonDestructive(true);operation.Build();
    if(operation.IsDone())operation.SimplifyResult(true,true);
    stoppedAt=operation.IsDone()?std::string():failure;
    return operation.IsDone()?operation.Shape():TopoDS_Shape();
}

//! 演算ごとに OCCT の道具で形を作る。止まったら空の形と、どこで止まったかの一言。
[[nodiscard]] TopoDS_Shape RunOperation(BooleanOperation operation, const TopoDS_Shape& base,
    const TopoDS_Shape& other, std::string& stoppedAt)
{
    if (operation == BooleanOperation::Union) {
        return NonDestructiveOperation<BRepAlgoAPI_Fuse>(base,other,stoppedAt,"足すところで止まりました。");
    }
    if (operation == BooleanOperation::Difference) {
        return NonDestructiveOperation<BRepAlgoAPI_Cut>(base,other,stoppedAt,"引くところで止まりました。");
    }
    return NonDestructiveOperation<BRepAlgoAPI_Common>(base,other,stoppedAt,"共通部分を求めるところで止まりました。");
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
        const double added = VolumeOf(other);
        if (!std::isfinite(before) || !std::isfinite(added) || before <= 0 || added <= 0) {
            return Out::Failure(MakeError(kBooleanFailed,
                "足し引きの入力が正しい向きの立体ではありません。",
                "入力部品の閉じ方と法線を確認してください。"));
        }
        std::string stoppedAt;
        const TopoDS_Shape made = RunOperation(operation, base, other, stoppedAt);
        if (!stoppedAt.empty() || made.IsNull()) {
            return Out::Failure(MakeError(kBooleanFailed, "足し引きを行えませんでした。",
                stoppedAt.empty() ? std::string("形が出来ませんでした。") : stoppedAt));
        }
        const double after = VolumeOf(made);
        const double volumeTolerance = std::max(std::pow(std::max(toleranceMm, 1e-6), 3.0),
            std::max(before, added) * 1e-7);
        if (!std::isfinite(after) || after < -volumeTolerance
            || (operation == BooleanOperation::Union
                && (after < std::max(before, added) - volumeTolerance
                    || after > before + added + volumeTolerance))
            || (operation == BooleanOperation::Difference && after > before + volumeTolerance)
            || (operation == BooleanOperation::Intersection
                && after > std::min(before, added) + volumeTolerance)) {
            return Out::Failure(MakeError(kBooleanFailed,
                "足し引きの結果の体積が入力と矛盾するため、変更しませんでした。",
                "接合部の隙間・重なり・自己交差を確認してください。"));
        }
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
        const auto checked = CheckSolidForExport(built.handle, toleranceMm);
        if (!checked.HasValue() || !checked.Value().Ok()) {
            ReleaseShape(built.handle);
            return Out::Failure(MakeError(kBooleanFailed,
                "足し引きの結果が閉じた正しい立体にならないため、変更しませんでした。",
                "接合部の隙間・重なり・自己交差を確認してください。"));
        }
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

Result<BooleanBuildResult> BuildBooleans(BooleanOperation operation,
    KernelShapeHandle first, const std::vector<KernelShapeHandle>& tools, double toleranceMm)
{
    using Out = Result<BooleanBuildResult>;
    if (tools.empty()) return Out::Failure(MakeError(kBooleanTargetMissing, "相手の部品がありません。", {}));
    if (tools.size() == 1) return BuildBoolean(operation, first, tools.front(), toleranceMm);
    if (operation != BooleanOperation::Union) {
        auto current = first;
        double before = -1;
        BooleanBuildResult result;
        for (const auto tool : tools) {
            const auto next = BuildBoolean(operation, current, tool, toleranceMm);
            if (current.value != first.value) ReleaseShape(current);
            if (!next.HasValue()) return next;
            result = next.Value();
            if (before < 0) before = result.previousVolumeMm3;
            current = result.handle;
        }
        result.previousVolumeMm3 = before;
        return Out::Success(result);
    }
    try {
        TopoDS_Shape base;
        if (!LookupShape(first, base)) return Out::Failure(MakeError(kBooleanTargetMissing, "土台の部品がありません。", {}));
        const double before = VolumeOf(base);
        double sum = before, largest = before;
        if (!std::isfinite(before) || before <= 0) return Out::Failure(MakeError(kBooleanFailed, "土台の体積が不正です。", {}));
        TopTools_ListOfShape arguments, others;
        arguments.Append(base);
        for (const auto handle : tools) {
            TopoDS_Shape shape;
            if (!LookupShape(handle, shape)) return Out::Failure(MakeError(kBooleanTargetMissing, "相手の部品がありません。", {}));
            const double volume = VolumeOf(shape);
            if (!std::isfinite(volume) || volume <= 0) return Out::Failure(MakeError(kBooleanFailed, "相手の体積が不正です。", {}));
            sum += volume;
            largest = std::max(largest, volume);
            others.Append(shape);
        }
        BRepAlgoAPI_Fuse fuse;
        fuse.SetArguments(arguments);
        fuse.SetTools(others);
        fuse.SetNonDestructive(true);
        fuse.Build();
        if (!fuse.IsDone()) return Out::Failure(MakeError(kBooleanFailed, "部品を足す処理に失敗しました。", {}));
        fuse.SimplifyResult(true, true);
        const double after = VolumeOf(fuse.Shape());
        const double epsilon = std::max(std::pow(std::max(toleranceMm, 1e-6), 3.0), largest * 1e-7);
        if (!std::isfinite(after) || after < largest - epsilon || after > sum + epsilon)
            return Out::Failure(MakeError(kBooleanFailed, "結合後の体積が入力と矛盾します。", {}));
        if (std::abs(after - before) / before <= 1e-9) return Out::Failure(NoChange(operation));
        const auto handle = StoreShape(fuse.Shape());
        const auto checked = CheckSolidForExport(handle, toleranceMm);
        if (!checked.HasValue() || !checked.Value().Ok()) {
            ReleaseShape(handle);
            return Out::Failure(MakeError(kBooleanFailed, "結合後の閉鎖性・法線・自己交差の検査に失敗しました。", {}));
        }
        return Out::Success(BooleanBuildResult{handle, after, before});
    } catch (const Standard_Failure& error) {
        return Out::Failure(MakeError(kBooleanFailed, "部品を足す処理に失敗しました。", error.GetMessageString()));
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kBooleanFailed, "部品を足す処理に失敗しました。", error.what()));
    } catch (...) {
        return Out::Failure(MakeError(kBooleanFailed, "部品を足す処理に失敗しました。", {}));
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

Result<BooleanBuildResult> BuildBooleans(BooleanOperation operation,
    KernelShapeHandle first, const std::vector<KernelShapeHandle>& tools, double toleranceMm)
{
    return BuildBoolean(operation, first, tools.empty() ? KernelShapeHandle{} : tools.front(), toleranceMm);
}

#endif // KACHACAD_V2_WITH_OCCT

} // namespace kachakacha::v2::kernel
