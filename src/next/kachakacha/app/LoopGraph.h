#pragma once

//! 線を辺、端点を節にしたグラフと、その中の閉じた輪。
//!
//! 境界面の「外周と通る線を分ける」(OuterLoopSplit)と、「線から面」(LoopFaces)が同じ
//! 輪の数え方を使う。2 か所に書くと、片方だけ輪の見つけ方が違う、が起きる。
//!
//! 端点は許容差(joinMm)でまとめる。平面かどうかは問わない(3D の線でも輪になる)。
//! ここは OCCT も Qt も呼ばない。

#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Vector3.h"

#include <cstddef>
#include <vector>

namespace kachakacha::v2::app {

//! 1 本の線(選択)を、グラフの辺として見たもの。
struct LoopEdge {
    std::size_t selection = 0;
    std::size_t from = 0;   //!< 端点(節)の番号
    std::size_t to = 0;
    std::vector<geometry::Vector3> points;   //!< from から to へ向かう点列
    bool alive = true;
};

//! 輪を 1 つ: 辺の番号と、それぞれを from→to の向きでたどるか。
struct LoopCycle {
    std::vector<std::size_t> edges;
    std::vector<bool> forward;
};

//! 端点を許容差でまとめた節の表。
class LoopGraph {
public:
    explicit LoopGraph(double joinMm) : joinMm_(joinMm) {}

    //! その点の節。許容差の内側に既にあればそれ、無ければ新しく作る。
    std::size_t NodeOf(const geometry::Vector3& point);
    [[nodiscard]] std::size_t NodeCount() const noexcept { return nodes_.size(); }
    [[nodiscard]] const geometry::Vector3& NodeAt(std::size_t index) const noexcept
    {
        return nodes_[index];
    }
    [[nodiscard]] double JoinMm() const noexcept { return joinMm_; }

private:
    double joinMm_;
    std::vector<geometry::Vector3> nodes_;
};

//! 線の並び(鎖)を辺にする。端点は graph の節へ。points は許容差に合わせた点列。
[[nodiscard]] LoopEdge MakeLoopEdge(std::size_t selection,
    const std::vector<geometry::CurveSegment>& segments, LoopGraph& graph);

//! 行き止まりの線(どちらかの端に他の線が来ていない)を、なくなるまで alive = false にする。
void PruneDeadEnds(std::vector<LoopEdge>& edges, std::size_t nodeCount);

//! 生きている辺から、閉じた輪を全部数える(単純閉路)。同じ輪は、いちばん小さい辺から
//! 始めたものだけを数える。上限 maximumCycles で打ち切る。
[[nodiscard]] std::vector<LoopCycle> FindLoopCycles(const std::vector<LoopEdge>& edges,
    std::size_t maximumCycles = 20000);

//! 輪が囲む面積。Newell のベクトル面積の大きさ(平らでない輪でも、いちばんよく
//! 見える向きへ投影した面積になる)。
[[nodiscard]] double EnclosedArea(const std::vector<LoopEdge>& edges, const LoopCycle& cycle);

//! 輪を 1 周する点列(辺の向きをそろえてつないだもの)。
[[nodiscard]] std::vector<geometry::Vector3> CycleRing(const std::vector<LoopEdge>& edges,
    const LoopCycle& cycle);

} // namespace kachakacha::v2::app
