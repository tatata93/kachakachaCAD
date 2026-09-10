#pragma once

//! モードごとの手順(ui-workflows §9 / §10 / §11、AT-UIX-001)。
//!
//! 右のパネルは「1本の手順」として並べる。いま何段目にいて、
//! 次へ進むには何が要るのかを、画面ではなくここが決める。
//!
//! こうする理由は2つ。
//!   1. 「進めない理由」を必ず言えるようにするため。理由の無い灰色のボタンは、
//!      利用者にとって行き止まりである(AT-UIX-002 と同じ考え)。
//!   2. 画面を出さずに手順そのものを試験できるようにするため。
//!
//! V1 は手順が画面の中に散らばっていて、どこから始めればいいのかが
//! 使う人にも作る人にも分からなかった。

#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/base/Diagnostic.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

//! その段がいまどうなっているか。
enum class StepState {
    //! まだ手が付いていない。
    NotStarted,
    //! 手が付いているが、まだ足りない。
    InProgress,
    //! この段は済んでいる。
    Done,
    //! 前の段が済んでいないので、まだ入れない。
    Blocked,
};

[[nodiscard]] std::string_view StepStateNameJa(StepState state) noexcept;

//! 手順の1段。
struct ProcessStep {
    //! 機械が見る安定ID。
    std::string id;
    //! 1始まりの番号。画面にそのまま出す。
    int number = 1;
    std::string titleJa;
    StepState state = StepState::NotStarted;
    //! 入れないときの理由。Blocked のときは必ず入る。
    std::string blockedReasonJa;
    //! この段で使うコマンドの台帳ID。空でもよい。
    std::vector<std::string> commandIds;
};

//! いまの状況。手順の進み具合はここから決まる。
//!
//! 画面が数え直さないよう、必要な数だけをここへ集める。
struct ProcessContext {
    //! 選ばれている部品の数。
    int selectedPartCount = 0;
    //! 選ばれているワイヤーの数。
    int selectedWireCount = 0;
    //! 選ばれているものの数(種類を問わない)。別の文書にするときの元。
    int selectedEntityCount = 0;
    //! 形状ガイドの役割テーブルに入っている行の数。
    int guideRowCount = 0;
    //! 押し出しの入力輪郭の数。
    int extrudeProfileCount = 0;
    //! 押し出しで出すものが1つ以上選ばれているか。
    bool extrudeHasOutput = false;
    //! 製作モデルが出来ているか。
    bool fabricationBuilt = false;
    //! 出来た部材の数。
    int panelCount = 0;
    //! 型紙が出来ているか。
    bool patternBuilt = false;
    //! 出力の対象が選ばれているか。
    bool exportTargetChosen = false;
    //! 出力前の検査を通ったか。
    bool exportValidated = false;
};

//! そのモードの手順を作る。番号は1から順に振られる。
[[nodiscard]] std::vector<ProcessStep> BuildProcessSteps(UiMode mode,
    const ProcessContext& context);

//! いま入れる段(Blocked でない最初の未完了の段)の番号。全部済んでいれば0。
[[nodiscard]] int CurrentStepNumber(const std::vector<ProcessStep>& steps);

//! 出力の直前に出す検査の要約(ui-workflows §11.2)。
struct ExportSummary {
    int targetCount = 0;
    int closedSolidCount = 0;
    int openingCount = 0;
    double minimumThicknessMm = 0.0;
    double assemblyPercent = 0.0;
    int warningCount = 0;
    std::vector<std::string> linesJa;
};

//! 検査の要約を組み立てる。数だけでなく、そのまま画面へ出す行も作る。
[[nodiscard]] base::Result<ExportSummary> BuildExportSummary(int targetCount,
    int closedSolidCount, int openingCount, double minimumThicknessMm,
    double assemblyPercent, int warningCount);

} // namespace kachakacha::v2::app
