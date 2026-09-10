#pragma once

//! 書き出し画面の中身(ui-workflows §11、WP-11)。
//!
//! 「出す」ボタンを押すまでに、画面が持っている状態はこれだけである。
//!   - 何を出すか(対象)
//!   - どの形式で出すか
//!   - どこへ出すか(ファイル名)
//!   - 既にあるものへ上書きしてよいか
//!
//! ここに置く理由は2つ。
//!   1. 対象を変えたときに形式をどうするか、という判断を Qt の中に書かない。
//!      書くと、画面を開かないと試験できない。
//!   2. 「なぜ押せないか」を画面で組み立てない。理由はコードで持つ。
//!
//! この層はファイルを書かない。書くのは ExportService::RunExport である。

#include "kachakacha/app/ExportService.h"
#include "kachakacha/app/ProcessSteps.h"
#include "kachakacha/base/Diagnostic.h"

#include <string>
#include <vector>

namespace kachakacha::v2::app {

//! いま何がいくつあるか。画面が数えて渡す。
//! 「表示している」と「選んでいる」を混ぜない(§11.1)。
struct ExportCounts {
    int visibleParts = 0;
    int selectedParts = 0;
    int selectedFabricationPanels = 0;
    int patternPages = 0;
    int selectedWires = 0;
    //! 選んでいるもの(種類を問わない)。別の文書にするときの元。
    int selectedEntities = 0;
    //! 文書そのもの。開いていれば1。
    int project = 0;
};

//! 手順の状況から数を作る。画面が数え直さないようにする。
//! 「表示している部品」は選択と別なので、別に渡してもらう。
[[nodiscard]] ExportCounts ExportCountsFrom(const ProcessContext& context,
    int visiblePartCount, bool hasDocument) noexcept;

//! その対象がいまいくつあるか。
[[nodiscard]] int ExportCountFor(const ExportCounts& counts, ExportTarget target) noexcept;

//! 画面の状態。
struct ExportPanelState {
    ExportTarget target = ExportTarget::SelectedParts;
    ExportFormat format = ExportFormat::Step;
    std::string path;
    bool overwrite = false;
    //! 対象を利用者が自分で選んだか。
    //! 選んでいなければ、数が変わるたびに選び直してよい。
    //! 「この文書」は開いていれば必ず1件あるので、これが無いと
    //! いつまでも文書が選ばれたままになり、選んだワイヤーへ移らない。
    bool targetChosenByUser = false;
};

//! 対象の1行ぶん。数が0の対象も消さずに出す。
//! 消すと「選んでいないから出てこない」のか「そもそも無い」のか分からなくなる。
struct ExportTargetRow {
    ExportTarget target = ExportTarget::SelectedParts;
    std::string labelJa;
    int count = 0;
    //! 数が0なら選べない。
    bool selectable = false;
};

//! 形式の1行ぶん。
struct ExportFormatRow {
    ExportFormat format = ExportFormat::Step;
    std::string labelJa;
    bool selectable = false;
};

//! 対象の並び。決まった順で、いつも同じ数だけ返す。
[[nodiscard]] std::vector<ExportTargetRow> BuildExportTargetRows(const ExportCounts& counts);

//! いまの対象で選べる形式の並び。選べないものも印を付けて残す。
[[nodiscard]] std::vector<ExportFormatRow> BuildExportFormatRows(ExportTarget target);

//! 画面を開いたときの初期状態。
//! 数がある対象のうち、並びの先頭のものを選ぶ。全部0なら先頭の対象のまま。
[[nodiscard]] ExportPanelState BeginExportPanel(const ExportCounts& counts);

//! 数が変わったときの選び直し。
//! 利用者が自分で対象を選んでいれば、それが今も出せる限り動かさない。
//! 選んでいなければ、いま出せるものへ移す。出力先と上書きの承諾はそのまま持ち越す。
[[nodiscard]] ExportPanelState RetargetForCounts(const ExportPanelState& state,
    const ExportCounts& counts);

//! 対象を変える。
//! いまの形式がその対象で使えるなら、形式は変えない(利用者の指定を勝手に消さない)。
//! 使えないなら、その対象で使える先頭の形式へ移す。
[[nodiscard]] base::Result<ExportPanelState> SetExportPanelTarget(
    const ExportPanelState& state, ExportTarget target, const ExportCounts& counts);

//! 形式を変える。組合せが成り立たなければ断る(EXP-017)。勝手に対象は変えない。
[[nodiscard]] base::Result<ExportPanelState> SetExportPanelFormat(
    const ExportPanelState& state, ExportFormat format);

//! 出力先を決める。拡張子はここでは足さない(足すのは出す直前)。
[[nodiscard]] ExportPanelState SetExportPanelPath(const ExportPanelState& state,
    const std::string& path);

[[nodiscard]] ExportPanelState SetExportPanelOverwrite(const ExportPanelState& state,
    bool overwrite);

//! 「出す」を押せるか。押せないときは、そのままの理由を返す。
//! 中身は作らないので、ここでは重い検査はしない。
[[nodiscard]] base::Result<ExportRequest> ToExportRequest(const ExportPanelState& state,
    const ExportCounts& counts);

//! 押せるかどうかだけを見る。
[[nodiscard]] bool CanRunExport(const ExportPanelState& state, const ExportCounts& counts);

//! 押せない理由の一文。押せるときは空。
[[nodiscard]] std::string ExportBlockReasonJa(const ExportPanelState& state,
    const ExportCounts& counts);

//! 出したあとに画面へ出す一文。
[[nodiscard]] std::string ExportOutcomeTextJa(const ExportOutcome& outcome);

//! いまの選びを1行にしたもの。帯に出す。
[[nodiscard]] std::string ExportSelectionTextJa(const ExportPanelState& state,
    const ExportCounts& counts);

} // namespace kachakacha::v2::app
