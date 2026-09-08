#pragma once

//! 測定や既にある形から作図点を作る(PRD-072、AT-MEA-005)。
//!
//! 測った結果を見るだけで終わらせず、そこから点を置けるようにする。
//! V1 は測定結果が読み取り専用で、測った位置に点を置きたければ
//! 座標を目で読んで手で打ち直す必要があった。
//!
//! ここが作るのは「どこに、どういう由来で点を置くか」までである。
//! 文書へ入れるのは Command の仕事なので、この層は文書を触らない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Measurement.h"

#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

using base::EntityId;
using geometry::CurveSegment;
using geometry::Vector3;

//! 点の出どころ。表示にも使うし、あとから「何から作ったか」を辿るのにも使う。
enum class PointSourceKind {
    MeasuredPoint,     //!< 測った位置そのもの
    MeasuredMidpoint,  //!< 2点測定の中点
    CurveCenter,       //!< 円・円弧の中心
    CurveStart,
    CurveEnd,
    CurveMidpoint,
    ControlPoint,      //!< ベジェ・B-spline の制御点
    ClosestApproach,   //!< 2つの曲線が最も近づく場所
};

[[nodiscard]] std::string PointSourceKindNameJa(PointSourceKind kind);

//! 作れる候補1つ。画面はこれを一覧に出し、利用者が選ぶ。
struct PointCandidate {
    PointSourceKind kind = PointSourceKind::MeasuredPoint;
    Vector3 positionMm{};
    std::string labelJa;
    //! どの形から来たか。空でもよい(測定点は元の形を持たないことがある)。
    std::optional<EntityId> sourceEntityId;
    //! 制御点のとき、何番目か。
    std::size_t index = 0;
};

//! その曲線から作れる点を全部挙げる。順番は決まっている(同じ入力なら同じ並び)。
[[nodiscard]] std::vector<PointCandidate> PointCandidatesOfCurve(const CurveSegment& curve,
    std::optional<EntityId> sourceEntityId);

//! 2点測定から作れる点(両端と中点)。
[[nodiscard]] std::vector<PointCandidate> PointCandidatesOfDistance(
    const geometry::DistanceMeasurement& measurement);

//! 2曲線の最接近から作れる点。
[[nodiscard]] std::vector<PointCandidate> PointCandidatesOfApproach(
    const geometry::ClosestApproach& approach);

//! 候補から作図点の Feature を組み立てる。
//! 所属は Command が作業中グループから決めるので、ここでは触らない。
[[nodiscard]] base::Result<domain::CreatePointDefinition> MakePointDefinition(
    const PointCandidate& candidate);

//! 表示名。「円1の中心」のように、由来が分かる名前にする。
[[nodiscard]] std::string PointDisplayNameJa(const PointCandidate& candidate,
    std::string_view sourceNameJa);

} // namespace kachakacha::v2::app
