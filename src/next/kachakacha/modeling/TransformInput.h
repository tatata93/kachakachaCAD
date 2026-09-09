#pragma once

//! 移動・複製・鏡映・回転が、置いた点から何を意味するかを決める(v1-input-parity.md §3)。
//!
//! V2ではこの4つの道具が道具箱に並んでいたのに、点を集めるだけで何も起きなかった。
//! 形を変える計算そのものは `geometry::TranslateCurve` などが既に持っている。
//! 足りなかったのは「置いた2点や3点が、どの移動量・どの鏡・どの角度なのか」を
//! 決めるところだけである。ここに置く。画面側は、この答えを渡すだけにする。
//!
//! 作業平面の法線を渡すのは、鏡の面と回転の軸がそれで決まるからである。
//! V1は画面の奥行き方向を使っていたので、視点を回すと鏡の向きが変わってしまった。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/ToolController.h"

#include <string>
#include <vector>

namespace kachakacha::v2::modeling {

//! 4つの道具のどれか。domain の WireTransformMethod へは画面側で写す。
//! ここで domain を見ないのは、modeling が文書の形を知らずに済むようにするため。
enum class TransformKind {
    Move,
    Copy,
    Mirror,
    Rotate,
};

//! 置いた点から読み取った、変換1回ぶんの中身。
struct TransformPlan {
    TransformKind kind = TransformKind::Move;
    //! 移動量、鏡の面の法線、回転の軸方向。道具によって意味が変わる。
    geometry::Vector3 vectorArgument{};
    //! 鏡の面上の点、回転の軸上の点。移動と複製では使わない。
    geometry::Vector3 pointArgument{};
    //! 回転角(ラジアン)。他の道具では 0。
    double angleRad = 0.0;
    //! 元の線を残すか。複製と鏡映は残す。移動と回転は残さない。
    bool keepsSource = false;
    //! 帯に出す一文。「20.0mm 動かします」など。
    std::string summaryJa;
};

//! その道具が、選んだ線を変換する道具か。
[[nodiscard]] bool ToolIsTransform(DrawingTool tool) noexcept;

//! 道具に要る点の数。変換の道具でなければ 0。
[[nodiscard]] int TransformPointCount(DrawingTool tool) noexcept;

//! 置いた点から中身を決める。決まらなければ断る。degrade しない。
//!
//! - 移動・複製: 2点。1点目から2点目への差が移動量。
//! - 鏡映: 2点。その2点を通る線が鏡。面の法線は線と作業平面法線の外積。
//! - 回転: 3点。1点目が中心、2点目が始まりの向き、3点目が終わりの向き。
[[nodiscard]] base::Result<TransformPlan> PlanTransform(DrawingTool tool,
    const std::vector<geometry::Vector3>& points, const geometry::Vector3& planeNormal,
    const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::modeling
