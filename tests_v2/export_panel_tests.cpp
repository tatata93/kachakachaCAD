// 書き出し画面の中身(ui-workflows §11、WP-11)。
#include "kachakacha/app/ExportPanel.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/io/AtomicFile.h"

#include <filesystem>
#include <fstream>
#include <string>

using kachakacha::v2::app::BeginExportPanel;
using kachakacha::v2::app::BuildExportFormatRows;
using kachakacha::v2::app::BuildExportTargetRows;
using kachakacha::v2::app::CanRunExport;
using kachakacha::v2::app::ExportBlockReasonJa;
using kachakacha::v2::app::ExportCountFor;
using kachakacha::v2::app::ExportCounts;
using kachakacha::v2::app::ExportFormat;
using kachakacha::v2::app::ExportFormatAllowed;
using kachakacha::v2::app::ExportOutcome;
using kachakacha::v2::app::ExportOutcomeTextJa;
using kachakacha::v2::app::ExportPanelState;
using kachakacha::v2::app::ExportSelectionTextJa;
using kachakacha::v2::app::ExportTarget;
using kachakacha::v2::app::SetExportPanelFormat;
using kachakacha::v2::app::SetExportPanelOverwrite;
using kachakacha::v2::app::SetExportPanelPath;
using kachakacha::v2::app::SetExportPanelTarget;
using kachakacha::v2::app::ToExportRequest;
using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

//! 何でも1つずつある状態。
[[nodiscard]] ExportCounts FullCounts()
{
    ExportCounts counts;
    counts.visibleParts = 3;
    counts.selectedParts = 2;
    counts.selectedFabricationPanels = 4;
    counts.patternPages = 1;
    counts.selectedWires = 5;
    counts.project = 1;
    return counts;
}

//! 部品だけがある状態。
[[nodiscard]] ExportCounts PartsOnly()
{
    ExportCounts counts;
    counts.visibleParts = 2;
    counts.selectedParts = 1;
    return counts;
}

const ExportTarget kTargets[] = {ExportTarget::VisibleParts, ExportTarget::SelectedParts,
    ExportTarget::SelectedFabricationPanels, ExportTarget::CurrentPattern,
    ExportTarget::SelectedWires, ExportTarget::Project};

//! 試験用の作業場所。
struct Scratch {
    std::string path;

    explicit Scratch(const std::string& name)
    {
        path = kachakacha::v2::io::FromPath(
            std::filesystem::temp_directory_path() / ("kacha_panel_" + name));
    }
    ~Scratch()
    {
        std::error_code code;
        std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
        std::filesystem::remove(kachakacha::v2::io::MakePath(path + ".step"), code);
        std::filesystem::remove(kachakacha::v2::io::MakePath(path + ".svg"), code);
    }
};

void MakeFile(const std::string& path)
{
    std::ofstream stream(kachakacha::v2::io::MakePath(path), std::ios::binary);
    stream << "x";
}

} // namespace

KACHA_V2_TEST(export_panel, 数が0の対象も並びから消えない)
{
    // 消すと「選んでいない」のか「そもそも無い」のかが分からなくなる。
    const auto rows = BuildExportTargetRows(PartsOnly());
    Require(rows.size() == std::size(kTargets), "対象の数");
    for (std::size_t index = 0; index < rows.size(); ++index) {
        Require(rows[index].target == kTargets[index], "対象の順が決まっている");
        Require(!rows[index].labelJa.empty(), "名前がある");
    }
}

KACHA_V2_TEST(export_panel, 数が0の対象は選べない)
{
    const auto rows = BuildExportTargetRows(PartsOnly());
    for (const auto& row : rows) {
        const bool expected = ExportCountFor(PartsOnly(), row.target) > 0;
        Require(row.selectable == expected, "選べるかは数で決まる");
    }
}

KACHA_V2_TEST(export_panel, 対象の名前に件数が入っている)
{
    const auto rows = BuildExportTargetRows(FullCounts());
    for (const auto& row : rows) {
        Require(row.labelJa.find(std::to_string(row.count)) != std::string::npos,
            "件数が名前に出る");
    }
}

