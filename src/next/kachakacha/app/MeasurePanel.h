#pragma once

//! 測る棚(PRD-070〜072、V1同等性)。
//!
//! V1 では、測るたびにどのモードで測るかを人が選ばされた。
//! 「2点距離」を選んでから点を2つ拾う、という順である。
//! 選び間違えると、拾い直しになった。
//!
//! V2 では逆にする。**選んだものから、測れることを全部出す。**
//! 1本選べば長さ・半径・両端、2本選べばそれに加えて最短距離と接線の角度。
//! 人が測り方を選ぶ必要はない。選ぶ手間が減るぶん、間違えようがない。
//!
//! ここは画面を知らない。行(見出しと値)を作るだけである。
//! そうしておくと、画面を出さずに「何が出るか」を確かめられる。

#include "kachakacha/geometry/CurveSegment.h"

#include <string>
#include <vector>

namespace kachakacha::v2::app {

//! 測った結果の1行。見出しと値を分けて持つ。表にそのまま並ぶ。
struct MeasureRow {
    std::string labelJa;
    std::string valueJa;
};

//! 測る対象。いまは曲線だけを見る。
struct MeasureRequest {
    std::vector<geometry::CurveSegment> curves;
    //! 長さを積分する刻み。文書の許容差を渡す。
    double toleranceMm = 0.001;
};

//! 数を mm の文字列にする。桁は3桁で揃える。
//! 揃えないと、表が読みにくく、変わった桁に気づけない。
[[nodiscard]] std::string FormatMillimetersJa(double value);

//! 度の文字列にする。
[[nodiscard]] std::string FormatDegreesJa(double radians);

//! 座標を (x, y, z) の形にする。
[[nodiscard]] std::string FormatPointJa(const geometry::Vector3& point);

//! 選んだものから、測れることを全部出す。
//!
//! 何も選んでいなければ、空ではなく「何を選べばよいか」を1行返す。
//! 空の表は、壊れているのか選び忘れなのかが分からない。
[[nodiscard]] std::vector<MeasureRow> BuildMeasureRows(const MeasureRequest& request);

//! 表の見出しに出す一言。何を測っているかを言う。
[[nodiscard]] std::string MeasureSummaryJa(const MeasureRequest& request);

} // namespace kachakacha::v2::app
