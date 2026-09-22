#include "kachakacha/app/DrawingShelfRows.h"

namespace kachakacha::v2::app {

using modeling::DrawingTool;

DrawingShelfRows DrawingShelfRowsFor(DrawingTool tool) noexcept
{
    DrawingShelfRows rows;
    switch (tool) {
    case DrawingTool::Arc:
        // 円弧だけが「作り方」を選ぶ。3点か、両端と半径か、始点と接線か。
        rows.arc = true;
        rows.construction = true;
        rows.keepPoints = true;
        break;
    case DrawingTool::Bezier:
        // ベジェだけ、制御多角形を補助線として残せる(D-11)。
        rows.controlPolygon = true;
        rows.construction = true;
        rows.keepPoints = true;
        break;
    case DrawingTool::Scale:
        // スケールだけ、倍率の欄がある(D-22)。
        rows.scale = true;
        break;
    case DrawingTool::Point:
    case DrawingTool::Line:
    case DrawingTool::Polyline:
    case DrawingTool::Rectangle:
    case DrawingTool::Circle:
    case DrawingTool::Spline:
    case DrawingTool::ConnectTwoPoints:
        rows.construction = true;
        rows.keepPoints = true;
        break;
    default:
        // 選択・変換・測定などは、この棚の欄を使わない。
        break;
    }
    return rows;
}

std::string DrawingShelfTitleJa(DrawingTool tool)
{
    return "作図 ― " + std::string(modeling::DrawingToolNameJa(tool));
}

std::string_view DrawingToolHintJa(DrawingTool tool) noexcept
{
    switch (tool) {
    case DrawingTool::Select:
        return "拾いたいものを押してください。Ctrl で足し引き、Tab で重なりを送ります。";
    case DrawingTool::SetGridOrigin:
        return "格子の原点にしたい場所を押してください。";
    case DrawingTool::Point:
        return "作図点を置きたい場所を押してください。";
    case DrawingTool::Line:
        return "始点と終点の2か所を押してください。Shift で水平・垂直に固定します。";
    case DrawingTool::Polyline:
        return "点を順に押してください。Enter で閉じずに終わり、始点を押すと閉じます。";
    case DrawingTool::Rectangle:
        return "向かい合う角の2か所を押してください。Shift で正方形になります。";
    case DrawingTool::Circle:
        return "中心と、円周の1点を押してください。";
    case DrawingTool::Arc:
        return "右の「作り方」のカードで決め方を選んでから押してください。";
    case DrawingTool::Bezier:
        return "始点・制御点2つ・終点の4か所を押してください。決める欄はありません。";
    case DrawingTool::Spline:
        return "通したい点を順に押してください。Enter で終わります。決める欄はありません。";
    case DrawingTool::Move:
        return "動かすものを押し、次に動かす元の点と先の点を押してください。";
    case DrawingTool::Copy:
        return "複製するものを押し、次に元の点と先の点を押してください。";
    case DrawingTool::Mirror:
        return "映すものを押し、次に鏡の線の2点を押してください。";
    case DrawingTool::Rotate:
        return "回すものを押し、次に中心と、回す前後の向きを押してください。";
    case DrawingTool::Split:
        return "切りたい線の、切る場所を押してください。";
    case DrawingTool::Trim:
        return "消したい側を押してください。境目になる線が要ります。";
    case DrawingTool::Extend:
        return "伸ばしたい線の、伸ばす側の端を押してください。";
    case DrawingTool::JoinEndpoints:
        return "つなぎたい端点を2つ押してください。";
    case DrawingTool::TangentJoin:
        return "接線でつなぐ2本の端を押してください。";
    case DrawingTool::CurvatureJoin:
        return "曲率までそろえてつなぐ2本の端を押してください。";
    case DrawingTool::Measure:
        return "測りたい点を押してください。結果は測る棚に出ます。";
    case DrawingTool::ConnectTwoPoints:
        return "結びたい点を2つ押してください。";
    case DrawingTool::ChamferOrFilletPair:
        return "角を落とす2本を押してください。量は右の欄で決めます。";
    case DrawingTool::Scale:
        return "大きさを変えるものを押し、次に中心を押してください(倍率は右の欄)。"
               "「基準の2点」なら中心・基準の点・行き先の点の3か所を押します。";
    }
    return "画面を押して進めてください。";
}

} // namespace kachakacha::v2::app
