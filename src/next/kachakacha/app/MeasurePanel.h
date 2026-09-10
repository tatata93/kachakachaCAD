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

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! 測った結果の1行。見出しと値を分けて持つ。表にそのまま並ぶ。
struct MeasureRow {
    std::string labelJa;
    std::string valueJa;
};

//! 測り方。Selection は V2 の「選んだものから全部」、残り3つは V1 の3モード。
//! 点を押して測るモードでは、押した点(吸着済み)を pickedPoints で受ける。
enum class MeasureMode {
    Selection,        //!< 選んだ線から測れることを全部出す
    TwoPoints,        //!< 2点間: 距離・dX/dY/dZ・座標面への投影・軸との角度
    ThreePointAngle,  //!< 3点角度: 1点目-頂点(2点目)-3点目
    Element,          //!< 要素: 線1本と点で接線・法線、線2本で接線どうし・法線どうしの角度
};

[[nodiscard]] std::string_view MeasureModeNameJa(MeasureMode mode) noexcept;
[[nodiscard]] const std::vector<MeasureMode>& MeasureModes();
//! そのモードで押す点の数(Selection と Element の2本は 0)。
[[nodiscard]] int MeasurePointCount(MeasureMode mode) noexcept;

//! 測る対象。選んだ線と、押した点。
struct MeasureRequest {
    std::vector<geometry::CurveSegment> curves;
    //! 長さを積分する刻み。文書の許容差を渡す。
    double toleranceMm = 0.001;
    MeasureMode mode = MeasureMode::Selection;
    //! 押した点(吸着済み)。押した順。
    std::vector<geometry::Vector3> pickedPoints;
    //! 測った相手の id(選んだ線と、押した点が吸着した線)。寸法を残すときの相手。
    std::vector<base::EntityId> targetIds;
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

//! いまの測定の主な値(残す寸法の値)。距離は mm、角度は rad。決まっていなければ空。
struct MeasurePrimaryValue {
    double value = 0.0;
    std::string unit;   //!< "mm" または "rad"
    std::string kind;   //!< "two_points" / "three_point_angle" / "element" / "selection"
};
[[nodiscard]] std::optional<MeasurePrimaryValue> MeasurePrimary(const MeasureRequest& request);

//! 「寸法を残す」。名前と主な値から、文書へ入れる参照寸法を作る。
//! 値が決まっていない、または測った相手が文書の線でなければ UI-M001 で断る。
[[nodiscard]] base::Result<document::ReferenceDimension> MeasureDimensionOf(
    const MeasureRequest& request, std::string_view label, base::DimensionId id);

} // namespace kachakacha::v2::app
