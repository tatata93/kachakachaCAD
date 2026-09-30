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
    case Shelf::ShellSplit:   return "シェル・分割";
    case Shelf::LoopFaces:    return "面にする";
    case Shelf::GptFabrication: return "製作近似 GPT版";
    case Shelf::GptSurface: return "面生成 GPT版";
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
        Shelf::Solid, Shelf::EdgeFinish, Shelf::ShellSplit, Shelf::LoopFaces, Shelf::GptSurface, Shelf::GptFabrication,
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
    (void)mode;
    // 道具未選択: ホストが同じモードのツール一覧を表示する。
    return {};
}

Shelf FrontShelfFor(UiMode mode, DrawingTool tool, bool extruding, bool surfacing,
    bool booleaning, bool thickening, bool editingSurface, bool analyzing, Shelf ownedShelf)
{
    const std::vector<Shelf> shelves = ShelvesFor(mode, tool, extruding, surfacing, booleaning,
        thickening, editingSurface, analyzing, ownedShelf);
    return shelves.empty() ? Shelf::None : shelves.front();
}

} // namespace kachakacha::v2::app
