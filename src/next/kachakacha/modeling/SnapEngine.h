#pragma once

//! スナップ(ui-ux-integrated-spec.md §6.1、v1-drawing-parity.md §4)。
//!
//! V1が持っていた8種を全部持つ。V2の仕様から漏れていた2種
//! (延長線上、作業平面へ法線投影)もここへ入れる。オーナー指示により必須。
//!
//! GeometryTolerance::snapPickPx(logical px)は通常候補の吸着半径である。
//! 候補どうしは §6.1 の順位と画面距離で比べる。
//! 現在ツールに適合する候補の強さ(§6.1)と、入力中ツールが要求する型の最優先
//! (§4.3 の1、product-contract.md PRD-060)はまだ持たない。
//! 強さの数値規則が仕様に無く、ツールが要求する型を渡す入力も無いためである。
//! ポインタの小さな揺れで候補が入れ替わり続けないよう、
//! 直前に選んだ候補を持ち越す(SnapHysteresis)。
//!
//! Qt に依存しない。画面の情報は ScreenMapping 1枚だけを受け取る。

#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/ScreenMapping.h"

#include <cstdint>
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

//! 候補の種類。並びは §6.1 の優先順位に沿う。
//! 同じ順位の種類があるので、順位そのものは SnapPriorityRank で見る。
enum class SnapKind {
    Endpoint,          //!< 端点
    DrawingPoint,      //!< 作図点
    Intersection,      //!< 実3D交点
    Midpoint,          //!< 中点
    Center,            //!< 円・円弧の中心
    Quadrant,          //!< 四半点
    ClosestOnCurve,    //!< 曲線上の最近点
    Perpendicular,     //!< 垂足
    Tangent,           //!< 接点
    Extension,         //!< 既存線分の延長線上(V1にあった)
    ProjectedOnPlane,  //!< 作業平面へ法線投影した点(V1にあった)
    GridMajor,         //!< 主グリッド点
    GridMinor,         //!< 副グリッド点
    FreeOnPlane,       //!< 作業平面上の自由点
    ScreenIntersection,//!< 画面上だけの交差。既定では選ばない
};

[[nodiscard]] std::string_view SnapKindLabelJa(SnapKind kind) noexcept;

//! §6.1 の順位。小さいほど強い。同じ値なら画面距離の近いほうを採る。
//!
//! 1. 端点、作図点 2. 交点 3. 中点、中心、四分点 4. 曲線上の最近点 5. 垂足、接点 6. グリッド。
//! 例外は置かない。同じ曲線の最近点は垂足・接点と同じか近いので、§6.1 のままでは
//! 垂足・接点が選ばれる場面が無い(オーナー判断待ち。v1-drawing-parity.md §4)。
//! §6.1 に無い延長線と平面へ投影は、仕様に順位が無いため暫定で 5 と 6 の間に置く
//! (オーナー判断待ち。同上)。平面上の自由点は最後の受け皿、画面交差は選ばない。
[[nodiscard]] int SnapPriorityRank(SnapKind kind) noexcept;

struct SnapCandidate {
    SnapKind kind = SnapKind::FreeOnPlane;
    Vector3 position{};
    double screenDistancePx = 0.0;
    //! 候補を出した形。作図点・端点・中点・中心・四半点・最近点・垂足・接点・延長線・
    //! 平面へ投影では出どころ、交点では ID の小さいほうの曲線。グリッドと自由点では空。
    EntityId entityId;
    SegmentId segmentId;
    //! 交点のときの相手(ID の大きいほうの曲線)。
    EntityId otherEntityId;
    SegmentId otherSegmentId;
    //! 同じ形・同じ種類の中で吸着先を見分ける番号。
    //! 端点と、端点を平面へ投影した点は 0=始点・1=終点。四半点・垂足・接点は出てきた順の番号。
    //! 交点は同じ2曲線の交差を entityId 側の曲線のパラメータ順に並べた番号。
    //! グリッドは副点の間隔で数えた格子の u 番号(副点を出していなくても同じ数え方)。
    std::int64_t featureIndex = 0;
    //! グリッドの格子の v 番号。ほかの種類では 0。
    std::int64_t latticeV = 0;
    //! 直前に選んだ候補を持ち越したもの。持ち越し中は手放すまで半径が広い。
    bool held = false;
};

