#pragma once

//! 曲線どうしの交差(geometry-contract §4)。
//!
//! 大事な区別が2つある。
//!   実3D交点  : 2曲線が本当に同じ位置を通る。距離が modelLinearMm 以下。
//!   画面交差  : 見た目だけ重なっている。手前と奥で離れている。
//! 後者を既定のスナップ候補にしてはならない。V1はここを分けていなかったので、
//! 「画面では線が交わっているのに、点を打つと奥の別の場所へ飛ぶ」が起きた。

#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/ScreenMapping.h"

#include <optional>
#include <vector>

namespace kachakacha::v2::geometry {

struct CurveIntersection {
    Vector3 position{};        //!< 2曲線の最近点の中点
    double firstParameter = 0.0;
    double secondParameter = 0.0;
    double gapMm = 0.0;        //!< 2曲線の隙間。実3D交点なら許容差以下
    bool real = true;          //!< false なら画面上だけの交差
};

//! 3Dでの交差候補。実交点だけを返す。
[[nodiscard]] std::vector<CurveIntersection> IntersectCurves(const CurveSegment& first,
    const CurveSegment& second, const GeometryTolerance& tolerance);

//! 画面上で重なって見える場所も含めて返す。real で見分ける。
[[nodiscard]] std::vector<CurveIntersection> IntersectCurvesOnScreen(
    const CurveSegment& first, const CurveSegment& second, const ScreenMapping& mapping,
    double screenToleranceMm, const GeometryTolerance& tolerance);

//! 曲線上で、指定した点にいちばん近い位置。
[[nodiscard]] ClosestPointResult ClosestOnCurve(const CurveSegment& segment,
    const Vector3& point);

//! 画面の1点から見て、曲線がいちばん近く見えるところ。
struct CurveScreenApproach {
    double distancePx = 0.0;   //!< 画面上の距離(logical px)
    double parameter = 0.0;    //!< 曲線のパラメータ
    Vector3 point{};           //!< 曲線上の3D座標。Evaluate(parameter) と同じ
};

//! ApproachToCurveOnScreen が返す距離の精度(logical px)。
inline constexpr double kCurveScreenApproachTolerancePx = 1.0e-4;

//! 画面の1点から見て曲線がいちばん近く見えるところ。maximumDistancePx(logical px)より遠ければ返さない。
//!
//! 画面空間の分枝限定で探す。標本の弦誤差や、区間の中で距離が1つの谷になることは仮定しない。
//! 曲線を区間に分け、区間ごとに曲線を必ず囲む凸包(直線は両端、掃引90度以下の円弧は両端と
//! 接線の交点、Bezier・B-splineは区間の制御点)を画面へ写す。視点の前の凸集合は投影で凸集合へ
//! 写るので、写した凸包までの距離はその区間の曲線までの画面距離の下限になる。下限の小さい区間から
//! 二分し、いちばん小さい下限が、実際に曲線上の点を写して測った最良の距離と
//! kCurveScreenApproachTolerancePx 以内になったら止める。
//!
//! 保証: 返す距離は真の最短画面距離以上、真の最短 + kCurveScreenApproachTolerancePx 以下。
//! 真の最短が maximumDistancePx 以下なら、返さないのはこの許容差ぶんの境目だけである。
//! 距離・パラメータ・点は同じ位置を指す。凸包ごとカメラの後ろにある区間は捨てる(どの点も写らない)。
//! 凸包がカメラの後ろにかかる区間は下限を0と見なして割り続け、
//! 分割の深さ(52段)か回数(8192回)の上限に達した区間は、それまでに写した点の実距離だけで評価する。
[[nodiscard]] std::optional<CurveScreenApproach> ApproachToCurveOnScreen(
    const CurveSegment& segment, const ScreenMapping& mapping, const ScreenPoint& pointer,
    double maximumDistancePx);

//! 曲線が平面の中にあるか。どの点も平面から toleranceMm 以内であること。
//! 選択の「作図面の上か」(app::CurveLiesOnPlane)とスナップが共有する唯一の判定である。
//! 円・円弧は中心のずれと傾きによるはみ出しの和、Bezier・B-splineは制御点で判定する
//! (曲線は制御点の凸包の中にある)。両端と真ん中だけで見ると、途中で浮く3次曲線を見逃す。
[[nodiscard]] bool CurveLiesInPlane(const CurveSegment& segment, const Vector3& planeOrigin,
    const Vector3& planeNormal, double toleranceMm);

//! 曲線の四半点(円・円弧は90度ごと、それ以外は 1/4 ごとの位置)。
[[nodiscard]] std::vector<Vector3> QuadrantPoints(const CurveSegment& segment);

//! 直線の延長線上で、指定した点にいちばん近い位置。
//! 曲線が延長できない種類(円、閉じたもの)なら値を返さない。
[[nodiscard]] std::optional<Vector3> ExtensionPoint(const CurveSegment& segment,
    const Vector3& point, double maximumExtensionMm);

//! 指定した点から曲線へ下ろした垂線の足。接線と垂直になる位置。
[[nodiscard]] std::vector<Vector3> PerpendicularFeet(const CurveSegment& segment,
    const Vector3& point, const GeometryTolerance& tolerance);

//! 指定した点から曲線へ引いた接線の接点。
[[nodiscard]] std::vector<Vector3> TangentPoints(const CurveSegment& segment,
    const Vector3& point, const GeometryTolerance& tolerance);

} // namespace kachakacha::v2::geometry
