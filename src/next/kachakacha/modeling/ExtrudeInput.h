#pragma once

//! 押し出しの入力検査と、出来上がりの予測(geometry-contract §8、AT-EXT-001〜008)。
//!
//! ここには押し出しの実行は入らない。入るのは
//!   1. その入力で押し出してよいかの判断
//!   2. 出来上がるはずの体積・面数・部品数の予測
//! の2つだけである。実際の形状は OCCT 側が作る。
//!
//! 予測を core が持つ理由は、V1 の失敗の再発を防ぐためである。
//! V1 は OCCT が返した solid をそのまま採用したので、
//! 断面を通っていない形や、穴が多角形へ化けた形が、そのまま板取りまで流れた。
//! V2 では core が「体積はこうなるはず」を先に出し、
//! OCCT が作ったものと突き合わせる。合わなければ、その結果は捨てる。
//!
//! 診断コードは geometry-contract §8.4 の通り EXT-001〜006。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/fabrication/SurfacePatch.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <string_view>
#include <vector>

namespace kachakacha::v2::modeling {

using base::Diagnostic;
using base::EntityId;
using fabrication::AnalyticSurfaceInfo;
using geometry::CurveSegment;
using geometry::GeometryTolerance;
using geometry::Vector3;

//! 押し出す向きの決め方(§8.2)。
enum class ExtrudeDirectionMode {
    ProfileNormal,
    WorkPlaneNormal,
    WorldX,
    WorldY,
    WorldZ,
    SelectedVector,
    CustomXYZ,
};

//! どこまで押すか(§8.3)。
enum class ExtrudeExtentMode {
    Distance,
    SymmetricDistance,
    TwoDistances,
    ToTarget,
    ThroughAll,
};

//! ToTarget の相手。
enum class ExtrudeTargetKind {
    None,
    Plane,
    AnalyticSurface,
};

//! 部品を作るときの演算(§8.4)。
enum class ExtrudeBooleanMode {
    NewPart,
    AddToPart,
    SubtractFromPart,
};

//! 何を作るか。最低1つ選ぶこと。
struct ExtrudeOutputSelection {
    bool endProfileWire = false;   //!< 押し出し先の輪郭ワイヤー
    bool sideBoundaryWires = false;//!< 側面の境界ワイヤー
    bool part = false;             //!< 部品

    [[nodiscard]] bool Any() const noexcept
    {
        return endProfileWire || sideBoundaryWires || part;
    }
};

//! 押し出す元になる1本の輪郭。
struct ExtrudeProfile {
    std::vector<CurveSegment> segments;
    bool closed = false;
    EntityId sourceEntityId;
    //! 線1本ごとの不変ID。側面の意味的キーはこれで作る。
    //! 空でもよいが、その場合は側面のキーが位置に依存する。
    //! UI から呼ぶときは必ず入れること(architecture-and-data.md §6)。
    std::vector<base::SegmentId> segmentIds;
};

struct ExtrudeRequest {
    std::vector<ExtrudeProfile> profiles;

    ExtrudeDirectionMode directionMode = ExtrudeDirectionMode::ProfileNormal;
    //! SelectedVector / CustomXYZ のとき。正規化前の値をそのまま持つ(§8.2)。
    Vector3 customDirection{0.0, 0.0, 1.0};
    //! WorkPlaneNormal のとき。
    WorkPlaneFrame workPlane;
    //! 向き反転ボタン。
    bool reversed = false;

    ExtrudeExtentMode extent = ExtrudeExtentMode::Distance;
    double distanceMm = 0.0;
    //! TwoDistances の負側。
    double secondDistanceMm = 0.0;

    ExtrudeTargetKind targetKind = ExtrudeTargetKind::None;
    WorkPlaneFrame targetPlane;
    AnalyticSurfaceInfo targetSurface;

