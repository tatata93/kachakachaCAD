#include "kachakacha/app/DrawingMethodCards.h"
#include "kachakacha/app/DrawingShelfRows.h"

#include <string>

namespace kachakacha::v2::app {

using modeling::ArcMode;
using modeling::DrawingTool;

namespace {

[[nodiscard]] DrawingMethodCard Card(const char* labelJa, const char* hintJa,
    const char* cursorFieldId = "")
{
    DrawingMethodCard card;
    card.labelJa = labelJa;
    card.hintJa = hintJa;
    card.cursorFieldId = cursorFieldId;
    return card;
}

//! 編集・変形の道具のカード。一文は道具の使い方(DrawingToolHintJa)と同じにする。
//! 別々に書くと、カードの一文だけ直して使い方の一文を直し忘れる、が起きる。
[[nodiscard]] DrawingMethodCard ToolCard(const char* labelJa, DrawingTool tool)
{
    DrawingMethodCard card;
    card.labelJa = labelJa;
    card.hintJa = std::string(DrawingToolHintJa(tool));
    return card;
}

[[nodiscard]] DrawingMethodCard ArcCard(const char* labelJa, ArcMode mode, const char* hintJa,
    bool extra = false)
{
    DrawingMethodCard card = Card(labelJa, hintJa);
    card.arcMode = mode;
    card.extra = extra;
    return card;
}

[[nodiscard]] DrawingMethodCard CircleCard(const char* labelJa, modeling::CircleMode mode,
    const char* hintJa, const char* cursorFieldId = "")
{
    DrawingMethodCard card = Card(labelJa, hintJa, cursorFieldId);
    card.circleMode = mode;
    return card;
}

[[nodiscard]] DrawingMethodCard SplineCard(const char* labelJa, modeling::SplineMode mode,
    const char* hintJa)
{
    DrawingMethodCard card = Card(labelJa, hintJa);
    card.splineMode = mode;
    return card;
}

[[nodiscard]] DrawingMethodCard ScaleCard(const char* labelJa, modeling::ScaleMode mode,
    const char* hintJa)
{
    DrawingMethodCard card = Card(labelJa, hintJa);
    card.scaleMode = mode;
    return card;
}

[[nodiscard]] DrawingMethodCard Blocked(const char* labelJa, const char* reasonJa)
{
    DrawingMethodCard card;
    card.labelJa = labelJa;
    card.blockedReasonJa = reasonJa;
    card.hintJa = reasonJa;
    return card;
}

} // namespace

std::vector<DrawingMethodCard> DrawingMethodCardsFor(DrawingTool tool)
{
    switch (tool) {
    case DrawingTool::Line:
        return {
            Card("2点", "始点と終点の2か所を押してください。Shift で水平・垂直に固定します。"),
            Card("点＋長さ＋角度",
                "始点を押してから、カーソル横の欄に長さを打ち、Tab で角度へ移って Enter。",
                "length"),
        };
    case DrawingTool::Circle:
        return {
            CircleCard("中心＋半径", modeling::CircleMode::CenterRadius,
                "中心を押し、次に円周の1点を押してください。半径は欄にも打てます。"),
            CircleCard("直径指定", modeling::CircleMode::CenterRadius,
                "中心を押してから、カーソル横の「直径」の欄に打って Enter(欄はここへ移してあります)。",
                "diameter"),
            CircleCard("3点", modeling::CircleMode::ThreePoints,
                "円周が通る3か所を順に押してください(一直線に並べると円が決まりません)。"),
        };
    case DrawingTool::Arc:
        return {
            ArcCard("3点", ArcMode::ThreePoints, "始点・通過点・終点の3か所を押してください。"),
            ArcCard("始点・終点・半径", ArcMode::EndpointsAndRadius,
                "始点と終点を押してください。半径は下の欄で決めます。"),
            ArcCard("中心・始点・終点", ArcMode::CenterStartEnd,
                "中心・始点・終点の3か所を押してください。半径は中心から始点まで、終点は向きだけを使い、"
                "左回り(反時計回り)に進みます。"),
            ArcCard("始点接線・半径・中心角", ArcMode::StartTangent,
                "始点と接線の向きを押してください。半径と中心角は下の欄で決めます。", true),
        };
    case DrawingTool::Bezier:
        return {
            Card("制御点で作成", "始点・制御点2つ・終点の4か所を押してください(3次)。"),
        };
    case DrawingTool::Spline:
        return {
            SplineCard("制御点", modeling::SplineMode::ControlPoints,
                "制御点を4つ以上、順に押してください。Enter で終わります。"),
            SplineCard("通過点", modeling::SplineMode::ThroughPoints,
                "通る点を3つ以上、順に押してください。どの点も必ず通ります。Enter で終わります。"),
            Blocked("近似 / Fit", "点列への当てはめはまだ作れません(核に当てはめがありません)。"),
        };
    case DrawingTool::Rectangle:
        return {Card("2角", "向かい合う角の2か所を押してください。Shift で正方形になります。")};
    case DrawingTool::Polyline:
        return {Card("頂点を順に", "点を順に押してください。右クリックか Enter で終わり、始点を押すと閉じます。")};
    case DrawingTool::Point:
        return {Card("1点", "作図点を置きたい場所を押してください。")};
    case DrawingTool::Move:
        // 「点＋距離＋角度」は CursorInput 側がまだ角度欄を持たない
        // (CursorFieldsFor(Move) は distance だけ)。無い作り方をカードにすると
        // 「押せるが何も起きない」になるので、いまは2点の1枚だけ。
        return {ToolCard("2点", tool)};
    case DrawingTool::Copy:
        return {ToolCard("2点", tool)};
    case DrawingTool::Mirror:
        return {ToolCard("鏡の線 2点", tool)};
    case DrawingTool::Rotate:
        return {ToolCard("中心+2方向", tool)};
    case DrawingTool::Scale:
        return {
            ScaleCard("倍率", modeling::ScaleMode::Factor,
                "大きさを変えるものを押し、次に中心を1か所押してください。倍率は下の欄で決めます。"),
            ScaleCard("基準の2点", modeling::ScaleMode::Reference,
                "中心・基準の点・行き先の点の3か所を押してください。倍率は「中心から行き先」÷「中心から基準」。"),
        };
    case DrawingTool::Split:
        return {ToolCard("交点で分ける", tool)};
    case DrawingTool::Trim:
        return {ToolCard("消したい区間を押す", tool)};
    case DrawingTool::Extend:
        return {ToolCard("伸ばす端を押す", tool)};
    case DrawingTool::JoinEndpoints:
        return {ToolCard("端点 2 つ", tool)};
    case DrawingTool::TangentJoin:
        return {ToolCard("接線", tool)};
    case DrawingTool::CurvatureJoin:
        return {ToolCard("曲率", tool)};
    case DrawingTool::Select:
    case DrawingTool::SetGridOrigin:
    case DrawingTool::ConnectTwoPoints:
    case DrawingTool::ChamferOrFilletPair:
    case DrawingTool::Measure:
        break;
    }
    return {};
}

int CurrentDrawingMethodIndex(DrawingTool tool, const modeling::ToolSettings& settings)
{
    const auto cards = DrawingMethodCardsFor(tool);
    if (cards.empty()) {
        return -1;
    }
    if (tool == DrawingTool::Arc) {
        for (std::size_t index = 0; index < cards.size(); ++index) {
            if (cards[index].arcMode.has_value() && *cards[index].arcMode == settings.arcMode) {
                return static_cast<int>(index);
            }
        }
    }
    if (tool == DrawingTool::Circle) {
        for (std::size_t index = 0; index < cards.size(); ++index) {
            if (cards[index].circleMode.has_value()
                && *cards[index].circleMode == settings.circleMode) {
                return static_cast<int>(index);
            }
        }
    }
    if (tool == DrawingTool::Spline) {
        for (std::size_t index = 0; index < cards.size(); ++index) {
            if (cards[index].splineMode.has_value()
                && *cards[index].splineMode == settings.splineMode) {
                return static_cast<int>(index);
            }
        }
    }
    if (tool == DrawingTool::Scale) {
        for (std::size_t index = 0; index < cards.size(); ++index) {
            if (cards[index].scaleMode.has_value() && *cards[index].scaleMode == settings.scaleMode) {
                return static_cast<int>(index);
            }
        }
    }
    for (std::size_t index = 0; index < cards.size(); ++index) {
        if (!cards[index].Blocked()) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

} // namespace kachakacha::v2::app