KACHA_V2_TEST(export_panel, 形式の並びは対象で選べるものに印が付く)
{
    for (ExportTarget target : kTargets) {
        const auto rows = BuildExportFormatRows(target);
        Require(rows.size() == std::size_t{6}, "形式の数");
        for (const auto& row : rows) {
            Require(row.selectable == ExportFormatAllowed(target, row.format),
                "台帳と同じ");
        }
    }
}

KACHA_V2_TEST(export_panel, 開いたときは数のある対象が選ばれる)
{
    ExportCounts counts;
    counts.selectedWires = 3;
    const ExportPanelState state = BeginExportPanel(counts);
    Require(state.target == ExportTarget::SelectedWires, "数のある対象");
    Require(ExportFormatAllowed(state.target, state.format), "形式も成り立つ");
}

KACHA_V2_TEST(export_panel, 何も無いときでも状態は作れる)
{
    // 開くこと自体は断らない。断るのは「出す」を押したときである。
    const ExportPanelState state = BeginExportPanel(ExportCounts{});
    Require(state.target == ExportTarget::VisibleParts, "先頭の対象のまま");
    Require(!CanRunExport(state, ExportCounts{}), "押せない");
}

KACHA_V2_TEST(export_panel, 対象を変えても使える形式はそのまま残る)
{
    const ExportCounts counts = FullCounts();
    ExportPanelState state = BeginExportPanel(counts);
    const auto toPanels = SetExportPanelTarget(state, ExportTarget::SelectedParts, counts);
    Require(toPanels.HasValue(), "選べる");
    const auto step = SetExportPanelFormat(toPanels.Value(), ExportFormat::Step);
    Require(step.HasValue(), "STEP は選べる");
    const auto moved = SetExportPanelTarget(step.Value(),
        ExportTarget::SelectedFabricationPanels, counts);
    Require(moved.HasValue(), "製作部材へ移れる");
    Require(moved.Value().format == ExportFormat::Step, "STEP のまま");
}

KACHA_V2_TEST(export_panel, 対象を変えて形式が使えなくなったら先頭へ移る)
{
    const ExportCounts counts = FullCounts();
    ExportPanelState state;
    state.target = ExportTarget::SelectedParts;
    state.format = ExportFormat::Step;
    const auto moved = SetExportPanelTarget(state, ExportTarget::CurrentPattern, counts);
    Require(moved.HasValue(), "型紙へ移れる");
    Require(moved.Value().format != ExportFormat::Step, "STEP のままにしない");
    Require(ExportFormatAllowed(ExportTarget::CurrentPattern, moved.Value().format),
        "使える形式になっている");
    Require(moved.Value().format == ExportFormat::Svg, "先頭の形式");
}

KACHA_V2_TEST(export_panel, 数が0の対象へは移れない)
{
    const auto moved = SetExportPanelTarget(BeginExportPanel(PartsOnly()),
        ExportTarget::CurrentPattern, PartsOnly());
    Require(!moved.HasValue(), "断る");
    RequireEqual(std::string("EXP-015"), FirstCode(moved.Diagnostics()), "コード");
}

KACHA_V2_TEST(export_panel, 成り立たない形式は断って対象を変えない)
{
    ExportPanelState state;
    state.target = ExportTarget::CurrentPattern;
    state.format = ExportFormat::Svg;
    const auto changed = SetExportPanelFormat(state, ExportFormat::Step);
    Require(!changed.HasValue(), "断る");
    RequireEqual(std::string("EXP-017"), FirstCode(changed.Diagnostics()), "コード");
    // 断ったのだから、元の状態はそのままである。
    Require(state.target == ExportTarget::CurrentPattern, "対象は動かない");
    Require(state.format == ExportFormat::Svg, "形式も動かない");
}

KACHA_V2_TEST(export_panel, 出力先を変えたら上書きの承諾は消える)
{
    ExportPanelState state = SetExportPanelOverwrite(BeginExportPanel(FullCounts()), true);
    Require(state.overwrite, "承諾している");
    const ExportPanelState moved = SetExportPanelPath(state, "/tmp/other");
    Require(!moved.overwrite, "別のファイルへの承諾ではない");
}

