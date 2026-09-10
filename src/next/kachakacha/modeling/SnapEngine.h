#pragma once

//! スナップ(ui-workflows.md §6、v1-drawing-parity.md §4)。
//!
//! V1が持っていた8種を全部持つ。V2の仕様から漏れていた2種
//! (延長線上、作業平面へ法線投影)もここへ入れる。オーナー指示により必須。
//!
//! 優先順位より画面距離を優先する場面がある。
//! 高優先の候補は6px、低優先は8pxまでを有効とし、
//! 「グリッドが端点を奪う」ことが起きないようにする(§6.1)。
//!
//! Qt に依存しない。画面の情報は ScreenMapping 1枚だけを受け取る。

#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/ScreenMapping.h"

#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::modeling {

using base::EntityId;
using base::SegmentId;
using geometry::CurveSegment;
using geometry::GeometryTolerance;
using geometry::ScreenMapping;
using geometry::ScreenPoint;
using geometry::Vector3;

//! 候補の種類。並びがそのまま既定の優先順位(小さいほど強い)。
enum class SnapKind {
    Intersection,      //!< 実3D交点
    Endpoint,          //!< 端点
    Center,            //!< 円・円弧の中心
    DrawingPoint,      //!< 作図点
    Tangent,           //!< 接点
    Perpendicular,     //!< 垂足
    Midpoint,          //!< 中点
    Quadrant,          //!< 四半点
    Extension,         //!< 既存線分の延長線上(V1にあった)
    ProjectedOnPlane,  //!< 作業平面へ法線投影した点(V1にあった)
    ClosestOnCurve,    //!< 曲線上の最近点
    GridMajor,         //!< 主グリッド点
    GridMinor,         //!< 副グリッド点
    FreeOnPlane,       //!< 作業平面上の自由点
    ScreenIntersection,//!< 画面上だけの交差。既定では選ばない
};

[[nodiscard]] std::string_view SnapKindLabelJa(SnapKind kind) noexcept;

//! 高優先(6px)か低優先(8px)か。
[[nodiscard]] bool IsHighPrioritySnap(SnapKind kind) noexcept;

struct SnapCandidate {
    SnapKind kind = SnapKind::FreeOnPlane;
    Vector3 position{};
    double screenDistancePx = 0.0;
    EntityId entityId;
    SegmentId segmentId;
    //! 交点のときの相手。
    EntityId otherEntityId;
    SegmentId otherSegmentId;
};

//! 場面。曲線と作図点と、グリッドと作業平面。
struct SnapCurve {
    EntityId entityId;
    SegmentId segmentId;
    CurveSegment segment;
    bool construction = false;
    //! 基準線(V1 の「基準線に設定」)。一点鎖線で出す。形は同じ。
    bool datum = false;
};

struct SnapDrawingPoint {
    EntityId entityId;
    Vector3 position{};
};

struct SnapGrid {
    bool visible = false;
    Vector3 origin{};
    Vector3 uDirection{1.0, 0.0, 0.0};
    Vector3 vDirection{0.0, 1.0, 0.0};
    double majorSpacingMm = 10.0;
    //! 0 = 副点なし、2 = 1/2、3 = 1/3、4 = 1/4。
    int subdivision = 0;
};

struct SnapWorkPlane {
    bool active = false;
    Vector3 origin{};
    Vector3 normal{0.0, 0.0, 1.0};
};

struct SnapScene {
    std::vector<SnapCurve> curves;
    std::vector<SnapDrawingPoint> points;
    SnapGrid grid;
    SnapWorkPlane workPlane;
};

struct SnapSettings {
    double highPriorityRadiusPx = 6.0;
    double lowPriorityRadiusPx = 8.0;
    //! 修飾キーで全スナップを止める。V1は Ctrl、V2仕様は Shift。
    //! どちらでも止まるように、呼び出し側で押されていれば true にする。
    bool suppressed = false;
    //! 副グリッド点は画面間隔が6pxを下回ると出さない(§6.3)。
    double minimumGridSpacingPx = 6.0;
    //! 延長線をどこまで伸ばして拾うか。
    double maximumExtensionMm = 1000.0;
    //! 接点・垂足を出すときの基準点(直前に置いた点)。無ければ出さない。
    std::optional<Vector3> referencePoint;
};

//! 候補をすべて集める。並びは決定的。
[[nodiscard]] std::vector<SnapCandidate> CollectSnapCandidates(const SnapScene& scene,
    const ScreenMapping& mapping, const ScreenPoint& pointer, const SnapSettings& settings,
    const GeometryTolerance& tolerance);

//! 集めた候補から1つ選ぶ。§6.1 の順位と、6px/8px の使い分けに従う。
[[nodiscard]] std::optional<SnapCandidate> ChooseSnap(
    const std::vector<SnapCandidate>& candidates, const SnapSettings& settings);

} // namespace kachakacha::v2::modeling
