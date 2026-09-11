#pragma once

//! 製作の棚(V1 の近似モデル画面 `PartModelPanel`、走査 §2-3)の欄。
//!
//! V1 は 分割軸 / 境界 / 上限 / 最小幅 / 再現度 / 曲げ / 固定 / 型紙 を 1 枚のパネルに
//! 並べていた。V2 は命令(`fabrication.*`)として全部あるが、方式や固定の種類は
//! 押すたびに回る切替だった。ここは **欄の値 ↔ 作り方(CreateFabricationModelDefinition)**
//! の往復と、欄の値の検査だけを持つ。画面は欄を並べて値を運ぶだけ。
//!
//! 断られかた:
//! - UI-F001 手動境界の書き方が読めません。(「0.3, 0.6」のように 0 と 1 の間の数をカンマ区切り)
//! - UI-F002 部材数の上限は 1〜200 にしてください。
//! - UI-F003 部材の最小幅は 0 より大きい数にしてください。
//! - UI-F004 再現度は 1〜20 にしてください。
//! - UI-F005 面の範囲は 0〜1 の中で、最小を最大より小さくしてください。
//! - UI-F006 部材番号が範囲外です。

#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/domain/Feature.h"

#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

struct FabricationChoice {
    FabricationMethod method = FabricationMethod::BandApproximation;
    //! 0 = U、1 = V、2 = 自動(曲がっている方向を横切る)。V1 方式だけが使う。
    int splitAxis = 2;
    bool automaticBoundaries = true;
    int maximumPartCount = 12;
    double minimumPartWidthMm = 4.0;
    int fidelity = 6;
    //! 手動境界(分割軸のパラメータ、0 と 1 の間)。automaticBoundaries が false のとき使う。
    std::vector<double> manualBoundaries;
    //! 面の範囲(V1 の板材の「範囲」)。u は列方向、v は行方向の 0〜1。
    double rangeUMin = 0.0;
    double rangeUMax = 1.0;
    double rangeVMin = 0.0;
    double rangeVMax = 1.0;
};

//! 「0.3, 0.6」のような文字を境界の並びにする。空なら空の並び。読めなければ UI-F001。
//! 値の範囲(0 と 1 の間、重複)は近似のときに core が検査する(FAB の理由で断る)。
[[nodiscard]] base::Result<std::vector<double>> ParseBoundaryList(std::string_view text);

//! 境界の並びを「0.3, 0.6」の文字にする(欄へ戻すため)。
[[nodiscard]] std::string FormatBoundaryList(const std::vector<double>& boundaries);

//! 欄の値の検査。範囲の外は断る(黙って寄せない)。
[[nodiscard]] base::Result<FabricationChoice> CheckFabricationChoice(
    const FabricationChoice& choice);

//! 欄の値を作り方へ写す。板厚・許すずれ・元の部品・開口などはここでは触らない。
void ApplyFabricationChoice(domain::CreateFabricationModelDefinition& definition,
    const FabricationChoice& choice);

//! 作り方から欄の値を作る(選んだ近似モデルの設定を棚に出すため)。
[[nodiscard]] FabricationChoice FabricationChoiceOf(
    const domain::CreateFabricationModelDefinition& definition);

//! 分割軸の名前(欄の並び: 自動 / U / V の順ではなく、値 0/1/2 の名前)。
[[nodiscard]] std::string_view SplitAxisNameJa(int splitAxis) noexcept;

// ---- 部材ごとの曲げ(V1 の part_model_part_assembly) ----

//! 「選んだ部材だけが曲がる」。V1 と同じ決まりで、帯ごとの進行度を作る。
//!
//! partNumbers が空なら **全体** を動かす(帯ごとの値は捨てて master に従う。V1 と同じ)。
//! 番号を挙げたら、いまの値(無ければ master)へ展開してから、その番号だけを書き換える。
//! 番号は 1 から始まる部材番号。範囲の外は UI-F006 で断る(黙って無視しない)。
struct BandProgressUpdate {
    //! 新しい masterPercent。部材を挙げたときは元のまま。
    double masterPercent = 100.0;
    //! 新しい bandProgress(0〜1)。全体を動かしたときは空(master に従う)。
    std::vector<double> bandProgress;
};

[[nodiscard]] base::Result<BandProgressUpdate> UpdateBandProgress(
    const domain::CreateFabricationModelDefinition& definition, int bandCount,
    const std::vector<int>& partNumbers, double percent);

//! 「1, 3」のような文字を部材番号の並びにする。空なら空。読めなければ UI-F006。
[[nodiscard]] base::Result<std::vector<int>> ParsePartNumberList(std::string_view text);

} // namespace kachakacha::v2::app
