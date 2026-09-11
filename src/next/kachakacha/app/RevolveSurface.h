#pragma once

//! 回転体(V1 の「回転面」、走査 §2-1)。
//!
//! V1 は「断面を軸まわりに角度を割って回した写しを並べ、それをロフトする」近似で
//! 回転面を作った。V2 は面そのものを回す(形状ガイドの作り方 Revolve、核が回転面を作る)ので、
//! 断面は面の上に厳密に載る。ここは **選んだ線と欄の値を要求にする検査** と、
//! 試験・下見のための **回した断面の並び** を持つ。回転面の専用の実体は持たない
//! (展開・型紙・部品の道は形状ガイドと同じ)。
//!
//! 軸は「直線 1 本」で渡す(V1 は X/Y/Z のコンボ + 任意の点だった。V2 は線を選ぶ)。
//!
//! 断られかた:
//! - REV-E001 断面の線がありません。
//! - REV-E002 軸は直線 1 本にしてください。
//! - REV-E003 回す角度は 0 より大きく 360 以下にしてください。
//! - REV-E004 断面の数は 2〜72 にしてください。
//! - REV-E005 断面が軸の上にあります(回しても面になりません)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Vector3.h"

#include <vector>

namespace kachakacha::v2::app {

struct RevolveRequest {
    std::vector<geometry::CurveSegment> profile;
    geometry::Vector3 axisPoint{};
    geometry::Vector3 axisDirection{0.0, 0.0, 1.0};
    double angleDeg = 360.0;
    int sections = 12;
};

//! 回した断面 1 つ。
struct RevolvedSection {
    double angleRad = 0.0;
    std::vector<geometry::CurveSegment> segments;
};

//! 選んだ線(断面と軸)と欄の値から要求を作る。軸は直線 1 本の線。
[[nodiscard]] base::Result<RevolveRequest> MakeRevolveRequest(
    const std::vector<geometry::CurveSegment>& profile,
    const std::vector<geometry::CurveSegment>& axis, double angleDeg, int sections = 12);

//! 断面を回した写しを、角度の小さい順に並べる(元の断面は含まない)。
//! 角度は angleDeg × i / sections(i = 1..sections)。360° なら最後の写しは元と重なる
//! (閉じたロフトの端になる)。
[[nodiscard]] base::Result<std::vector<RevolvedSection>> BuildRevolvedSections(
    const RevolveRequest& request, double toleranceMm);

} // namespace kachakacha::v2::app
