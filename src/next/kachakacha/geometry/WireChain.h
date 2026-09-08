#pragma once

//! WireEntity と、複数ワイヤーから論理鎖を作る連結解析(geometry-contract §3)。
//!
//! 連結解析は「役割ごとに独立して」行う。境界・断面・ガイドを一緒くたに入れると、
//! 断面がガイドへ接する正当なケースが必ず分岐扱いで落ちる。
//! 呼び出し側は1つの役割ぶんだけを渡すこと。
//!
//! 初回切替では端点どうしの接続だけを扱う。Segment内部での交差は扱わない
//! (曲線どうしの3D交差は縮退処理が重く、許容差も2桁足りないため。
//!  pre-implementation-fixes.md 第3節 矛盾16)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"

#include <vector>

namespace kachakacha::v2::geometry {

//! 1本のワイヤー。順序付きのSegmentを持ち、隣は許容差内で必ずつながる。
class WireEntity {
public:
    struct Item {
        base::SegmentId id;
        CurveSegment segment;
    };

    //! つながっていないSegment列は作らせない。
    [[nodiscard]] static base::Result<WireEntity> Make(base::EntityId id,
        std::vector<Item> items, double modelLinearMm);

    [[nodiscard]] base::EntityId Id() const noexcept { return id_; }
    [[nodiscard]] const std::vector<Item>& Items() const noexcept { return items_; }
    [[nodiscard]] bool IsClosed() const noexcept { return closed_; }
    [[nodiscard]] Vector3 StartPoint() const { return items_.front().segment.StartPoint(); }
    [[nodiscard]] Vector3 EndPoint() const { return items_.back().segment.EndPoint(); }
    [[nodiscard]] double TotalLength(double tolerance) const;

private:
    base::EntityId id_;
    std::vector<Item> items_;
    bool closed_ = false;
};

//! 連結解析へ渡す1本ぶん。向きは解析が決めるので、呼び出し側は指定しない。
struct ChainInput {
    base::EntityId entityId;
    base::SegmentId segmentId;
    CurveSegment segment;
};

//! 解析が決めた順序と向き。Featureへはこの形で保存する。
struct OrientedSegment {
    base::EntityId entityId;
    base::SegmentId segmentId;
    bool reversed = false;
};

struct ChainOrder {
    std::vector<OrientedSegment> segments;
    bool closed = false;
};

//! 分岐で自動決定できなかったときに、UIへ返す候補。
struct BranchCandidate {
    Vector3 position{};
    int degree = 0;
    std::vector<OrientedSegment> incident;
};

struct ChainAnalysis {
    ChainOrder order;
    std::vector<BranchCandidate> branches;   //!< GEO-W002 のとき非空
    std::vector<Vector3> nearMissPoints;     //!< GEO-W003 のとき非空
};

//! 1つの役割ぶんのSegment群を、順序と向きの決まった1本の鎖にする。
//! 決定的であること。乱数も unordered container の反復順も使わない。
[[nodiscard]] base::Result<ChainAnalysis> AnalyzeChain(std::vector<ChainInput> inputs,
    const GeometryTolerance& tolerance);

//! ワイヤーが自分自身と交わっている場所。
struct SelfIntersection {
    std::size_t firstSegment = 0;
    std::size_t secondSegment = 0;
    Vector3 position{};
};

//! ワイヤー単体としての自己交差(GEO-W004)。
//!
//! 「輪郭を面にするとき」の自己交差は面を作る側で見ているが、
//! 型紙や曲げ線として使うワイヤーは面にしないので、そこでは見られない。
//! 自分と交わるワイヤーは、切っても曲げても意味が決まらないので、ここで断る。
//!
//! 隣り合う線が端点で触れているのは交差ではない。閉じた鎖の最初と最後も同じ。
[[nodiscard]] base::Result<std::vector<SelfIntersection>> FindSelfIntersections(
    const std::vector<CurveSegment>& segments, bool closed,
    const GeometryTolerance& tolerance);

} // namespace kachakacha::v2::geometry