KACHA_V2_TEST(export_panel, 出力先が空なら押せない)
{
    const ExportPanelState state = BeginExportPanel(FullCounts());
    Require(state.path.empty(), "初めは空");
    Require(!CanRunExport(state, FullCounts()), "押せない");
    Require(ExportBlockReasonJa(state, FullCounts()).find("EXP-016") != std::string::npos,
        "理由が出る");
}

KACHA_V2_TEST(export_panel, そろえば押せる)
{
    Scratch scratch("ok");
    const ExportPanelState state = SetExportPanelPath(BeginExportPanel(FullCounts()),
        scratch.path);
    Require(CanRunExport(state, FullCounts()), "押せる");
    Require(ExportBlockReasonJa(state, FullCounts()).empty(), "理由は無い");
}

KACHA_V2_TEST(export_panel, 注文には件数と拡張子付きの道が入る)
{
    Scratch scratch("request");
    ExportPanelState state = BeginExportPanel(FullCounts());
    const auto target = SetExportPanelTarget(state, ExportTarget::SelectedParts,
        FullCounts());
    Require(target.HasValue(), "対象を選べる");
    const auto format = SetExportPanelFormat(target.Value(), ExportFormat::Step);
    Require(format.HasValue(), "形式を選べる");
    const auto request = ToExportRequest(SetExportPanelPath(format.Value(), scratch.path),
        FullCounts());
    Require(request.HasValue(), "注文が作れる");
    Require(request.Value().targetCount == 2, "選んだ部品の数");
    Require(request.Value().path == scratch.path + ".step", "拡張子が付く");
}

KACHA_V2_TEST(export_panel, 既にあるファイルは承諾なしでは押せない)
{
    Scratch scratch("exists");
    MakeFile(scratch.path + ".step");
    ExportPanelState state;
    state.target = ExportTarget::SelectedParts;
    state.format = ExportFormat::Step;
    state = SetExportPanelPath(state, scratch.path);
    Require(!CanRunExport(state, FullCounts()), "押せない");
    RequireEqual(std::string("EXP-018"),
        FirstCode(ToExportRequest(state, FullCounts()).Diagnostics()), "コード");
    const ExportPanelState allowed = SetExportPanelOverwrite(state, true);
    Require(CanRunExport(allowed, FullCounts()), "承諾すれば押せる");
}

KACHA_V2_TEST(export_panel, 選びの一文に対象と件数と形式が入る)
{
    const ExportPanelState state = BeginExportPanel(FullCounts());
    const std::string text = ExportSelectionTextJa(state, FullCounts());
    Require(text.find("表示している部品") != std::string::npos, "対象");
    Require(text.find("3") != std::string::npos, "件数");
    Require(text.find("STL") != std::string::npos, "形式");
}

KACHA_V2_TEST(export_panel, 出したあとの一文に道と大きさが入る)
{
    ExportOutcome outcome;
    outcome.path = "/tmp/a.step";
    outcome.byteCount = 128;
    const std::string text = ExportOutcomeTextJa(outcome);
    Require(text.find("/tmp/a.step") != std::string::npos, "道");
    Require(text.find("128") != std::string::npos, "大きさ");
    Require(text.find("控え") == std::string::npos, "控えは無い");
    outcome.keptBackup = true;
    Require(ExportOutcomeTextJa(outcome).find("控え") != std::string::npos, "控えを知らせる");
}

KACHA_V2_TEST(export_panel, どの対象からどの対象へも数があれば移れる)
{
    // 対象を変える道が、途中で行き止まりにならないことを見る。
    const ExportCounts counts = FullCounts();
    for (ExportTarget from : kTargets) {
        const auto start = SetExportPanelTarget(BeginExportPanel(counts), from, counts);
        Require(start.HasValue(), "出発できる");
        for (ExportTarget to : kTargets) {
            const auto moved = SetExportPanelTarget(start.Value(), to, counts);
            Require(moved.HasValue(), "移れる");
            Require(ExportFormatAllowed(moved.Value().target, moved.Value().format),
                "移った先でも形式が成り立つ");
        }
    }
}