    ExtrudeOutputSelection outputs;
    ExtrudeBooleanMode booleanMode = ExtrudeBooleanMode::NewPart;
    //! 足す/引く相手が明示選択されているか。近い部品を自動で選ばない(§8.4)。
    bool hasSelectedPart = false;
    //! 0距離の輪郭ワイヤー出力を、利用者が確認済みか。
    bool zeroDistanceConfirmed = false;
};

//! 輪郭の入れ子。GuideSurface と同じ考え方。
struct ExtrudeLoop {
    std::size_t profileIndex = 0;
    bool isHole = false;
    std::vector<std::size_t> holes;   //!< 外周のとき、属する穴の profileIndex
    double areaMm2 = 0.0;             //!< 符号なし面積
    //! 面積が厳密か。直線と円弧だけで出来た輪郭なら厳密に出せる。
    //! ベジェやB-splineが混ざると標本化した近似になる。
    bool areaIsExact = true;
};

struct ExtrudeAnalysis {
    //! 反転を織り込んだ、正規化済みの押し出し方向。
    Vector3 direction{0.0, 0.0, 1.0};
    //! 輪郭が載っている平面。
    geometry::PlaneFit profilePlane;
    //! 外周と穴の分類。外周だけが独立した出力候補になる。
    std::vector<ExtrudeLoop> loops;
    //! 作られるはずの部品の数。事前表示と commit がここで一致するかを見る(AT-EXT-005)。
    std::size_t expectedPartCount = 0;
    //! 輪郭平面から見た、押し出しの開始と終了(方向に沿った符号つき距離)。
    double startOffsetMm = 0.0;
    double endOffsetMm = 0.0;
    //! 予測。OCCT が作ったものと突き合わせる。
    double predictedVolumeMm3 = 0.0;
    double predictedProfileAreaMm2 = 0.0;
    //! 断面積(したがって体積)が厳密か。突き合わせの厳しさをここで変える。
    bool areaIsExact = true;
    std::size_t predictedFaceCount = 0;
    //! ToTarget のとき、輪郭上の各点が相手へ届くまでの距離の幅。
    double minimumReachMm = 0.0;
    double maximumReachMm = 0.0;
    std::vector<Diagnostic> notes;
};

//! 入力を調べる。押し出してよいなら予測つきで返す。だめなら EXT-00x で断る。
[[nodiscard]] base::Result<ExtrudeAnalysis> AnalyzeExtrudeRequest(
    const ExtrudeRequest& request, const GeometryTolerance& tolerance);

//! 出来上がった部品が、予測と合っているかを確かめる(§8.4 の「別々に近似計算しない」)。
struct ExtrudeResultCheck {
    double volumeErrorRatio = 0.0;
    bool volumeMatches = true;
    bool faceCountMatches = true;
    bool partCountMatches = true;
    [[nodiscard]] bool Ok() const noexcept
    {
        return volumeMatches && faceCountMatches && partCountMatches;
    }
};

//! 予測と実物が食い違ったときの診断を組み立てる(EXT-008 ResultInvalid)。
//! カーネル層はこれを使って断る。コードをあちこちに書かない。
[[nodiscard]] Diagnostic MakeResultInvalid(std::string detailsJa);

[[nodiscard]] ExtrudeResultCheck CheckExtrudeResult(const ExtrudeAnalysis& analysis,
    double actualVolumeMm3, std::size_t actualFaceCount, std::size_t actualPartCount);

//! 平面に載った閉じた輪郭の符号つき面積。
//! 直線と円弧だけなら厳密に出す。円が多角形へ化けていないかを、
//! 出来上がりの体積と突き合わせて見つけるために要る(AT-EXT-004)。
struct PlanarLoopArea {
    double signedAreaMm2 = 0.0;
    bool exact = true;
};

[[nodiscard]] PlanarLoopArea SignedAreaOnPlane(const std::vector<CurveSegment>& segments,
    const geometry::PlanarFrame& frame, double samplingToleranceMm);

[[nodiscard]] constexpr std::string_view ExtrudeExtentName(ExtrudeExtentMode mode) noexcept
{
    switch (mode) {
    case ExtrudeExtentMode::Distance:          return "distance";
    case ExtrudeExtentMode::SymmetricDistance: return "symmetric_distance";
    case ExtrudeExtentMode::TwoDistances:      return "two_distances";
    case ExtrudeExtentMode::ToTarget:          return "to_target";
    case ExtrudeExtentMode::ThroughAll:        return "through_all";
    }
    return "unknown";
}

} // namespace kachakacha::v2::modeling
