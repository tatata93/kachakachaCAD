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
    //! 形状ガイドの役割の表(旧)。menu の guide.* が書く。
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
    //! 型紙の下見。出来た型紙を紙の形で見る。
    Pattern,
    //! 部品(V1 の部品タブ)。押し出しの距離・板厚・厚みの付け方・治具・回転体。
    Part,
    //! 押し出しの最中だけ出す棚(オーナー指示 2026-09-14 §7)。
    //! 入力・距離・方向・範囲・操作・確定を1枚に置く。
    Extrude,
    //! 「面を作る」の欄(作り方・入力・断面順・状態)。
    //! UI の正本のとおり、4つの段を1枚に置く。
    Surface,
    //! 「足す・引く」の欄(土台・相手・状態)。引継ぎ 2026-09-17 の 4。
    Boolean,
    //! 「厚み」の欄(入力・作り方・厚み・状態)。指示書 matrix P-10。
    Thicken,
    //! 配列(直線/円形)の欄(指示書 D-23)。`wire.array_*` が構えている間だけ出す。
    Array,
    //! 「面の編集」の欄(合わせる・つなぐ・整える・対称・U/V 線・面へ投影)。
    SurfaceEdit,
};

[[nodiscard]] std::string_view ShelfNameJa(Shelf shelf) noexcept;

//! いまの道具とモードで、右に出す棚。並びは前に出るものが先。
//! 空にはならない(どの道具にも、少なくとも1枚は出す棚がある)。
//!
//! `extruding` は「押し出しの下見を出している最中か」。ここを渡さないと
//! `Shelf::Extrude` がどの組み合わせにも現れず、`RefreshRightShelves` が
//! **毎回その棚を隠していた**。棚に値を入れた直後に自分で隠していたので、
//! 押し出しの欄は一度も画面に出ていなかった。
//! `booleaning` は「足す・引くの欄を構えている最中か」。
//! `thickening` は「厚みの欄を構えている最中か」(指示書 matrix P-10)。
//! `editingSurface` は「面の編集の欄を構えている最中か」(自分で出して隠される形にしない)。
[[nodiscard]] std::vector<Shelf> ShelvesFor(UiMode mode, modeling::DrawingTool tool,
    bool extruding = false, bool surfacing = false, bool booleaning = false,
    bool thickening = false, bool editingSurface = false);

//! そのうち前に出す1枚。ShelvesFor の先頭と必ず同じ。
[[nodiscard]] Shelf FrontShelfFor(UiMode mode, modeling::DrawingTool tool,
    bool extruding = false, bool surfacing = false, bool booleaning = false,
    bool thickening = false, bool editingSurface = false);

//! 決まった順に並べた全部。台帳と試験が同じ順を見る。
[[nodiscard]] const std::vector<Shelf>& AllShelves();

} // namespace kachakacha::v2::app
