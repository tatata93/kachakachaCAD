#include "kachakacha/app/Ribbon.h"

namespace kachakacha::v2::app {

namespace {

// modeling::GuideSurfaceMethod の整数値(modeling を include すると層が逆になるので数で持つ)。
constexpr int kPlanar = 0;
constexpr int kRuled = 1;
constexpr int kLoft = 2;
constexpr int kGuidedLoft = 3;
constexpr int kGordon = 4;
constexpr int kBoundaryFill = 5;
constexpr int kOffsetGuide = 6;
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
                Blocked("スケール", "スケールはまだできません(核に線の拡大縮小がありません)"),
                Tool("コピー", "wire.copy"), Extra("直線に並べる", "wire.array_linear"),
                Extra("円に並べる", "wire.array_circular")}},
        {"plane", "作業面",
            {Tool("作業面", "workplane.create"), Tool("選択に正対", "view.align_selection"),
                Tool("作業面に正対", "view.align_workplane"),
                Tool("作業中にする", "workplane.set_active"), Extra("グリッド", "grid.edit"),
                Extra("グリッド原点", "grid.move_origin")}},
        {"surface", "面作成",
            {Surface("平面", kPlanar), Surface("ルールド面", kRuled), Surface("ロフト面", kLoft),
                Surface("ガイド付きロフト", kGuidedLoft), Surface("境界面", kBoundaryFill),
                Surface("曲線網", kGordon), Surface("離した面", kOffsetGuide, true),
                Extra("回転面", "guide.revolve")}},
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
            {Tool("押し出し", "part.extrude"),
                Blocked("回転体", "立体の回転体はまだ作れません。作図モードの面作成「回転面」で面を作り、"
                                  "「厚み」で立体にしてください"),
                Blocked("ロフト立体", "ロフト立体はまだ作れません。面作成のロフト面を作ってから「厚み」で立体にしてください"),
                Blocked("スイープ", "スイープはまだ作れません(核に経路に沿った立体がありません)"),
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
                Blocked("交差", "交差はまだできません(核に共通部分の演算がありません)")}},
        {"place", "配置",
            {Blocked("移動", "部品の移動はまだできません(線の移動はあります。部品の変形は核にありません)"),
                Blocked("回転", "部品の回転はまだできません"),
                Blocked("ミラー", "部品のミラーはまだできません"),
                Blocked("コピー", "部品のコピーはまだできません"),
                Blocked("パターン", "部品のパターンはまだできません(線の配列はあります)")}},
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
                Tool("輪郭 Wire", "fabrication.freeze_state")}},
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
