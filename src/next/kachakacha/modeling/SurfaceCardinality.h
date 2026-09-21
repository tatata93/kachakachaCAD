#pragma once

//! 面の入力の「個数の約束」(オーナー指示 2026-09-22「任意個数入力」)。
//!
//! **数学的・幾何学的に固定本数である必要がない入力は、可変長にする。**
//! 固定本数は、その演算の定義上どうしても要る場合だけ(回転体の軸は 1 本、など)。
//!
//! これまでは「ガイドはちょうど 2 本」「断面は 3 本以上 1000 本まで」のような
//! 本数の判定が、検査・画面・核のあちこちに別々に書かれていた。画面は 1 本で
//! 「生成可能」と出し、検査は 2 本でないと断り、核は 3 本目を黙って使わない、
//! という食い違いがあった。ここに 1 か所で書き、全員がここを読む。
//!
//! 上限は「数学的に決まっている」ときだけ付ける。計算量のための上限は付けず、
//! 多いときは soft warning(`kSurfaceInputSoftWarningCount`)で知らせる。

#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace kachakacha::v2::modeling {

//! 上限なし。
inline constexpr std::size_t kUnlimitedCount = std::numeric_limits<std::size_t>::max();

//! これを超えたら「計算に時間がかかることがあります」と知らせる(断らない)。
inline constexpr std::size_t kSurfaceInputSoftWarningCount = 64;

//! 1 つの役割の個数の約束。
struct RoleCardinality {
    ChainRole role = ChainRole::Section;
    std::size_t minimum = 0;
    std::size_t maximum = kUnlimitedCount;
    //! 並びに意味があるか(断面の順など)。
    bool ordered = false;
    //! 人が並べ替えられるか(手動固定)。
    bool reorderable = false;
    //! 固定本数である理由。固定でなければ空。
    const char* fixedReasonJa = "";
    //! 1 回の操作で複数を受けたときの意味。
    const char* batchJa = "";
    //! **1 回の面の生成**が受けられる数。選べる数(minimum..maximum)とは分ける。
    //! 例: 離した面は元の面を何枚でも選べるが、1 回の生成は 1 枚から 1 つを作る。
    //! 選んだ数がこれを超えたら、画面は 1 つずつ別に作る(一括)。
    std::size_t perBuildMaximum = kUnlimitedCount;

    [[nodiscard]] bool Optional() const noexcept { return minimum == 0; }
    [[nodiscard]] bool Unlimited() const noexcept { return maximum == kUnlimitedCount; }
};

//! その作り方が受ける役割と、それぞれの個数。並びは画面に出す順。
[[nodiscard]] const std::vector<RoleCardinality>& SurfaceCardinality(
    GuideSurfaceMethod method);

//! その作り方で、その役割の約束。受けない役割なら nullptr。
[[nodiscard]] const RoleCardinality* SurfaceCardinalityOf(GuideSurfaceMethod method,
    ChainRole role) noexcept;

//! その個数でよいか。よければ空、だめなら人に向けた理由(「断面が 2 本以上必要です。いま 1 本。」)。
//! `roleNameJa` は画面の欄の名前(「断面」「ガイド」「通る線」)。
[[nodiscard]] std::string SurfaceCardinalityProblemJa(GuideSurfaceMethod method,
    ChainRole role, std::size_t count, const std::string& roleNameJa);

//! 「2〜任意」「0〜1」「ちょうど 4」。表と画面に出す。
[[nodiscard]] std::string CardinalityRangeJa(const RoleCardinality& cardinality);

//! ロフトの断面の条件(役割をまたぐので表に書けない)。よければ空。
[[nodiscard]] std::string LoftSectionRuleProblemJa(std::size_t sections, std::size_t rails);

//! 選んだ数だと、1 回の生成に収まらず一括(1 つずつ別に作る)になるか。
[[nodiscard]] bool SurfaceNeedsBatch(GuideSurfaceMethod method, ChainRole role,
    std::size_t count) noexcept;

} // namespace kachakacha::v2::modeling
