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
    case Shelf::Thicken:    return "厚み";
    case Shelf::Array:      return "配列";
    case Shelf::SurfaceEdit: return "面の編集";
    case Shelf::SurfaceAnalysis: return "面の解析";
    case Shelf::Solid:      return "立体を作る";
    case Shelf::EdgeFinish: return "辺の丸め・面取り";
    }
    return "なし";
}

const std::vector<Shelf>& AllShelves()
{
    static const std::vector<Shelf> all{
        Shelf::WorkPlane, Shelf::Drawing, Shelf::Edit, Shelf::Corner, Shelf::Measure,
        Shelf::GuideTable, Shelf::Fabrication, Shelf::Export, Shelf::Grid, Shelf::Display,
        Shelf::Parameter, Shelf::Pattern, Shelf::Part, Shelf::Extrude, Shelf::Surface,
        Shelf::Boolean, Shelf::Thicken, Shelf::Array, Shelf::SurfaceEdit, Shelf::SurfaceAnalysis,
        Shelf::Solid, Shelf::EdgeFinish,
    };
    return all;
}

std::vector<Shelf> ShelvesFor(UiMode mode, DrawingTool tool, bool extruding,
    bool surfacing, bool booleaning, bool thickening, bool editingSurface, bool analyzing,
    Shelf ownedShelf)
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
    // 「厚み」も同じ。足す・引くのすぐ後(優先度は足す・引くの次)。
    // 面の欄・作り方・厚みが見えていなければ、選び直しも確定も触れない。
    if (thickening) {
        return {Shelf::Thicken};
    }
    // 「面の編集」も同じ。縁・面・線の欄と滑らかさが見えていなければ、確定も触れない。
    if (editingSurface) {
        return {Shelf::SurfaceEdit};
    }
    // 自分の棚を持つ道具(立体を作る・辺の丸め面取り)も同じ。欄が見えていなければ、
    // 選び直しも確定も触れない。
    if (ownedShelf != Shelf::None) {
        return {ownedShelf};
    }
    // 「面の解析」は道具の棚より後ろ。道具を持てば道具の棚が前に出る(解析の表示は残る)。
    if (analyzing) {
        return {Shelf::SurfaceAnalysis};
    }
    switch (tool) {
    case DrawingTool::Measure:
        // 測るときは測る欄だけ。ほかの棚は測る手を邪魔する。
        return {Shelf::Measure};
    case DrawingTool::SetGridOrigin:
        return {Shelf::Grid};
    case DrawingTool::ChamferOrFilletPair:
        // 面取りは面取りの棚1枚(指示書 C-09「一道具一枚」)。
        // 量は面取りの棚が数の棚と同じ値を持ち、SetSizeHandler で数の棚へ
        // 流している(V2Shelves.cpp)ので、量の欄をここに並べなくても
        // CornerSizeMm() は数の棚から読み続けられる。
        return {Shelf::Corner};
    case DrawingTool::Move:
    case DrawingTool::Copy:
    case DrawingTool::Mirror:
    case DrawingTool::Rotate:
    case DrawingTool::Scale:
    case DrawingTool::Split:
    case DrawingTool::Trim:
    case DrawingTool::Extend:
    case DrawingTool::JoinEndpoints:
    case DrawingTool::TangentJoin:
    case DrawingTool::CurvatureJoin:
    case DrawingTool::Point:
    case DrawingTool::Line:
    case DrawingTool::Polyline:
    case DrawingTool::Rectangle:
    case DrawingTool::Circle:
    case DrawingTool::Arc:
    case DrawingTool::Bezier:
    case DrawingTool::Spline:
    case DrawingTool::ConnectTwoPoints:
        // 一道具一枚は「道具のページ」(指示書 C-09、D-15/D-21)。
        // 編集・変形の道具にも作り方カードと次にすることの一文がある
        // (DrawingMethodCards / DrawingShelfRows)。数値で直す欄(Edit)は
        // 「選択」道具のときだけ前に出す。
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
        // 役割の表(GuideTable)はここから外した(指示書 C-09、I-03)。
        // 右は「いまの道具の1枚だけ」なので、部品欄の後ろに表を常設すると
        // 2枚目になってしまう。表は guide.* コマンド自身が ShowShelf で
        // 前に出す(V2GuideTableCommands.cpp)。
        return {Shelf::Part};
    case UiMode::Fabrication:
        // 1枚(指示書 C-09)。板厚・許すずれは製作の棚が SetParameterMm で数の棚へ映すので、
        // 製作の最中に数の棚を並べる必要はない。数そのものを直したいときは
        // 「数の設定」(view.number_settings)が自分で前へ出す。
        return {Shelf::Fabrication};
    case UiMode::Output:
        // 1枚(指示書 C-09)。型紙の下見は `fabrication.create_pattern` が作ったときに
        // 自分で前へ出す(V2FabricationCommands.cpp の ShowShelf(Pattern))。
        // 出力モードへ入っただけで型紙の空の棚を並べると、書き出しの欄が半分に潰れる。
        return {Shelf::Export};
    }
    return {Shelf::Edit};
}

Shelf FrontShelfFor(UiMode mode, DrawingTool tool, bool extruding, bool surfacing,
    bool booleaning, bool thickening, bool editingSurface, bool analyzing, Shelf ownedShelf)
{
    const std::vector<Shelf> shelves = ShelvesFor(mode, tool, extruding, surfacing, booleaning,
        thickening, editingSurface, analyzing, ownedShelf);
    return shelves.empty() ? Shelf::None : shelves.front();
}

} // namespace kachakacha::v2::app
