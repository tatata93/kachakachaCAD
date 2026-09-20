// 作図の「作り方」カード(正本 3 HTML 2026-09-18、指示書 D-01〜D-14)。
#include "kachakacha/app/CursorInput.h"
#include "kachakacha/app/DrawingMethodCards.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::CurrentDrawingMethodIndex;
using kachakacha::v2::app::DrawingMethodCard;
using kachakacha::v2::app::DrawingMethodCardsFor;
using kachakacha::v2::modeling::ArcMode;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::modeling::ToolSettings;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

KACHA_V2_TEST(drawing_method_cards, 正本の作り方が並び核に無いものは理由つき)
{
    const auto circle = DrawingMethodCardsFor(DrawingTool::Circle);
    Require(circle.size() == 3, "円は3枚");
    RequireEqual(circle[0].labelJa, std::string("中心＋半径"), "先頭は中心＋半径");
    Require(!circle[0].Blocked() && !circle[1].Blocked(), "中心＋半径と直径指定は押せる");
    Require(circle[2].labelJa == "3点" && circle[2].Blocked(), "3点円は押せない");
    Require(circle[2].blockedReasonJa.find("核") != std::string::npos, "理由は核に無いこと");

    const auto spline = DrawingMethodCardsFor(DrawingTool::Spline);
    Require(spline.size() == 3, "スプラインは3枚");
    Require(!spline[0].Blocked() && spline[1].Blocked() && spline[2].Blocked(),
        "制御点だけ押せる");
    Require(DrawingMethodCardsFor(DrawingTool::Line).size() == 2, "線は2点と点＋長さ＋角度");
    Require(DrawingMethodCardsFor(DrawingTool::Select).empty(), "選択に作り方は無い");
}

KACHA_V2_TEST(drawing_method_cards, 円弧のカードは作り方を決め既存の始点接線を失わない)
{
    const auto arc = DrawingMethodCardsFor(DrawingTool::Arc);
    Require(arc.size() == 4, "円弧は正本3 + その他1");
    Require(arc[0].arcMode.has_value() && *arc[0].arcMode == ArcMode::ThreePoints, "3点");
    Require(arc[1].arcMode.has_value() && *arc[1].arcMode == ArcMode::EndpointsAndRadius,
        "始点・終点・半径");
    Require(arc[2].Blocked() && !arc[2].arcMode.has_value(), "中心・始点・終点は押せない");
    Require(arc[3].arcMode.has_value() && *arc[3].arcMode == ArcMode::StartTangent && arc[3].extra,
        "始点接線はその他として残る");
    ToolSettings settings;
    settings.arcMode = ArcMode::StartTangent;
    Require(CurrentDrawingMethodIndex(DrawingTool::Arc, settings) == 3, "設定に当たるカード");
    settings.arcMode = ArcMode::EndpointsAndRadius;
    Require(CurrentDrawingMethodIndex(DrawingTool::Arc, settings) == 1, "設定に当たるカード");
    Require(CurrentDrawingMethodIndex(DrawingTool::Circle, settings) == 0, "円は最初の押せるカード");
    Require(CurrentDrawingMethodIndex(DrawingTool::Select, settings) == -1, "無ければ -1");
}

KACHA_V2_TEST(drawing_method_cards, 欄を名指しするカードは実在する欄を指す)
{
    // 「押しても何も起きないカード」を作らないために、カードは入力欄を名指しできる。
    // 名指しが綴り違いだと、押しても焦点が動かず、また何も起きないカードに戻ってしまう。
    const auto circle = DrawingMethodCardsFor(DrawingTool::Circle);
    RequireEqual(circle[1].cursorFieldId, std::string("diameter"), "直径指定は直径の欄");
    Require(circle[0].cursorFieldId.empty(), "中心＋半径は既定の欄のまま");
    const auto line = DrawingMethodCardsFor(DrawingTool::Line);
    RequireEqual(line[1].cursorFieldId, std::string("length"), "点＋長さ＋角度は長さの欄");
    // 名指しした欄が、その道具の入力列に本当にあるか。
    const auto circleFields = kachakacha::v2::app::CursorFieldsFor(DrawingTool::Circle, true);
    bool found = false;
    for (const auto& field : circleFields) {
        found = found || field.id == "diameter";
    }
    Require(found, "円の入力列に 直径 の欄がある");
}

KACHA_V2_TEST(drawing_method_cards, どのカードにも次にすることの一文がある)
{
    for (const DrawingTool tool : {DrawingTool::Point, DrawingTool::Line, DrawingTool::Polyline,
             DrawingTool::Rectangle, DrawingTool::Circle, DrawingTool::Arc, DrawingTool::Bezier,
             DrawingTool::Spline,
             // 編集・変形の道具も道具のページを持つ(指示書 D-15/D-21)。
             DrawingTool::Move, DrawingTool::Copy, DrawingTool::Mirror, DrawingTool::Rotate,
             DrawingTool::Split, DrawingTool::Trim, DrawingTool::Extend,
             DrawingTool::JoinEndpoints, DrawingTool::TangentJoin, DrawingTool::CurvatureJoin}) {
        const auto cards = DrawingMethodCardsFor(tool);
        Require(!cards.empty(), "作図の道具には1枚以上");
        for (const DrawingMethodCard& card : cards) {
            Require(!card.labelJa.empty() && !card.hintJa.empty(), "名前と一文が空でない");
        }
    }
}

KACHA_V2_TEST_MAIN("drawing_method_cards_tests")
