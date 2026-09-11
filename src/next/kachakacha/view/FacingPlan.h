#pragma once

//! 「選択に正対」の決め方(V1 の `CadViewport::AlignToSelection`、走査 §2-6)。
//!
//! V1 の「選択に正対」は、向きを変えるだけの道具ではない。3つを同時にやっていた:
//!   1. 選んだものが載っている平面へ **正対する**(姿勢)
//!   2. 選んだものを **画面の真ん中へ持ってくる**(注視点)
//!   3. 画面に **収まる大きさにする**(倍率)
//! 向きだけ変えて中身が画面の外にあると、「きいていない」ようにしか見えない。
//! V2 で向きしか変えていなかったのは、ここが抜けていたためである。
//!
//! 平面が決まらないもの(直線1本、点が一直線に並んだ線)もある。
//! そのときは V1 と同じく **いまの視線から法線を作る**。向きを勝手に変えないためである。
//!
//! 表と裏のどちらから見るかは変えない。いま見ている側に留まる(V1 と同じ)。
//! 正対のたびに裏へ回り込むと、押すたびに模型が裏返ったように見える。
//!
//! 断られかた:
//! - UI-V008 正対する先の形が空です。
//! - UI-V009 選んだものの平面が決まりません。
//! - UI-V004 その向きへは正対できません(法線の長さが0など)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/view/ViewOrientation.h"

#include <vector>

namespace kachakacha::v2::view {

inline constexpr const char* kFacingNoPoints = "UI-V008";
inline constexpr const char* kFacingNoPlane = "UI-V009";

//! 正対の仕方。姿勢・注視点・大きさの3つで1組。どれか1つだけでは足りない。
struct FacingPlan {
    //! その面を正面から見る姿勢。
    Quaternion orientation{};
    //! 画面の真ん中へ持ってくる点(選んだものの囲みの中心)。
    geometry::Vector3 center{};
    //! 画面に収めたい大きさ(mm)。囲みの縦横の大きいほう。最低 10mm。
    double spanMm = 10.0;
};

//! 点の並びに、いちばんよく載る平面の法線を推す(V1 と同じ「外積のいちばん大きい組」)。
//!
//! 一直線に並んでいると平面が決まらない。そのときは **いまの視線から** 法線を作る
//! (線と直交する向きのうち、いまの視線にいちばん近いもの)。
//! こうすると、直線を選んで正対しても、向きがほとんど変わらない。
[[nodiscard]] base::Result<geometry::Vector3> BestFitNormal(
    const std::vector<geometry::Vector3>& points,
    const geometry::Vector3& currentViewDirection);

//! 標本点と面の向きから、正対の仕方を決める。
//!
//! uAxisHint は面の上の「横」の向き。上向きはここから作る(縦 = 法線 × 横)。
//! 長さが0、または法線と平行なら、ViewOrientation の決まった逃がし方に任せる。
//! currentViewDirection は目から模型へ向かう向き。どちら側に留まるかをこれで決める。
[[nodiscard]] base::Result<FacingPlan> PlanFacingSelection(
    const std::vector<geometry::Vector3>& points, const geometry::Vector3& normal,
    const geometry::Vector3& uAxisHint, const geometry::Vector3& currentViewDirection);

} // namespace kachakacha::v2::view
