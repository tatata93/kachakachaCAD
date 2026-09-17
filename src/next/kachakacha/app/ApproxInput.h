#pragma once

//! 「近似」の入力と候補(引継ぎ 2026-09-17 の 3)。
//!
//! これまでの近似は「選んでから押す」しかなく、押した瞬間に文書へ入っていた。
//! ここでは 道具を押す → 3D で面や立体を押す → 候補を見比べる → 下見 → 確定 にする。
//!
//! 候補は **実際に作って** 比べる。見積もりではない。同じ backend
//! (`EvaluateFabrication`)を3通りの作り方で回し、部材数と最大のずれを並べる。
//!   A 面ごとに展開 … V2 方式。伸ばさず平らにできる面だけ。できなければ断る。
//!   B 帯で近似(自動分割)… V1 方式。許すずれに収まるまで帯を切る。
//!   C 帯1枚で近似 … V1 方式で分割せず。どれだけずれるかが分かる。
//! 選んだ候補だけを下見に出し、確定はその候補の作り方と結果をそのまま使う。
//!
//! backend を画面のために複製しない。ここは「どの作り方を回すか」を決めるだけ。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/domain/Feature.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

//! 近似の入力。画面の欄と1対1。
struct ApproxInputState {
    //! 元になるもの(部品か形状ガイド)。押した順。
    std::vector<base::EntityId> sources;
    //! 見比べる候補のうち、いま選んでいるもの(0 = A、1 = B、2 = C)。
    int selectedCandidate = 1;
    bool candidateChosenByUser = false;
};

//! 候補の名前と一言。
struct ApproxCandidateSpec {
    std::string label;    //!< "A" "B" "C"
    std::string nameJa;
    std::string hintJa;
};

//! 候補の並び。画面のボタンと同じ順。
[[nodiscard]] const std::vector<ApproxCandidateSpec>& ApproxCandidateSpecs();

//! 候補の作り方。棚の欄から作った基本の作り方に、候補ごとの違いだけを重ねる。
[[nodiscard]] domain::CreateFabricationModelDefinition ApproxCandidateDefinition(
    const domain::CreateFabricationModelDefinition& base, int candidate);

//! 候補を実際に作った結果の要約。作れなかったときは理由。
struct ApproxCandidateOutcome {
    bool evaluated = false;
    bool available = false;
    std::size_t partCount = 0;
    double maximumDeviationMm = 0.0;
    bool reachedTolerance = true;
    std::string refusalJa;
};

//! 「A 面ごとに展開 — 3部材 / 最大 0.000 mm」。作れなければ「— 作れません: 理由」。
[[nodiscard]] std::string ApproxCandidateLineJa(const ApproxCandidateSpec& spec,
    const ApproxCandidateOutcome& outcome);

//! 棚の方式(0 = 面ごと、1 = 帯)に当たる候補。棚のとおりの候補は昔の「選んでから押す」と同じ作り方。
[[nodiscard]] int ApproxCandidateForMethod(int method);

//! 人が選んでいないとき、どれを既定にするか。
//! 棚の方式どおりの候補が作れればそれ。作れなければ、許すずれに収まって作れたもののうち
//! 部材が最も少ないもの。無ければ作れた最初。無ければ棚の方式どおり。
[[nodiscard]] int PreferredApproxCandidate(const std::vector<ApproxCandidateOutcome>& outcomes,
    int baseMethod);

//! 押したものを入れる/外す(もう一度押すと外れる)。
[[nodiscard]] ApproxInputState WithApproxSourcesToggled(const ApproxInputState& state,
    const std::vector<base::EntityId>& ids);
[[nodiscard]] ApproxInputState WithoutApproxSources(const ApproxInputState& state,
    const std::vector<base::EntityId>& ids);

//! 「4. 状態」に出す行。対象の数・候補の行・下見の様子。
[[nodiscard]] std::vector<std::string> ApproxStatusLinesJa(const ApproxInputState& state,
    const std::vector<ApproxCandidateOutcome>& outcomes, bool previewShown);

//! 一番下の一行。「近似: SOURCES=2 / CANDIDATE=B / PARTS=4 / MAXDEV=0.180mm / Preview only」
[[nodiscard]] std::string ApproxFooterLine(const ApproxInputState& state,
    const std::vector<ApproxCandidateOutcome>& outcomes, bool previewShown);

} // namespace kachakacha::v2::app
