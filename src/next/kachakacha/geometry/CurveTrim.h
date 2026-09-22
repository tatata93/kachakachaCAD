#pragma once

//! 1 本の線を、ほかの線(境目)との交点で切る・伸ばす・分ける(Inventor のスケッチの
//! トリム・延長・分割と同じ手順のための幾何、INVENTOR_PROCEDURE.md)。
//!
//! 境目は「渡された全部の線」。線の中の交点(開いた線は端を除く)を **区切り** と呼ぶ。
//!   トリム: 押した位置 t を挟む 2 つの区切りの間が消える。区切りが片側に無ければ端まで、
//!           1 つも無ければ線ごと消える(Inventor と同じ)。円は周期で扱い、残りは 1 本の円弧になる。
//!   延長  : 線の端を、伸ばした先でいちばん近い境目まで伸ばす。
//!   分割  : 押した位置にいちばん近い区切りで 2 本に分ける。
//! 交点は geometry/CurveIntersection(実 3D 交点、許容差は modelLinearMm)で求める。
//! B スプラインは途中で切れない(GEO-C014)ので、切る操作はその理由で断る。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace kachakacha::v2::geometry {

//! 円の区切りが 1 つしか無く、消す区間が決まらない。
inline constexpr const char* kTrimOneCutOnCircle = "GEO-E023";
//! 分ける交点が無い。
inline constexpr const char* kSplitNoCut = "GEO-E024";
//! 閉じた線は延ばせない。
inline constexpr const char* kExtendClosed = "GEO-E025";

//! 区切り(線の上の交点)。
struct CutPoint {
    double parameter = 0.0;       //!< 切られる線の t
    std::size_t boundaryIndex = 0; //!< どの境目と交わったか
    Vector3 point{};
};

//! 線の上の区切りを全部。開いた線は端(0・1)を除き、t の昇順・重複なし。
[[nodiscard]] std::vector<CutPoint> CutPointsOn(const CurveSegment& target,
    const std::vector<CurveSegment>& boundaries, const GeometryTolerance& tolerance);

//! 消える区間。開いた線: [low, high](0〜1)。円: low から進んで high まで(low > high なら 0 をまたぐ)。
struct TrimSpan {
    double low = 0.0;
    double high = 1.0;
    bool whole = false;    //!< 区切りが無く、線ごと消える
    bool closed = false;   //!< 円(周期)
    std::optional<CutPoint> lowCut;
    std::optional<CutPoint> highCut;
};

//! 押した位置 t を挟む区間。
[[nodiscard]] base::Result<TrimSpan> TrimSpanAround(const CurveSegment& target,
    const std::vector<CutPoint>& cuts, double t, const GeometryTolerance& tolerance);

//! 区間を消した残り。before = 始点側 [0, low]、after = 終点側 [high, 1](無ければ空)。
//! 円は after に残りの円弧が 1 本入る。線ごと消えるなら両方とも空。
struct TrimRemainder {
    std::vector<CurveSegment> before;
    std::vector<CurveSegment> after;
};

[[nodiscard]] base::Result<TrimRemainder> RemoveSpan(const CurveSegment& target,
    const TrimSpan& span);

//! 消える区間の点列(下見用)。
[[nodiscard]] std::vector<Vector3> SampleSpan(const CurveSegment& target, const TrimSpan& span,
    int count = 48);

//! 区間の長さ(mm)。
[[nodiscard]] double SpanLengthMm(const CurveSegment& target, const TrimSpan& span,
    const GeometryTolerance& tolerance);

//! 端(0 = 始点、1 = 終点)を、伸ばした先でいちばん近い境目まで伸ばす。
struct ExtendResult {
    CurveSegment extended;
    double addedMm = 0.0;
    std::size_t boundaryIndex = 0;
    Vector3 reachedPoint{};
};

[[nodiscard]] base::Result<ExtendResult> ExtendToNearestBoundary(const CurveSegment& target,
    int endpointIndex, const std::vector<CurveSegment>& boundaries,
    const GeometryTolerance& tolerance);

//! 押した位置 t にいちばん近い区切り。
[[nodiscard]] std::optional<CutPoint> NearestCut(const CurveSegment& target,
    const std::vector<CutPoint>& cuts, double t);

//! 区切りで 2 本に分ける。円は、その位置を継ぎ目にした 360° の円弧 1 本(second は空)。
struct SplitPieces {
    CurveSegment first;
    std::optional<CurveSegment> second;
};

[[nodiscard]] base::Result<SplitPieces> SplitAtCut(const CurveSegment& target,
    const CutPoint& cut);

} // namespace kachakacha::v2::geometry
