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
    }
    return "なし";
}

const std::vector<Shelf>& AllShelves()
{
    static const std::vector<Shelf> all{
        Shelf::WorkPlane, Shelf::Drawing, Shelf::Edit, Shelf::Corner, Shelf::Measure,
        Shelf::GuideTable, Shelf::Fabrication, Shelf::Export, Shelf::Grid, Shelf::Display,
        Shelf::Parameter,
    };
    return all;
}

std::vector<Shelf> ShelvesFor(UiMode mode, DrawingTool tool)
{
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
        return {Shelf::GuideTable, Shelf::Parameter};
    case UiMode::Fabrication:
        return {Shelf::Fabrication, Shelf::Parameter};
    case UiMode::Output:
        return {Shelf::Export};
    }
    return {Shelf::Edit};
}

Shelf FrontShelfFor(UiMode mode, DrawingTool tool)
{
    const std::vector<Shelf> shelves = ShelvesFor(mode, tool);
    return shelves.empty() ? Shelf::None : shelves.front();
}

} // namespace kachakacha::v2::app
