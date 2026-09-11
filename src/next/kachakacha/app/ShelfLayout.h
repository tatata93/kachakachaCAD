#pragma once

//! 右に出す棚の決め方(オーナー指摘 2026-09-11「右画面は選択ツールの設定だけ」)。
//!
//! V2 は右の枠へ棚を9枚まとめて積んでいた。どれも常に出ているので、
//! 1枚あたりが 80px ほどまで潰れ、見出しだけが並ぶ画面になっていた。
//! V1 は右が **札(タブ)** で、いま使っている道具の欄だけが前に出ていた。
//!
//! ここは「いまの道具とモードなら、どの棚か」だけを決める。
//! 棚そのものは Qt 側にある。決め方を core に置くのは、
//! 画面を出さずに確かめられるようにするためである。
//!
//! **道具を選ぶこと以外では、前に出る棚は変わらない。**
//! 選ぶたびに棚が入れ替わると、打ちかけの数字が消えたように見える。

#include "kachakacha/app/UiMode.h"
#include "kachakacha/modeling/ToolController.h"

#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! 右に出せる棚。画面の QDockWidget と1対1に対応する。
enum class Shelf {
    None,
    //! 作業平面を作る(V1 の「平面を作る」タブ)。
    WorkPlane,
    //! 作図の設定(V1 の「作図」タブ)。円弧の作り方・補助線・数値で線を作る。
    Drawing,
    //! 選んでいるものを数値で直す(V1 の「選択内容の数値編集」)。
    Edit,
    //! 面取り(V1 の「面取り」欄)。
    Corner,
    //! 測る(PRD-070)。
    Measure,
    //! 形状ガイドの役割の表。
    GuideTable,
    //! 製作(V1 の近似モデル画面)。
    Fabrication,
    //! 書き出し。
    Export,
    //! グリッド。
    Grid,
    //! 表示。
    Display,
    //! 数(板厚・面取り量)。
    Parameter,
};

[[nodiscard]] std::string_view ShelfNameJa(Shelf shelf) noexcept;

//! いまの道具とモードで、右に出す棚。並びは前に出るものが先。
//! 空にはならない(どの道具にも、少なくとも1枚は出す棚がある)。
[[nodiscard]] std::vector<Shelf> ShelvesFor(UiMode mode, modeling::DrawingTool tool);

//! そのうち前に出す1枚。ShelvesFor の先頭と必ず同じ。
[[nodiscard]] Shelf FrontShelfFor(UiMode mode, modeling::DrawingTool tool);

//! 決まった順に並べた全部。台帳と試験が同じ順を見る。
[[nodiscard]] const std::vector<Shelf>& AllShelves();

} // namespace kachakacha::v2::app
