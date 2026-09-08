#pragma once

//! 閉じたワイヤー群から部品の候補を見つける(geometry-contract §7)。
//!
//! 「選んだ線が立体を囲んでいるか」を、線の並び順に頼らず決める。
//! 手順は §7.3 のとおり:
//!   端点で分割 → 平面ごとの閉路を列挙 → 各辺がちょうど2回使われる組合せを探す。
//!
//! ここでは OCCT を使わない。使わなくても「囲めていない」「辺が3回使われている」
//! 「体積が0」は判定できる。OCCT へ渡すのは、この検査を通った候補だけにする。
//! 任意の非平面閉輪郭へ理由のないFillを自動でかけない(§7.2)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/SubshapeKey.h"

#include <vector>

namespace kachakacha::v2::modeling {

using base::EntityId;
using base::SegmentId;
using geometry::CurveSegment;
using geometry::GeometryTolerance;
using geometry::Vector3;

//! 候補に入れる1本の線。どのワイヤーの何番目かを持ったまま扱う。
struct CageEdgeInput {
    EntityId entityId;
    SegmentId segmentId;
    CurveSegment segment;
};

//! 利用者が根拠を示したパッチ(geometry-contract §7.2 の 2〜4)。
//! 平面でない面は、ここで示されたものだけを使う。
//! 任意の非平面閉輪郭へ理由のないFillを自動でかけない。
struct CageDeclaredPatch {
    //! この面を囲む線(CageEdgeInput の添字)。順不同でよい。
    std::vector<std::size_t> edgeIndices;
    //! 根拠になった形状ガイド。BoundaryFill として明示した場合は空でよい。
    EntityId guideSurfaceId;
};

//! 見つかった1枚のパッチ。
struct CagePatch {
    //! この面を囲む辺(CageEdgeInput の添字)。順序は面を1周する順。
    std::vector<std::size_t> edgeIndices;
    //! 各辺を、その向きのまま使うか反転して使うか。
    std::vector<bool> reversed;
    Vector3 normal{};
    double areaMm2 = 0.0;
    //! 平面として自動で見つけた面か、利用者が根拠を示した面か。
    bool declared = false;
    //! この面の意味的キー。cage/patch/<代表する線のID>。
    SubshapeKey key;
};

//! 見つかった1つの閉シェル。
struct CageShell {
    std::vector<CagePatch> patches;
    //! 体積。すべて平面の面ならこれが正しい値になる。
    //! 平面でない面が混ざる場合は境界からの概算で、正しい値は OCCT が出す(§7.3 の7)。
    //! ここでの用途は「潰れていないか」の足切りであって、表示用の値ではない。
    double volumeMm3 = 0.0;
    bool volumeIsApproximate = false;
    //! 面の向きを揃えた結果、外向きになっているか。
    bool outwardOriented = true;
};

struct WireCageAnalysis {
    //! 閉シェルの候補。1つならそのまま、複数なら利用者に選ばせる。
    std::vector<CageShell> shells;
    //! 閉シェルに使われなかった辺(CageEdgeInput の添字)。
    std::vector<std::size_t> unusedEdges;
    std::vector<base::Diagnostic> notes;
};

//! 選ばれた線から閉シェルの候補を探す。
//! 見つからない、曖昧、閉じていない、辺が3回使われている、体積0 は
//! それぞれ GEO-S0xx で断る。
[[nodiscard]] base::Result<WireCageAnalysis> AnalyzeWireCage(
    const std::vector<CageEdgeInput>& edges,
    const std::vector<CageDeclaredPatch>& declaredPatches,
    const GeometryTolerance& tolerance);

//! 平面の面だけで囲める形のための短い呼び方。
[[nodiscard]] base::Result<WireCageAnalysis> AnalyzeWireCage(
    const std::vector<CageEdgeInput>& edges, const GeometryTolerance& tolerance);

//! 確定する部品1つぶんの計画。
//!
//! 1つの閉シェル = 1つの部品である。2つのシェルを同時に確定しても、
//! 中身が2つ入った1つの部品にはしない(AT-GEO-013)。
//! まとめてしまうと、片方だけを消す・厚みを変えるといった操作ができなくなり、
//! 製作モデルも型紙も「どちらの立体のものか」を言えなくなる。
struct WireCagePart {
    std::size_t shellIndex = 0;
    double volumeMm3 = 0.0;
    bool volumeIsApproximate = false;
    //! この部品の面の意味的キー。OCCT の面番号は使わない。
    std::vector<SubshapeKey> faceKeys;
};

//! 選んだシェルから、部品の計画を作る。選んだ数だけ部品が出る。
[[nodiscard]] base::Result<std::vector<WireCagePart>> PlanWireCageParts(
    const WireCageAnalysis& analysis, const std::vector<std::size_t>& chosenShells);

} // namespace kachakacha::v2::modeling
