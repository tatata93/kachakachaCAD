#include "kachakacha/app/ShelfLayout.h"

namespace kachakacha::v2::app {

using modeling::DrawingTool;

std::string_view ShelfNameJa(Shelf shelf) noexcept
{
    switch (shelf) {
    case Shelf::None:        return "なし";
    case Shelf::WorkPlane:   return "作業平面";
    case Shelf::Drawing:     return "作図";
    case Shelf::Edit:        return "編集";
    case Shelf::Corner:      return "面取り";
    case Shelf::Measure:     return "測る";
    case Shelf::GuideTable:  return "形状ガイドの役割";
    case Shelf::Fabrication: return "製作";
    case Shelf::Export:      return "書き出し";
    case Shelf::Grid:        return "グリッド";
    case Shelf::Display:     return "表示";
    case Shelf::Parameter:   return "数";
    case Shelf::Pattern:     return "型紙の下見";
    case Shelf::Part:        return "部品";
    case Shelf::Extrude:    return "押し出し";
    case Shelf::Surface:    return "面を作る";
    case Shelf::Boolean:    return "足す・引く";
    }
    return "なし";
}

const std::vector<Shelf>& AllShelves()
{
    static const std::vector<Shelf> all{
        Shelf::WorkPlane, Shelf::Drawing, Shelf::Edit, Shelf::Corner, Shelf::Measure,
        Shelf::GuideTable, Shelf::Fabrication, Shelf::Export, Shelf::Grid, Shelf::Display,
        Shelf::Parameter, Shelf::Pattern, Shelf::Part, Shelf::Extrude, Shelf::Surface,
        Shelf::Boolean,
    };
    return all;
}

std::vector<Shelf> ShelvesFor(UiMode mode, DrawingTool tool, bool extruding,
    bool surfacing, bool booleaning)
{
    // 下見を出している間は、その操作の棚が前に出る。
    // **道具やモードより優先する。** いま手をつけている操作の欄が
    // 見えていなければ、距離も向きも演算も確定も触れない。
    // 右は「いまの道具の1枚だけ」(正本 3 HTML 2026-09-18、指示書 C-09)。
    // 2枚目に部品の欄を添えていたが、押し出しの最中に板厚の欄が並ぶと
    // どちらの距離が効くのか読めなくなる。
    if (extruding) {
        return {Shelf::Extrude};
    }
    // 「面を作る」の最中も同じ。作り方・入力・断面順・状態が見えていなければ、
    // 方式も役割も順序も触れない。
    if (surfacing) {
        return {Shelf::Surface};
    }
    // 「足す・引く」も同じ。土台・相手の欄が見えていなければ、選び直しも確定も触れない。
    if (booleaning) {
        return {Shelf::Boolean};
    }
    switch (tool) {
    case DrawingTool::Measure:
        // 測るときは測る欄だけ。ほかの棚は測る手を邪魔する。
        return {Shelf::Measure};
    case DrawingTool::SetGridOrigin:
        return {Shelf::Grid};
    case DrawingTool::ChamferOrFilletPair:
        // 面取りは量を数の棚と分け合う。量だけ別の札にあると往復になる。
        return {Shelf::Corner, Shelf::Parameter};
    case DrawingTool::Move:
    case DrawingTool::Copy:
    case DrawingTool::Mirror:
    case DrawingTool::Rotate:
    case DrawingTool::Split:
    case DrawingTool::Trim:
    case DrawingTool::Extend:
    case DrawingTool::JoinEndpoints:
    case DrawingTool::TangentJoin:
    case DrawingTool::CurvatureJoin:
        // 直す道具は、選んだものの数値を見ながら使う。
        return {Shelf::Edit};
    case DrawingTool::Point:
    case DrawingTool::Line:
    case DrawingTool::Polyline:
    case DrawingTool::Rectangle:
    case DrawingTool::Circle:
    case DrawingTool::Arc:
    case DrawingTool::Bezier:
    case DrawingTool::Spline:
    case DrawingTool::ConnectTwoPoints:
        return {Shelf::Drawing};
    case DrawingTool::Select:
        break;
    }
    // 選択道具のときだけ、モードで変わる。
    // 「いま何を相手にしているか」がモードで決まるためである。
    switch (mode) {
    case UiMode::Drawing:
        // 選んでいるものを数値で直す欄。これが V1 の「選択内容の数値編集」に当たる。
        return {Shelf::Edit};
    case UiMode::Part:
        // 道具の設定を前に出す。V1 の部品タブに当たる(オーナー指摘 2026-09-11)。
        // 役割の表は札として後ろに残す。表だけ出していたので、板厚も厚みの
        // 付け方も治具のすき間も、右のどこにも無かった。
        return {Shelf::Part, Shelf::GuideTable};
    case UiMode::Fabrication:
        return {Shelf::Fabrication, Shelf::Parameter};
    case UiMode::Output:
        // 出す前に型紙を見る。見ないまま出すと、紙とプラ板を無駄にしてから気づく。
        return {Shelf::Export, Shelf::Pattern};
    }
    return {Shelf::Edit};
}

Shelf FrontShelfFor(UiMode mode, DrawingTool tool, bool extruding, bool surfacing,
    bool booleaning)
{
    const std::vector<Shelf> shelves =
        ShelvesFor(mode, tool, extruding, surfacing, booleaning);
    return shelves.empty() ? Shelf::None : shelves.front();
}

} // namespace kachakacha::v2::app
