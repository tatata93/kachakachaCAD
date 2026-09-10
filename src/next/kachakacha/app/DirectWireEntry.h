#pragma once

//! 数値で線を作る(V1 の右パネル「数値で線を作る」の欄)。
//!
//! 画面でクリックせずに、座標を打って線を置く。模型では「駅の断面は x=120 の位置」
//! のように、数で決まっている線が多い。
//!
//! 平面上のものは作業平面の (u, v) mm で、3D のものは世界の (x, y, z) mm で受ける。
//! 幾何を作るのは geometry(CurveSegment / ArcBuilders)で、ここは欄の値を並べ替えて
//! 渡し、足りない・つぶれている入力を理由番号で断るだけ。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! 数値で作れる線の種類。V1 の wireKind_ と同じ並び(3Dベジェは V2 では B-spline で代える)。
enum class DirectWireKind {
    PlanarLine,      //!< 作業平面の上の直線(u1,v1)→(u2,v2)
    SpatialLine,     //!< 3D の直線(x,y,z)→(x,y,z)
    PlanarCircle,    //!< 作業平面の上の円。中心(u,v)と半径
    PlanarArc,       //!< 作業平面の上の円弧。始点・通過点・終点(u,v)
    PlanarBezier,    //!< 作業平面の上の3次ベジェ。制御点4つ(u,v)
};

[[nodiscard]] std::string_view DirectWireKindNameJa(DirectWireKind kind) noexcept;
[[nodiscard]] const std::vector<DirectWireKind>& DirectWireKinds();
//! その種類に要る点の数(2D なら (u,v) の組、3D なら (x,y,z) の組)。
[[nodiscard]] int DirectWirePointCount(DirectWireKind kind) noexcept;
[[nodiscard]] bool DirectWireIsPlanar(DirectWireKind kind) noexcept;

struct DirectWireRequest {
    DirectWireKind kind = DirectWireKind::PlanarLine;
    //! 平面上なら (u, v, 0)、3D なら (x, y, z)。要る数だけ見る。
    std::vector<geometry::Vector3> points;
    //! 円の半径 mm。
    double radiusMm = 10.0;
    //! 補助線として作るか。
    bool construction = false;
};

//! 欄の値から線を作る。作れなければ UI-D001(点が足りない)/ UI-D002(半径が正でない)、
//! または geometry の理由(つぶれた直線・一直線の3点など)で断る。
[[nodiscard]] base::Result<std::vector<geometry::CurveSegment>> BuildDirectWire(
    const DirectWireRequest& request, const modeling::WorkPlaneFrame& plane);

} // namespace kachakacha::v2::app
