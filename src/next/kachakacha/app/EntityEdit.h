#pragma once

//! 選んだものの数値編集(V1 の「編集」タブ、走査 §1-15)。
//!
//! V1 では、選んだ作業平面の 原点/法線/平面内X、選んだ線の 各点/中心/軸/半径/角度 を
//! 欄に出し、「変更を適用」で置き換えた。ここはその「欄 ↔ 定義」の往復だけを持つ。
//! 画面(Qt)は欄を並べて値を運ぶだけで、何が作れて何が作れないかはここが決める。
//!
//! 決まり:
//! - 原点の基準平面(top_XY など)は数値で変えられない(UI-E002)。
//! - 線の種類は保つ。直線は直線、円弧は円弧のまま値を差し替える。
//!   種類が混ざったワイヤー(直線と円弧の並びなど)は欄に出せないので断る(UI-E003)。
//! - 直線の「長さ」「平面内角度」は V1 の拘束ではなく、その場で終点を置き直す。
//!   V2 には拘束の仕組みが無いので、「固定」と言わずに「置き直す」と言う。
//! - つぶれる形(長さ0、半径0)は CurveSegment の作り方が断る。ここで丸めない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <optional>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! 欄に出す線の形。
enum class WireEditShape {
    Line,        //!< 直線1本(点2つ)
    Polyline,    //!< 直線の並び(点 N+1 個)
    CubicBezier, //!< 制御点4つ
    CubicBSpline,//!< 制御点 N 個
    Circle,      //!< 中心・軸・半径
    CircularArc, //!< 中心・軸・半径・開始角・中心角
};

[[nodiscard]] std::string_view WireEditShapeNameJa(WireEditShape shape) noexcept;

//! 作業平面の欄。
struct PlaneEditFields {
    geometry::Vector3 origin{};
    geometry::Vector3 normal{0.0, 0.0, 1.0};
    geometry::Vector3 uDirection{1.0, 0.0, 0.0};
};

//! 線の欄。形によって使う欄が違う(使わない欄は無視する)。
struct WireEditFields {
    WireEditShape shape = WireEditShape::Line;
    //! Line / Polyline / CubicBezier / CubicBSpline の点。
    std::vector<geometry::Vector3> points;
    //! Circle / CircularArc。
    geometry::Vector3 center{};
    geometry::Vector3 uAxis{1.0, 0.0, 0.0};
    geometry::Vector3 vAxis{0.0, 1.0, 0.0};
    double radiusMm = 0.0;
    double startAngleDeg = 0.0;
    double sweepAngleDeg = 0.0;
    //! Line だけ。入れると始点を動かさずに終点を置き直す。
    std::optional<double> lengthMm;
    std::optional<double> angleDeg;
    bool construction = false;
    std::optional<base::EntityId> sourcePlaneId;
};

//! いまの定義から欄を作る。種類が混ざっていれば UI-E003。
[[nodiscard]] base::Result<WireEditFields> WireEditFieldsOf(
    const domain::CreateWireDefinition& definition);

//! 欄から線の並びを作る。angleFrame は「平面内角度」の 0° 方向(u軸)と 90° 方向(v軸)。
//! 作れない形は理由つきで断る(CurveSegment の作り方の理由をそのまま通す)。
[[nodiscard]] base::Result<std::vector<geometry::CurveSegment>> BuildWireFromEditFields(
    const WireEditFields& fields, const modeling::WorkPlaneFrame& angleFrame);

//! 欄から定義を作り直す。線の ID(segmentIds)は本数が同じなら保ち、増減したら ids で振る。
[[nodiscard]] base::Result<domain::CreateWireDefinition> EditedWireDefinition(
    const domain::CreateWireDefinition& current, const WireEditFields& fields,
    const modeling::WorkPlaneFrame& angleFrame, base::IdGenerator& ids);

//! いまの平面の定義から欄を作る。
[[nodiscard]] PlaneEditFields PlaneEditFieldsOf(
    const domain::CreateWorkPlaneDefinition& definition);

//! 欄から平面の定義を作り直す。作り方は「位置と向きを数値で」(PointNormal)になる。
//! 原点の基準平面は UI-E002 で断る。法線と u 軸が平行なら core の平面の作り方が断る。
[[nodiscard]] base::Result<domain::CreateWorkPlaneDefinition> EditedPlaneDefinition(
    const domain::CreateWorkPlaneDefinition& current, const PlaneEditFields& fields,
    const geometry::GeometryTolerance& tolerance);

//! 直線の長さと平面内角度(いまの値)。欄の初期値に使う。
struct LineMeasure {
    double lengthMm = 0.0;
    double angleDeg = 0.0;
};
[[nodiscard]] LineMeasure MeasureLine(const geometry::Vector3& start,
    const geometry::Vector3& end, const modeling::WorkPlaneFrame& angleFrame);

//! 「数値で編集できるものを1つ選んでください」(UI-E001)。選択の数が 1 でないときに出す。
[[nodiscard]] base::Diagnostic NothingToEditDiagnostic(std::size_t selectedCount);

} // namespace kachakacha::v2::app