KACHA_V2_TEST(export_panel, 手順の状況から数を作る)
{
    kachakacha::v2::app::ProcessContext context;
    context.selectedPartCount = 2;
    context.selectedWireCount = 7;
    context.panelCount = 5;
    const ExportCounts counts =
        kachakacha::v2::app::ExportCountsFrom(context, 3, true);
    Require(counts.visibleParts == 3, "表示している部品");
    Require(counts.selectedParts == 2, "選んだ部品");
    Require(counts.selectedWires == 7, "選んだワイヤー");
    Require(counts.project == 1, "文書");
    // 製作モデルが出来ていなければ、部材の数があっても0にする。
    Require(counts.selectedFabricationPanels == 0, "製作モデルが無い");
    Require(counts.patternPages == 0, "型紙が無い");
}

KACHA_V2_TEST(export_panel, 製作モデルが出来ていれば部材と型紙が出せる)
{
    kachakacha::v2::app::ProcessContext context;
    context.fabricationBuilt = true;
    context.panelCount = 5;
    context.patternBuilt = true;
    const ExportCounts counts =
        kachakacha::v2::app::ExportCountsFrom(context, 0, true);
    Require(counts.selectedFabricationPanels == 5, "部材");
    Require(counts.patternPages == 1, "型紙");
}

KACHA_V2_TEST(export_panel, 文書が無ければ何も出せない)
{
    const ExportCounts counts =
        kachakacha::v2::app::ExportCountsFrom(kachakacha::v2::app::ProcessContext{},
            0, false);
    for (ExportTarget target : kTargets) {
        Require(ExportCountFor(counts, target) == 0, "全部0");
    }
    Require(!CanRunExport(BeginExportPanel(counts), counts), "押せない");
}

KACHA_V2_TEST(export_panel, 文書はいつも1件あるので対象を占領しない)
{
    // 「この文書」は開いていれば必ず1件ある。数だけで残すと、
    // ワイヤーを選んでも対象が文書のままになる。
    ExportCounts counts;
    counts.project = 1;
    ExportPanelState state = kachakacha::v2::app::RetargetForCounts(
        BeginExportPanel(counts), counts);
    Require(state.target == ExportTarget::Project, "ほかに無ければ文書");
    counts.selectedWires = 2;
    state = kachakacha::v2::app::RetargetForCounts(state, counts);
    Require(state.target == ExportTarget::SelectedWires, "選んだワイヤーへ移る");
    Require(state.format == ExportFormat::Svg, "形式もその対象のものになる");
}

KACHA_V2_TEST(export_panel, 自分で選んだ対象は数が変わっても動かない)
{
    ExportCounts counts = FullCounts();
    const auto chosen = kachakacha::v2::app::SetExportPanelTarget(
        BeginExportPanel(counts), ExportTarget::CurrentPattern, counts);
    Require(chosen.HasValue(), "選べる");
    Require(chosen.Value().targetChosenByUser, "自分で選んだ印が付く");
    counts.selectedWires = 99;
    const ExportPanelState kept =
        kachakacha::v2::app::RetargetForCounts(chosen.Value(), counts);
    Require(kept.target == ExportTarget::CurrentPattern, "動かない");
}

KACHA_V2_TEST(export_panel, 自分で選んだ対象でも出せなくなれば移る)
{
    ExportCounts counts = FullCounts();
    const auto chosen = kachakacha::v2::app::SetExportPanelTarget(
        BeginExportPanel(counts), ExportTarget::CurrentPattern, counts);
    Require(chosen.HasValue(), "選べる");
    counts.patternPages = 0;
    const ExportPanelState moved =
        kachakacha::v2::app::RetargetForCounts(chosen.Value(), counts);
    Require(moved.target != ExportTarget::CurrentPattern, "移る");
    Require(ExportCountFor(counts, moved.target) > 0, "出せるものへ移る");
    Require(!moved.targetChosenByUser, "選び直しの印は消える");
}

KACHA_V2_TEST(export_panel, 選び直しても出力先は消えない)
{
    ExportCounts counts;
    counts.project = 1;
    ExportPanelState state = SetExportPanelPath(BeginExportPanel(counts), "/tmp/a");
    counts.selectedWires = 1;
    const ExportPanelState moved = kachakacha::v2::app::RetargetForCounts(state, counts);
    Require(moved.path == "/tmp/a", "出力先はそのまま");
}

KACHA_V2_TEST_MAIN("export_panel_tests")
