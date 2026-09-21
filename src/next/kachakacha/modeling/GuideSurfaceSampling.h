#pragma once

//! 形状ガイドの入力検査で共有する道具(GuideSurfaceInput.cpp と LoftInput.cpp が使う)。
//!
//! 外へ見せる API ではない。検査の中身を 2 つのファイルへ分けたので、
//! 点列にする・交わりを探す・断面を並べる道具をここへまとめた。
//! 同じ道具を 2 か所に書くと、片方だけ直して判定が食い違う。

#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::modeling::detail {

inline constexpr const char* kNonPlanar = "GEO-G001";
inline constexpr const char* kMixedOpenClosed = "GEO-G002";
inline constexpr const char* kSectionOrder = "GEO-G003";
inline constexpr const char* kNotConnected = "GEO-G004";
inline constexpr const char* kCrossingMissing = "GEO-G005";
inline constexpr const char* kCrossingOrder = "GEO-G006";
inline constexpr const char* kSelfIntersection = "GEO-G007";
inline constexpr const char* kFitExceeded = "GEO-G008";
//! 入力の数や種類がそもそも足りない。上の8つはどれも「幾何が悪い」話なので分ける。
inline constexpr const char* kBadInput = "GEO-G009";

//! 検査用の点列を作るときの粗さ。細かすぎると遅く、粗いと交差を見逃す。
[[nodiscard]] double SamplingToleranceMm(const GeometryTolerance& tolerance);

struct SampledChain {
    std::size_t chainIndex = 0;
    const GuideChain* chain = nullptr;
    std::vector<Vector3> points;
    std::vector<double> parameters;   //!< 正規化弧長
    Vector3 centroid{};
    double lengthMm = 0.0;
};

[[nodiscard]] std::vector<SampledChain> SampleAll(const GuideSurfaceRequest& request,
    double toleranceMm);

[[nodiscard]] std::vector<std::size_t> IndicesWithRole(const GuideSurfaceRequest& request,
    ChainRole role);

//! 人に見せる呼び名。「断面 3」「ガイド 2」。作り方で呼び方が変わる役割がある
//! (曲線網の外形U/V、境界面の通る線)ので、作り方も渡す。
[[nodiscard]] std::string ChainLabel(const GuideChain& chain);
[[nodiscard]] std::string ChainLabel(GuideSurfaceMethod method, const GuideChain& chain);

//! 断面が全部openか全部closedかを見る。混ざっていたら GEO-G002。
[[nodiscard]] std::vector<Diagnostic> CheckSectionOpenClosed(
    const GuideSurfaceRequest& request, const std::vector<std::size_t>& sections);

//! 断面の重心を主成分軸へ落として並べる(§6.4)。同値ならID順。
[[nodiscard]] SectionOrdering OrderSections(const std::vector<SampledChain>& sampled,
    const std::vector<std::size_t>& sections);

//! 隣り合う断面が離れているか。全域で許容差以下なら退化(§6.3)。
[[nodiscard]] bool SectionsAreDistinct(const SampledChain& first, const SampledChain& second,
    double toleranceMm);

//! open断面の向きを揃える。反転したほうが端点どうし近ければ、そちらを使う。
[[nodiscard]] bool ShouldReverseAgainst(const SampledChain& reference,
    const SampledChain& candidate);

//! 2本の鎖が最も近づく場所。線分どうしで測る。
[[nodiscard]] ChainCrossing FindClosestApproach(const SampledChain& first,
    const SampledChain& second);

//! 許容差内で近づく区間がいくつあるか。2箇所以上なら「2重交差」。
[[nodiscard]] int CountApproaches(const SampledChain& first, const SampledChain& second,
    double toleranceMm);

//! 線どうしをつないでよい隙間。端点の吸着と同じ考え方。
[[nodiscard]] double JoinToleranceMm(const GeometryTolerance& tolerance);

} // namespace kachakacha::v2::modeling::detail
