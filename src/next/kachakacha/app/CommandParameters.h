#pragma once

//! コマンドが使う数(板厚・面取り量・型紙の余白ほか)。
//!
//! これまで、押し出しの厚みは 0.5mm の決め打ちだった。
//! プラ板は 0.3 / 0.5 / 1.0mm と使い分けるので、**変えられないと使えない。**
//!
//! ここに集めたのは、次の3つを1か所で守るためである。
//!   1. **式で書ける。** `0.3*2` や `20000/87` がそのまま通る。
//!      電卓を出して打ち直すと、打ち間違いが混ざる。
//!   2. **範囲の外は断る。** 板厚 0mm は切れないし、100mm はプラ板ではない。
//!      黙って近い値へ寄せない。寄せると、頼んだ値と違う物が出来る。
//!   3. **断っても前の値を保つ。** 打ち間違えた瞬間に値が消えると、
//!      何だったか思い出せなくなる。
//!
//! 値そのものは文書に入らない。「次に何を作るか」の設定であって、
//! 出来た物の寸法は Feature の側に残る。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/Expression.h"

#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

enum class ParameterId {
    ExtrudeDistance,   //!< 押し出しの距離(板厚)
    OffsetDistance,    //!< ワイヤーの平行オフセット距離。符号で側を選ぶ
    CornerSize,        //!< 面取り量 / 丸め半径
    PatternMarginMm,   //!< 型紙の余白
    ScaleDenominator,  //!< 縮尺の分母。1/87 なら 87
    RealSizeMm,        //!< 実物の寸法(mm)。縮尺で割ると模型の寸法になる
    MaxDeviationMm,    //!< 展開で許すずれ(mm)。ここが通るか通らないかを決める
};

struct ParameterDefinition {
    ParameterId id;
    //! 機械が見る安定ID。表示名は変わってもこれは変えない。
    std::string_view key;
    std::string_view nameJa;
    geometry::QuantityKind kind = geometry::QuantityKind::Length;
    double defaultValue = 0.0;
    //! 受け付ける範囲。両端を含む。
    double minimum = 0.0;
    double maximum = 0.0;
    //! なぜその範囲なのか。断るときにそのまま出す。
    std::string_view reasonJa;
};

//! 定義の一覧。並びは決まっていて、実行中に増えたり減ったりしない。
[[nodiscard]] const std::vector<ParameterDefinition>& ParameterDefinitions();

[[nodiscard]] const ParameterDefinition* FindParameter(ParameterId id) noexcept;

//! いまの値。式も残す。あとで打ち直すときに、書いたものがそのまま出る。
struct ParameterValue {
    std::string expression;
    double value = 0.0;
};

//! 全部の値。定義と同じ並び。
struct ParameterSet {
    std::vector<ParameterValue> values;
};

//! 既定値でそろえる。
[[nodiscard]] ParameterSet DefaultParameters();

[[nodiscard]] double ParameterValueOf(const ParameterSet& set, ParameterId id) noexcept;
[[nodiscard]] std::string ParameterTextOf(const ParameterSet& set, ParameterId id);

//! 式を評価して入れ替える。断ったときは前の集合をそのまま返さず、
//! 診断だけを返す。呼ぶ側が前の値を保つ。
[[nodiscard]] base::Result<ParameterSet> SetParameter(const ParameterSet& set,
    ParameterId id, std::string_view text);

//! 実寸を縮尺で割った、模型の寸法(mm)。
//!
//! 1/87 で実物 20000mm なら 229.885mm である。
//! これを手で計算していると、桁を1つ間違えても気づけない。
[[nodiscard]] double ScaledSizeMm(const ParameterSet& set) noexcept;

//! それを人が読む形にした一言。棚に出す。
[[nodiscard]] std::string ScaledSizeTextJa(const ParameterSet& set);

} // namespace kachakacha::v2::app
