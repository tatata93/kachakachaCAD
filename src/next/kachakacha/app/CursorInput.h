#pragma once

//! カーソル連動の数値入力(ui-workflows §7、AT-UIX-003)。
//!
//! 最初の点を置いた直後、カーソルの右下16pxに小さな入力列を出し、
//! 最初の主要寸法欄へ自動で焦点を合わせる。Qt には依存しない。
//! 画面が持つのは「どこへ描くか」だけで、欄の並びも、式の評価も、
//! 何を解くかも、すべてここにある。
//!
//! 決まりが4つある。
//!   1. 数値を確定した欄はロックされ、マウスを動かしても変わらない。
//!   2. 入力中もマウス移動でプレビューは更新する(ロックしていない欄だけ)。
//!   3. 式と評価値を同時に出す。例: (180/2)*3 = 270 mm。
//!   4. 過剰拘束や矛盾のときは、その欄を赤くして、最後の変更だけ確定しない。
//!      入力列ごと消したり、勝手に別の値へ寄せたりしない。
//!
//! V1 は数値入力とマウスが別々の道で、片方を使うともう片方の値が黙って捨てられた。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/Expression.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/ToolController.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

using geometry::QuantityKind;
using geometry::Vector3;
using modeling::DrawingTool;

//! 入力列を出す位置。カーソルからの離し方(px)。
inline constexpr double kCursorPanelOffsetPx = 16.0;

//! 欄1つの定義。道具ごとに決まっていて、実行中に増えたり減ったりしない。
struct CursorField {
    std::string id;          //!< 機械が見る安定ID。"length" など
    std::string labelJa;
    QuantityKind kind = QuantityKind::Length;
    //! 最初に焦点を合わせる主要寸法欄か。道具ごとにちょうど1つ。
    bool primary = false;
};

//! 欄1つの、いまの状態。
struct CursorFieldState {
    std::string text;        //!< 入力中の式そのまま
    bool locked = false;     //!< 確定済み。マウスで動かない
    double value = 0.0;      //!< 長さはmm、角度はrad
    bool hasValue = false;
    bool error = false;      //!< 赤表示
    std::string messageJa;   //!< 赤いときの理由
};

struct CursorInputPanel {
    DrawingTool tool = DrawingTool::Line;
    //! 作業平面の上で描いているか。直線は欄立てが変わる。
    bool onWorkPlane = true;
    bool active = false;
    std::vector<CursorField> fields;
    std::vector<CursorFieldState> states;
    std::size_t focusedIndex = 0;
};

//! その道具の欄立て。カーソル入力を使わない道具では空になる。
[[nodiscard]] const std::vector<CursorField>& CursorFieldsFor(DrawingTool tool,
    bool onWorkPlane);
[[nodiscard]] bool ToolUsesCursorInput(DrawingTool tool);

//! 最初の点を置いた直後に呼ぶ。主要寸法欄へ焦点が合った状態で返る。
[[nodiscard]] base::Result<CursorInputPanel> BeginCursorInput(DrawingTool tool,
    bool onWorkPlane);

//! Tab / Shift+Tab。端では回り込む。
[[nodiscard]] base::Result<CursorInputPanel> FocusNextField(const CursorInputPanel& panel,
    bool backward);
//! 欄を名前で選ぶ(右パネルから触ったとき)。
[[nodiscard]] base::Result<CursorInputPanel> FocusField(const CursorInputPanel& panel,
    std::string_view fieldId);

//! 文字を入れる。ここではまだ評価しない(打っている途中で赤くしないため)。
[[nodiscard]] base::Result<CursorInputPanel> SetFieldText(const CursorInputPanel& panel,
    std::size_t index, std::string_view text);

//! Enter。いまの欄を評価してロックする。
struct CursorCommitResult {
    CursorInputPanel panel;
    //! 必要な欄がそろって、Featureを確定してよいか。
    bool readyToFinish = false;
};
[[nodiscard]] base::Result<CursorCommitResult> CommitFocusedField(
    const CursorInputPanel& panel, const Vector3& pointerDelta);

//! Esc。コマンドごと取り消す。
[[nodiscard]] CursorInputPanel CancelCursorInput(const CursorInputPanel& panel);

//! マウスが動いた。ロックしていない欄だけ書き換える。
[[nodiscard]] base::Result<CursorInputPanel> UpdateFromPointer(const CursorInputPanel& panel,
    const Vector3& pointerDelta);

//! いまの欄の値から決まる形。ロックと矛盾するなら値を返さない。
[[nodiscard]] base::Result<Vector3> SolveDelta(const CursorInputPanel& panel,
    const Vector3& pointerDelta);

//! 「(180/2)*3 = 270 mm」。式が空なら評価値だけを出す。
[[nodiscard]] std::string FieldDisplayJa(const CursorField& field,
    const CursorFieldState& state);

//! 入力列を置く場所。画面外へ出るときは左または上へ寄せる。
struct CursorPanelPlacement {
    double xPx = 0.0;
    double yPx = 0.0;
    bool flippedHorizontally = false;
    bool flippedVertically = false;
};
[[nodiscard]] CursorPanelPlacement PlaceCursorPanel(double cursorXPx, double cursorYPx,
    double panelWidthPx, double panelHeightPx, double viewWidthPx, double viewHeightPx);

} // namespace kachakacha::v2::app
