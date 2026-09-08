#pragma once

//! ワイヤー編集(geometry-contract §5、およびV1同等性の要件)。
//! トリム、延長、面取り、丸め、オフセット、交点、移動・複製・回転・ミラー。
//!
//! 共通の約束: 元の曲線種類を保つ。近似の折れ線で代用しない。
//! できないものは診断コードつきで断る。

#include "kachakacha/geometry/CurveSegment.h"

#include <utility>
#include <vector>

namespace kachakacha::v2::geometry {

// ---- 交差 ----
//
// 交差そのものは geometry/CurveIntersection.h が持つ。
// ここに置いていた同名の struct は、そちらと名前がぶつかっていたので取り除いた。
// 編集(トリム・延長)の中で使う簡易版だけを、別の名前で残す。

struct EditIntersection {
    double parameterA = 0.0;
    double parameterB = 0.0;
    Vector3 point{};
};

//! 編集で使う3D交点。接触や重なりは1点にまとめる。
//! 画面上だけの交差を見分けたい場合は geometry/CurveIntersection.h を使う。
[[nodiscard]] std::vector<EditIntersection> IntersectCurvesForEditing(
    const CurveSegment& a, const CurveSegment& b, double toleranceMm);

// ---- 延長 ----

//! 端(0=始点側 1=終点側)を距離ぶん延ばす。種類を保つ。
//! Circle と閉じたWireは延長できない。
[[nodiscard]] base::Result<CurveSegment> ExtendCurve(const CurveSegment& curve,
    int endpointIndex, double distanceMm);

//! 相手の曲線と最初に当たるところまで延ばす。
[[nodiscard]] base::Result<CurveSegment> ExtendCurveToBoundary(const CurveSegment& curve,
    int endpointIndex, const CurveSegment& boundary, double toleranceMm);

// ---- トリム ----

//! 境界で切って、クリック位置を含む区間を捨てる。
[[nodiscard]] base::Result<CurveSegment> TrimCurve(const CurveSegment& curve,
    const CurveSegment& boundary, double clickParameter, double toleranceMm);

// ---- 角の加工 ----

struct CornerResult {
    CurveSegment first;   //!< 加工後の1本目(短くなる)
    CurveSegment corner;  //!< 面取りの線、または丸めの円弧
    CurveSegment second;  //!< 加工後の2本目
};

//! C面取り。2本の直線の角を、指定した切戻し量で落とす。
//! 離れている線は交点まで自動で延ばしてから落とす(V1と同じ挙動)。
[[nodiscard]] base::Result<CornerResult> ChamferLines(const CurveSegment& first,
    const CurveSegment& second, double setbackMm, double toleranceMm);

//! R面取り(丸め)。2本の直線の角を、指定した半径の円弧で丸める。
[[nodiscard]] base::Result<CornerResult> FilletLines(const CurveSegment& first,
    const CurveSegment& second, double radiusMm, double toleranceMm);

//! 2本の直線を、互いの交点まで延ばす(または縮める)。V1の「2線を交点まで」。
[[nodiscard]] base::Result<std::pair<CurveSegment, CurveSegment>> MeetLines(
    const CurveSegment& first, const CurveSegment& second, double toleranceMm);

// ---- オフセット ----

//! 平面上の曲線を、面内で距離ぶん平行移動する。直線と円弧は種類を保つ。
[[nodiscard]] base::Result<CurveSegment> OffsetCurveInPlane(const CurveSegment& curve,
    Vector3 planeNormal, double distanceMm);

// ---- 変換 ----

[[nodiscard]] CurveSegment TranslateCurve(const CurveSegment& curve, Vector3 delta);

//! 向きを逆にする。種類を保つ。折れ線へ落とさない。
//!
//! 面を1周する順に並べた辺は、そのままでは向きがそろっていないことがある。
//! そこで逆にする必要が出るが、そのたびに折れ線へ落としては形が変わってしまう。
[[nodiscard]] base::Result<CurveSegment> ReverseCurve(const CurveSegment& curve);

//! 軸まわりの回転。軸は点と方向で与える。
[[nodiscard]] base::Result<CurveSegment> RotateCurve(const CurveSegment& curve,
    Vector3 axisPoint, Vector3 axisDirection, double angleRad);

//! 平面での鏡映。平面は点と法線で与える。
[[nodiscard]] base::Result<CurveSegment> MirrorCurve(const CurveSegment& curve,
    Vector3 planePoint, Vector3 planeNormal);

} // namespace kachakacha::v2::geometry
