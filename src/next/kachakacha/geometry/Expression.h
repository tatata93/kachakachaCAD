#pragma once

//! 数値入力欄の数式(geometry-contract §11)。
//!
//! 現行版のパーサは + - * / と単項と括弧と pi しか持たない。
//! ^ ・ deg() ・ rad() ・ 単位suffix ・ 全角正規化 ・ 式の保持は、ここで新しく作る。
//!
//! 大事な約束: 式と評価値の両方を持ち、式を再編集できる。
//! 0除算・構文エラー・単位の取り違えは値を返さない(commitさせない)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/Units.h"

#include <string>
#include <string_view>

namespace kachakacha::v2::geometry {

//! その欄が何を受け付けるか。長さ欄に角度、角度欄に長さを入れたら拒否する。
enum class QuantityKind {
    Length,   //!< mm へ直して返す
    Angle,    //!< rad へ直して返す
    Scalar,   //!< 単位なし(個数、比率など)
};

//! 式と、その評価結果。Featureへはこの両方を保存する。
struct EvaluatedValue {
    std::string expression;  //!< 利用者が書いたそのままの式(再編集用)
    double value = 0.0;      //!< 長さならmm、角度ならrad
    QuantityKind kind = QuantityKind::Scalar;
};

//! 全角の数字と演算子を半角へ直す。入力の見た目を直すだけで、意味は変えない。
[[nodiscard]] std::string NormalizeFullWidth(std::string_view text);

//! 評価する。受け付けられない式は診断つきで断る。
[[nodiscard]] base::Result<EvaluatedValue> EvaluateExpression(std::string_view text,
    QuantityKind expected);

} // namespace kachakacha::v2::geometry
