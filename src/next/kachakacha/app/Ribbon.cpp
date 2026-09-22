#include "kachakacha/app/Ribbon.h"

namespace kachakacha::v2::app {

namespace {

// modeling::GuideSurfaceMethod の整数値(modeling を include すると層が逆になるので数で持つ)。
constexpr int kPlanar = 0;
constexpr int kRuled = 1;
constexpr int kLoft = 2;
constexpr int kGordon = 4;
constexpr int kBoundaryFill = 5;
constexpr int kOffsetGuide = 6;
constexpr int kFourEdgePatch = 8;
// app::MeasureMode の整数値。
constexpr int kMeasureSelection = 0;
constexpr int kMeasureTwoPoints = 1;
constexpr int kMeasureAngle = 2;
constexpr int kMeasureElement = 3;

[[nodiscard]] RibbonTool Tool(std::string_view label, std::string_view id)
{
    return RibbonTool{label, id, std::nullopt, std::nullopt, {}, false};
}

[[nodiscard]] RibbonTool Extra(std::string_view label, std::string_view id)
{
    return RibbonTool{label, id, std::nullopt, std::nullopt, {}, true};
}

[[nodiscard]] RibbonTool Surface(std::string_view label, int method, bool extra = false)
{
    return RibbonTool{label, "surface.create", method, std::nullopt, {}, extra};
}

[[nodiscard]] RibbonTool Measure(std::string_view label, int mode)
{
    return RibbonTool{label, "measure.open", std::nullopt, mode, {}, false};
}

[[nodiscard]] RibbonTool Blocked(std::string_view label, std::string_view whyJa)
{
    return RibbonTool{label, {}, std::nullopt, std::nullopt, whyJa, false};
}

const std::vector<RibbonCategory>& DrawingCategories()
{
    static const std::vector<RibbonCategory> categories{
        {"basic", "基本作図",
            {Tool("線", "draw.line"), Tool("円", "draw.circle"), Tool("円弧", "draw.arc"),
                Tool("矩形", "draw.rectangle"), Tool("多角形", "draw.polyline"),
                Tool("点", "draw.point")}},
        {"curve", "曲線",
            {Tool("ベジェ", "draw.bezier"), Tool("スプライン", "draw.spline"),
                Blocked("楕円", "楕円はまだ作れません(核に楕円の線がありません)")}},
        {"edit", "編集",
            {Tool("トリム", "wire.trim"), Tool("延長", "wire.extend"), Tool("分割", "wire.split"),
                Tool("結合", "wire.join"), Tool("オフセット", "wire.offset"),
                Tool("面取り", "wire.chamfer"), Tool("丸め", "wire.fillet"),
                Extra("接線でつなぐ", "wire.tangent"), Extra("曲率でつなぐ", "wire.curvature"),
                Extra("端点を合わせる", "wire.coincident"),
                Extra("2本を交点まで", "wire.meet_lines"),
                Extra("角を落とす", "wire.corner_chamfer"), Extra("角を丸める", "wire.corner_fillet"),
                Extra("交点を点に", "wire.intersection_points"),
                Extra("中心を点に", "wire.center_points"), Extra("主要点を点に", "wire.key_points"),
                Extra("平面へ投影", "wire.project"), Extra("面へ投影", "wire.project_surface"),
                Extra("巻き付け投影", "wire.wrap_project"),
                Extra("基準線にする", "wire.set_datum"), Extra("基準線を外す", "wire.clear_datum"),
                Extra("数値で直す", "edit.numeric")}},
        {"transform", "変形",
            {Tool("移動", "wire.move"), Tool("回転", "wire.rotate"), Tool("ミラー", "wire.mirror"),
                Tool("スケール", "wire.scale"),
                Tool("コピー", "wire.copy"), Extra("直線に並べる", "wire.array_linear"),
                Extra("円に並べる", "wire.array_circular")}},
        {"plane", "作業面",
            {Tool("作業面", "workplane.create"), Tool("選択に正対", "view.align_selection"),
                Tool("作業面に正対", "view.align_workplane"),
                Tool("作業中にする", "workplane.set_active"), Extra("グリッド", "grid.edit"),
                Extra("グリッド原点", "grid.move_origin")}},
        {"surface", "面作成",
            // 2026-09-22: 「ガイド付きロフト」は「ロフト面」へ統合(ガイド 0〜任意)。
            // 互換の入口は面の棚の「その他」に残す。空いた場所へ「四辺面」。
            {Surface("平面", kPlanar), Surface("ルールド面", kRuled), Surface("ロフト面", kLoft),
                Surface("境界面", kBoundaryFill), Surface("四辺面", kFourEdgePatch),
                Surface("曲線網", kGordon), Surface("離した面", kOffsetGuide, true),
                Extra("回転面", "guide.revolve"),
                // 面の編集(プロンプト additional_surface_tools)。正本のカテゴリは増やさず、
                // 面作成の後ろ(「その他」)に置く。面を作ったあとに直す・つなぐ道具。
                Extra("面を合わせる", "surface.match"), Extra("面をつなぐ", "surface.bridge"),
                Extra("面を整える", "surface.refit"), Extra("対称に写す", "surface.mirror"),
                Extra("U/V 線", "surface.iso_curves"),
                // 面の解析(プロンプト surface_analysis)。作りながら見るので面作成に並べる。
                Extra("面の解析", "view.surface_analysis"), Extra("ゼブラ", "view.analysis_zebra")}},
        {"note", "注記",
            {Tool("寸法", "measure.open"),
                Blocked("テキスト", "テキストの注記はまだありません(文書に文字の要素がありません)")}},
        {"measure", "測定",
            {Measure("距離", kMeasureTwoPoints), Measure("角度", kMeasureAngle),
                Measure("半径/直径", kMeasureElement), Measure("座標", kMeasureSelection),
                Blocked("面積", "面積の測定はまだありません(閉じた輪郭の面積を測る道が核にありません)")}},
    };
    return categories;
}

const std::vector<RibbonCategory>& PartCategories()
{
    static const std::vector<RibbonCategory> categories{
        {"create", "作成",
            // 回転体・ロフト立体・スイープ(P-08/P-09)。道具から始め、棚で輪郭・軸・経路を選ぶ。
            {Tool("押し出し", "part.extrude"), Tool("回転体", "part.revolve"),
                Tool("ロフト立体", "part.loft_solid"), Tool("スイープ", "part.sweep"),
                Tool("厚み", "part.thicken"), Extra("平面まで厚み", "part.thicken_to_plane"),
                Extra("ワイヤー群から部品", "part.from_wire_cage"), Extra("治具", "part.surface_jig"),
                Extra("現在状態を固定", "derived.freeze")}},
        {"shape", "形状編集",
            {Blocked("フィレット", "立体の辺の丸めはまだできません(核に辺の丸めがありません)"),
                Blocked("面取り", "立体の辺の面取りはまだできません(核に辺の面取りがありません)"),
                Blocked("シェル", "シェルはまだできません(核に面を抜く肉抜きがありません)"),
                Blocked("分割", "立体の分割はまだできません(核に立体を切る道がありません)"),
                Tool("結合", "part.boolean_add")}},
        {"face", "面編集",
            {Tool("押し引き", "part.extrude"),
                Blocked("面オフセット", "面オフセットはまだできません(核に面を動かす道がありません)"),
                Blocked("面削除", "面削除はまだできません"),
                Blocked("面置換", "面置換はまだできません")}},
        {"boolean", "ブール演算",
            {Tool("足す", "part.boolean_add"), Tool("引く", "part.boolean_cut"),
                Tool("交差", "part.boolean_intersect")}},
        {"place", "配置",
            // 線と同じ道具・同じ点の置き方で部品を動かす(P-18)。パターンは線の配列と同じ棚。
            {Tool("移動", "part.move"), Tool("回転", "part.rotate"), Tool("ミラー", "part.mirror"),
                Tool("コピー", "part.copy"), Tool("パターン", "part.array_linear"),
                Extra("円に並べる", "part.array_circular")}},
    };
    return categories;
}

const std::vector<RibbonCategory>& FabricationCategories()
{
    static const std::vector<RibbonCategory> categories{
        {"approx", "近似", {Tool("近似", "fabrication.create")}},
        {"edit", "部材編集",
            {Tool("近似部品編集", "fabrication.edit_part"), Tool("分割", "fabrication.split_part"),
                Tool("結合", "fabrication.merge_parts"), Tool("切れ目", "fabrication.assign_relief_cut"),
                Tool("半径編集", "fabrication.edit_part"),
                Extra("開口 / 折り線", "fabrication.assign_role"),
                Extra("接続する部材の範囲", "fabrication.set_connection_scope")}},
        {"bend", "曲げ・展開",
            {Tool("曲げ状態", "fabrication.edit_part"), Tool("展開", "fabrication.create_pattern"),
                Tool("展開基準辺", "fabrication.set_unfold_base"),
                Blocked("表裏反転", "表裏反転はまだできません(型紙の配置に鏡映がありません)")}},
        {"generate", "生成",
            {Tool("現在形状を生成", "fabrication.freeze_state"),
                Tool("Flat Wire", "fabrication.freeze_flat"),
                Tool("目標形状(100%)を固定", "fabrication.freeze_target"),
                // 以前は fabrication.freeze_state を指す張りぼてで、
                // 「現在形状を生成」と中身が同じだった(指示書 F-14)。
                Tool("輪郭 Wire", "fabrication.freeze_wires")}},
    };
    return categories;
}

const std::vector<RibbonCategory>& OutputCategories()
{
    static const std::vector<RibbonCategory> categories{
        {"export", "書き出し",
            {Tool("出力を検査", "export.validate"), Tool("STL", "export.stl"),
                Tool("STEP", "export.step"), Tool("SVG", "export.svg"), Tool("DXF", "export.dxf"),
                Tool("PDF 原寸", "export.pdf_1to1")}},
        {"view", "表示", {Tool("表示設定", "view.display_settings")}},
    };
    return categories;
}

} // namespace

const std::vector<RibbonCategory>& RibbonCategoriesFor(UiMode mode)
{
    switch (mode) {
    case UiMode::Drawing:     return DrawingCategories();
    case UiMode::Part:        return PartCategories();
    case UiMode::Fabrication: return FabricationCategories();
    case UiMode::Output:      return OutputCategories();
    }
    return DrawingCategories();
}

const RibbonTool* FindRibbonTool(UiMode mode, std::string_view commandId)
{
    for (const RibbonCategory& category : RibbonCategoriesFor(mode)) {
        for (const RibbonTool& tool : category.tools) {
            if (tool.commandId == commandId) {
                return &tool;
            }
        }
    }
    return nullptr;
}

bool RibbonHasCommand(UiMode mode, std::string_view commandId)
{
    return FindRibbonTool(mode, commandId) != nullptr;
}

} // namespace kachakacha::v2::app
