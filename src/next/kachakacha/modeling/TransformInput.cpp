#include "kachakacha/modeling/TransformInput.h"

#include <cmath>

namespace kachakacha::v2::modeling {
namespace {

using base::MakeError;
using base::Result;
using geometry::Cross;
using geometry::Dot;
using geometry::Normalized;
using geometry::Vector3;

[[nodiscard]] Result<TransformPlan> Refuse(const char* code, const char* summaryJa,
    std::string detailJa)
{
    return Result<TransformPlan>::Failure({MakeError(code, summaryJa, std::move(detailJa))});
}

//! 小数を、桁を落として読める形にする。帯に出すためだけに使う。
[[nodiscard]] std::string Rounded(double value)
{
    const double snapped = std::round(value * 100.0) / 100.0;
    std::string text = std::to_string(snapped);
    while (text.size() > 1 && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

} // namespace

bool ToolIsTransform(DrawingTool tool) noexcept
{
    return tool == DrawingTool::Move || tool == DrawingTool::Copy
        || tool == DrawingTool::Mirror || tool == DrawingTool::Rotate;
}

int TransformPointCount(DrawingTool tool) noexcept
{
    switch (tool) {
    case DrawingTool::Move:
    case DrawingTool::Copy:
    case DrawingTool::Mirror:
        return 2;
    case DrawingTool::Rotate:
        return 3;
    default:
        return 0;
    }
}

Result<TransformPlan> PlanTransform(DrawingTool tool, const std::vector<Vector3>& points,
    const Vector3& planeNormal, const geometry::GeometryTolerance& tolerance)
{
    if (!ToolIsTransform(tool)) {
        return Refuse("UI-T003", "このツールは点から形を作りません。",
            std::string(DrawingToolNameJa(tool)) + " は変換の道具ではありません。");
    }
    const int required = TransformPointCount(tool);
    if (static_cast<int>(points.size()) != required) {
        return Refuse("UI-T001", "点が足りません。",
            std::string(DrawingToolNameJa(tool)) + " には " + std::to_string(required)
                + " 点が要ります。");
    }
    for (const Vector3& point : points) {
        if (!point.IsFinite()) {
            return Refuse("UI-G004", "原点に有限でない数が入っています。",
                "置いた点に数値でない値が入っています。");
        }
    }
    const Vector3 normal = Normalized(planeNormal, tolerance.numericEpsilon);
    if (normal.Length() < 0.5) {
        return Refuse("UI-X001", "作業平面の向きが決まっていません。",
            "鏡の面と回転の軸は、作業平面の法線から決めます。");
    }

    TransformPlan plan;
    if (tool == DrawingTool::Move || tool == DrawingTool::Copy) {
        const Vector3 delta = points[1] - points[0];
        if (delta.Length() <= tolerance.modelLinearMm) {
            return Refuse("UI-X002", "動かす距離がありません。",
                "1点目と2点目が同じ場所です。離れた2点を置いてください。");
        }
        plan.kind = tool == DrawingTool::Move ? TransformKind::Move : TransformKind::Copy;
        plan.vectorArgument = delta;
        plan.keepsSource = tool == DrawingTool::Copy;
        plan.summaryJa = (tool == DrawingTool::Move ? std::string("移動: ")
                                                    : std::string("コピー: "))
            + Rounded(delta.Length()) + "mm";
        return Result<TransformPlan>::Success(std::move(plan));
    }

    if (tool == DrawingTool::Mirror) {
        const Vector3 along = points[1] - points[0];
        if (along.Length() <= tolerance.modelLinearMm) {
            return Refuse("UI-X003", "鏡の線が決まりません。",
                "2点が同じ場所です。鏡にしたい線の上に、離れた2点を置いてください。");
        }
        const Vector3 mirrorNormal = Normalized(Cross(along, normal),
            tolerance.numericEpsilon);
        if (mirrorNormal.Length() < 0.5) {
            return Refuse("UI-X003", "鏡の線が決まりません。",
                "引いた線が作業平面と垂直です。平面の上に鏡の線を引いてください。");
        }
        plan.kind = TransformKind::Mirror;
        plan.vectorArgument = mirrorNormal;
        plan.pointArgument = points[0];
        plan.keepsSource = true;
        plan.summaryJa = "ミラー複製: 引いた線を鏡にします";
        return Result<TransformPlan>::Success(std::move(plan));
    }

    // 回転。中心から見た2つの向きの角度を、作業平面の上で測る。
    const Vector3 from = points[1] - points[0];
    const Vector3 to = points[2] - points[0];
    if (from.Length() <= tolerance.modelLinearMm || to.Length() <= tolerance.modelLinearMm) {
        return Refuse("UI-X004", "回す角度が決まりません。",
            "中心と同じ場所に点があります。中心から離れた2点を置いてください。");
    }
    const Vector3 unitFrom = Normalized(from, tolerance.numericEpsilon);
    const Vector3 unitTo = Normalized(to, tolerance.numericEpsilon);
    // atan2 で測る。acos だけだと、どちら回りかが分からない。
    const double angle = std::atan2(Dot(Cross(unitFrom, unitTo), normal),
        Dot(unitFrom, unitTo));
    if (std::abs(angle) <= tolerance.modelAngularRad) {
        return Refuse("UI-X004", "回す角度が決まりません。",
            "始まりと終わりの向きが同じです。");
    }
    plan.kind = TransformKind::Rotate;
    plan.vectorArgument = normal;
    plan.pointArgument = points[0];
    plan.angleRad = angle;
    plan.keepsSource = false;
    plan.summaryJa = "回転: " + Rounded(angle * 180.0 / 3.14159265358979323846) + "度";
    return Result<TransformPlan>::Success(std::move(plan));
}

} // namespace kachakacha::v2::modeling