//! 吸着先の同一性。候補の位置や距離は含まない。
//!
//! 曲線上の最近点と延長線はポインタに付いて位置が動くが、種類と曲線で同じ吸着先と見る。
//! それ以外も位置では見ない。別の形の端点が近くに(または同じ位置に)あっても別の吸着先である。
//! 位置で見ると、持ち越しが近くの別の形へ移り、その形の ID が文書の参照へ入ってしまう。
struct SnapTargetKey {
    SnapKind kind = SnapKind::FreeOnPlane;
    EntityId entityId;
    SegmentId segmentId;
    EntityId otherEntityId;
    SegmentId otherSegmentId;
    std::int64_t featureIndex = 0;
    std::int64_t latticeV = 0;
};

[[nodiscard]] SnapTargetKey TargetKeyOf(const SnapCandidate& candidate) noexcept;
[[nodiscard]] bool operator==(const SnapTargetKey& first, const SnapTargetKey& second) noexcept;
[[nodiscard]] inline bool operator!=(const SnapTargetKey& first,
    const SnapTargetKey& second) noexcept
{
    return !(first == second);
}
//! 並びを決めるための全順序。種類 → entityId → segmentId → 相手 → 番号。
[[nodiscard]] bool operator<(const SnapTargetKey& first, const SnapTargetKey& second) noexcept;

//! 2つの候補が同じ吸着先を指しているか(SnapTargetKey が等しいか)。
//! 候補をまとめるときも、持ち越しを見分けるときも、これ1つを使う。
[[nodiscard]] bool SameSnapTarget(const SnapCandidate& first,
    const SnapCandidate& second) noexcept;

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
    //! S キーを押している間、全スナップを止める。
    bool suppressed = false;
    //! 副グリッド点は画面間隔が6pxを下回ると出さない(§6.3)。
    double minimumGridSpacingPx = 6.0;
    //! 延長線をどこまで伸ばして拾うか。
    double maximumExtensionMm = 1000.0;
    //! 接点・垂足を出すときの基準点(直前に置いた点)。無ければ出さない。
    std::optional<Vector3> referencePoint;
    //! 直前に選んだ候補。あれば、それと同じ吸着先(SameSnapTarget)だけは
    //! snapPickPx + holdMarginPx まで拾い続け、同じ順位の候補へは holdMarginPx より
    //! 明らかに近いときだけ乗り換える。
    std::optional<SnapCandidate> heldSnap;
    //! 持ち越しの余裕(logical px)。手の震えで境目を行き来しても入れ替わらない幅。
    double holdMarginPx = 4.0;
};

//! 候補をすべて集める。並びは決定的(順位 → 画面距離 → SnapTargetKey)で、場面の並び順に依らない。
//! 範囲は tolerance.snapPickPx。settings.heldSnap と同じ吸着先だけは余裕を足す。
//! 同じ吸着先の候補は1つにまとめる。位置が同じでも吸着先が違えば別に残す。
[[nodiscard]] std::vector<SnapCandidate> CollectSnapCandidates(const SnapScene& scene,
    const ScreenMapping& mapping, const ScreenPoint& pointer, const SnapSettings& settings,
    const GeometryTolerance& tolerance);

//! 集めた候補から1つ選ぶ。§6.1 の順位に従い、持ち越した候補があれば
//! 順位が上の候補が現れるか、同じ順位で明らかに近い候補が現れるまで保つ。
[[nodiscard]] std::optional<SnapCandidate> ChooseSnap(
    const std::vector<SnapCandidate>& candidates, const SnapSettings& settings);

//! スナップのヒステリシス状態。ポインタを動かすたびに Resolve を呼ぶ。
//!
//! 状態は「持ち越している候補があるか」だけである。
//! - 何も持っていない → 選ばれた候補を持つ。
//! - 持っている → 選び直した結果に置き換える(同じ候補なら位置と距離だけ更新)。
//! - 何も選ばれない、または抑止中 → 手放す。
//! 抑止(S)はトグルではないので、離したあとは持ち越し無しから始まる。
class SnapHysteresis {
public:
    [[nodiscard]] std::optional<SnapCandidate> Resolve(const SnapScene& scene,
        const ScreenMapping& mapping, const ScreenPoint& pointer, const SnapSettings& settings,
        const GeometryTolerance& tolerance);

    //! 持ち越しを捨てる。ツールの切替、取消、抑止の開始で呼ぶ。
    void Reset() noexcept { held_.reset(); }

    [[nodiscard]] const std::optional<SnapCandidate>& Held() const noexcept { return held_; }

private:
    std::optional<SnapCandidate> held_;
};

} // namespace kachakacha::v2::modeling
