#pragma once

//! 初心者の「面を作る」(プロンプト beginner_workflow)。選んだ線の役割と作り方を、
//! **線のつながりから決定的に** 決めて、理由と他の候補を言う。AI の推測ではない。
//!
//! 見るものは 5 つだけ:
//!   つながり … 線の端どうしが重なるか(端どうしでつながって閉じた輪になるか)
//!   交わり   … 線どうしが交わる・触れるか、何か所か、線のどこでか
//!   閉じ方   … 1 本で閉じているか
//!   同じ平面 … 閉じた輪・線が 1 つの平面に載るか
//!   順番     … ガイドに沿った断面の並びが、どのガイドでも同じか
//!
//! 決め方(上から順に当てはめる。同じ入力なら、選んだ順が違っても同じ答えになる):
//!   1. 人が線ごとに決めた役割(右の棚・右クリック)はそのまま使う
//!   2. 端どうしでつながって閉じた輪があり、残りの線が輪に届く・輪の内側にある
//!        → 輪が境界、残りは面が通る線(平らなら平面、4 辺なら四辺面、それ以外は境界面)
//!        ただし、内側の線が 2 本の辺のあいだを全部渡している(はしご形)ならロフトを薦める
//!   3. 閉じた線だけ: 同じ平面なら平面(外形と穴)、そうでなければ断面
//!   4. 交わりが二部(互いに交わらない 2 組)に分かれる → 本数の多い組が断面、少ない組がガイド
//!        (閉じた線の組は断面、同数なら長い組がガイド)。U・V とも 3 本以上で全部が
//!        交わる網なら曲線網(Gordon)を薦める
//!   5. どれとも交わらない線は断面(2 本ならルールド、3 本以上ならロフト)。閉じた断面の
//!        真ん中を通って断面と交わらない線は中心線
//! 候補の作り方は、幾何の検査(modeling::AnalyzeGuideSurfaceRequest)に実際に通して、
//! 成り立つかを確かめてから言う。**成り立たない作り方を成り立つとは言わない。**

#include "kachakacha/app/Rgb.h"
#include "kachakacha/app/SurfaceInputState.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kachakacha::v2::app {

//! 役割を決める線 1 本(場面の曲線)。
struct RoleWire {
    base::EntityId id;
    std::vector<geometry::CurveSegment> segments;
};

//! 決めた役割(Auto にはならない)。
struct ClassifiedWire {
    base::EntityId id;
    WireRoleChoice role = WireRoleChoice::Section;
    //! 人が決めた役割か。
    bool fixedByUser = false;
    //! なぜその役割か(「2 本のガイドの両方と交わる」など)。
    std::string reasonJa;
};

//! 作り方の候補 1 つ。成り立つかは幾何の検査に通して決める。
struct SurfaceMethodCandidate {
    modeling::GuideSurfaceMethod method = modeling::GuideSurfaceMethod::LoftSections;
    bool feasible = false;
    std::string reasonJa;
    //! この作り方にするときの線ごとの役割(選んだ順)。
    std::vector<ClassifiedWire> wires;
};

struct SurfaceRoleAnalysis {
    //! 選んだ順。
    std::vector<ClassifiedWire> wires;
    modeling::GuideSurfaceMethod recommended = modeling::GuideSurfaceMethod::LoftSections;
    //! 薦めた作り方が幾何の検査を通ったか。偽なら problemsJa に理由がある。
    bool recommendedFeasible = false;
    std::string recommendedReasonJa;
    std::vector<SurfaceMethodCandidate> alternatives;
    //! 調べた事実(「選んだ 5 本を調べました」「ガイド候補: 2 本」「順序の矛盾なし」)。
    std::vector<std::string> factsJa;
    //! 問題(「断面2と断面3の順序がガイド1とガイド2で逆転しています」)。
    std::vector<std::string> problemsJa;

    [[nodiscard]] std::size_t Count(WireRoleChoice role) const noexcept;
    [[nodiscard]] WireRoleChoice RoleOf(const base::EntityId& id) const noexcept;
};

//! 選んだ線の役割と作り方を決める。overrides は人が決めた役割(Auto は無視する)。
[[nodiscard]] SurfaceRoleAnalysis AnalyzeSurfaceRoles(const std::vector<RoleWire>& wires,
    const std::vector<WireRoleOverride>& overrides, const geometry::GeometryTolerance& tolerance);

//! 分類を入力の欄へ写す(断面・ガイド・境界・通る線・中心線)。作り方は、人が選んで
//! いなければ薦めたもの。欄に入っていない線(元の面など)は触らない。
[[nodiscard]] SurfaceInputState WithClassifiedRoles(const SurfaceInputState& state,
    const SurfaceRoleAnalysis& analysis);

//! 他の候補を使う(右の棚の「この作り方にする」)。その候補の役割で欄を入れ直し、作り方を
//! 人が選んだものにする(おまかせは切れる)。
[[nodiscard]] SurfaceInputState WithCandidateRoles(const SurfaceInputState& state,
    const SurfaceMethodCandidate& candidate);

//! 候補の名前(「ガイド付きロフト(ガイド2本)」「境界面」など)。
[[nodiscard]] std::string SurfaceCandidateNameJa(const SurfaceMethodCandidate& candidate);

//! 棚に出す「おすすめ」の行(調べた事実・おすすめと理由・他の候補・問題)。
[[nodiscard]] std::vector<std::string> SurfaceRoleSummaryJa(const SurfaceRoleAnalysis& analysis);

//! 役割の名前(右の棚の選び肢と同じ言葉)と色(3D の色分け)。
//! ガイド = 青、断面 = 橙、境界 = 紫、通る線 = 緑、中心線 = 青緑。
[[nodiscard]] std::string_view WireRoleLabelJa(WireRoleChoice role) noexcept;
[[nodiscard]] Rgb WireRoleColor(WireRoleChoice role) noexcept;
//! 右の棚・右クリックに並べる順(自動 / 断面 / ガイド / 境界 / 通る線 / 中心線)。
[[nodiscard]] const std::vector<WireRoleChoice>& WireRoleChoices();

//! その線が、いまの入力でどの役割か(欄と作り方から)。入っていなければ Auto。
[[nodiscard]] WireRoleChoice RoleOfEntry(const SurfaceInputState& state,
    const base::EntityId& id) noexcept;
//! 役割を受ける欄(作り方で変わる。通る線は境界面・四辺面の「ガイド」の欄)。
[[nodiscard]] modeling::ChainRole SlotForWireRole(WireRoleChoice role) noexcept;
//! 欄に入れたときの役割(作り方で変わる)。
[[nodiscard]] WireRoleChoice WireRoleForSlot(modeling::GuideSurfaceMethod method,
    modeling::ChainRole slot) noexcept;
//! 人が決めた役割(自動に戻すなら Auto)。上書きの一覧だけを書き換える。
[[nodiscard]] SurfaceInputState WithWireRoleChoice(const SurfaceInputState& state,
    const base::EntityId& id, WireRoleChoice role);
//! 人が決めた役割。決めていなければ Auto。
[[nodiscard]] WireRoleChoice WireRoleOverrideOf(const SurfaceInputState& state,
    const base::EntityId& id) noexcept;
//! 3D の色分け(入っている線ごとの色)。
[[nodiscard]] std::vector<std::pair<base::EntityId, Rgb>> SurfaceRoleColors(
    const SurfaceInputState& state);

} // namespace kachakacha::v2::app
