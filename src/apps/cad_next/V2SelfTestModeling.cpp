//! 形を作るところのケース(WP-08)。
//!
//! 線の編集・作業平面・押し出し・部材・型紙。
//! 「プラ板から物を作る本筋」がここで通っているかを確かめる。

#include "V2SelfTest.h"

#include "V2MainWindow.h"

#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/UiMode.h"
#include "Win95Style.h"

#include "kachakacha/app/CursorInput.h"
#include "kachakacha/app/ProcessSteps.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/base/Version.h"
#include "kachakacha/view/ViewOrientation.h"
#include "kachakacha/kernel/KernelInfo.h"

#include <filesystem>
#include <system_error>

#include <QApplication>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QImage>
#include <QPainter>
#include <QStringList>

#include <iostream>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

[[nodiscard]] bool DrawCrossingPair(V2MainWindow& window)
{
    if (!window.ApplyManualState(QStringLiteral("draw-line"))) {
        return false;
    }
    auto& viewport = window.Viewport();
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    // 1本目と交わるように、縦向きに引く。
    viewport.ClickAt(QPointF(viewport.width() * 0.5, viewport.height() * 0.3));
    viewport.HoverAt(QPointF(viewport.width() * 0.5, viewport.height() * 0.8));
    viewport.ClickAt(QPointF(viewport.width() * 0.5, viewport.height() * 0.8));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    return viewport.Selection().entityIds.size() == 2;
}

[[nodiscard]] bool CaseWireSplitMakesMorePieces(V2MainWindow& window)
{
    // 交わる2本を分割すると、1本目が交点で切れて本数が増える。
    if (!Explain("交わる2本を引ける", DrawCrossingPair(window))) {
        return false;
    }
    const std::size_t before = window.Session().Scene().curves.size();
    window.RunCommand("wire.split");
    const std::size_t after = window.Session().Scene().curves.size();
    if (!Explain((std::string("線が増えた(") + std::to_string(before) + " → "
                     + std::to_string(after) + ")").c_str(), after > before)) {
        return false;
    }
    return Explain((std::string("帯に結果が出る(") + window.StatusText().toStdString()
                       + ")").c_str(),
        window.StatusText().contains(QStringLiteral("分割")));
}

[[nodiscard]] bool CaseWireEditNeedsSelection(V2MainWindow& window)
{
    // 何も選ばずに押したら、選べと言う。黙って何も起きない、をしない。
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    const std::uint64_t before = window.Session().GetDocument().Revision();
    for (const char* id : {"wire.split", "wire.join", "wire.coincident", "wire.tangent",
             "wire.curvature", "wire.chamfer", "wire.fillet"}) {
        window.RunCommand(id);
        if (!Explain((std::string("理由が出る: ") + id).c_str(),
                !window.StatusText().isEmpty())) {
            return false;
        }
    }
    return Explain("文書は変わらない",
        window.Session().GetDocument().Revision() == before);
}

[[nodiscard]] bool CaseWireConnectRefusesWhenNotAligned(V2MainWindow& window)
{
    // 直角に交わる2本を接線接続しようとすると断る。黙って曲線に化けさせない。
    if (!Explain("交わる2本を引ける", DrawCrossingPair(window))) {
        return false;
    }
    const std::uint64_t before = window.Session().GetDocument().Revision();
    window.RunCommand("wire.tangent");
    if (!Explain((std::string("断る理由が出る(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("GEO-E")))) {
        return false;
    }
    return Explain("文書は変わらない",
        window.Session().GetDocument().Revision() == before);
}

[[nodiscard]] bool CaseWorkPlaneIsCreatedAndActivated(V2MainWindow& window)
{
    // 作業平面は文書に入る。画面の飾りではない。
    // 入れないと「この線はどの面の上か」があとで誰にも分からなくなる。
    const std::size_t before = window.Session().GetDocument().Snapshot().entities.size();
    window.RunCommand("workplane.create");
    const auto& snapshot = window.Session().GetDocument().Snapshot();
    if (!Explain((std::string("ものが増えた(") + std::to_string(before) + " → "
                     + std::to_string(snapshot.entities.size()) + ")").c_str(),
            snapshot.entities.size() == before + 1)) {
        return false;
    }
    int planes = 0;
    for (const auto& entity : snapshot.entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::WorkPlane) {
            ++planes;
        }
    }
    if (!Explain("作業平面が1つある", planes == 1)) {
        return false;
    }
    // 押すたびに別の標準面ができる。1つしか作れないと側面図が描けない。
    window.RunCommand("workplane.create");
    window.RunCommand("workplane.create");
    int all = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::WorkPlane) {
            ++all;
        }
    }
    if (!Explain((std::string("3面できる(実際は ") + std::to_string(all) + ")").c_str(),
            all == 3)) {
        return false;
    }
    // 作業中にするには、どれを作業中にするかを選ぶ。選ばずには決まらない。
    window.RunCommand("workplane.set_active");
    if (!Explain((std::string("選ばなければ理由が出る(")
                     + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("作業平面を1つ")))) {
        return false;
    }
    const auto planeSelection = kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::WorkPlane);
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(planeSelection.entityIds.front());
    window.Viewport().SetSelection(one);
    window.RunCommand("workplane.set_active");
    return Explain((std::string("作業中にできる(") + window.StatusText().toStdString()
                       + ")").c_str(),
        window.StatusText().contains(QStringLiteral("作業中")));
}

[[nodiscard]] bool CaseGridSpacingCycles(V2MainWindow& window)
{
    // グリッドは見え方の都合なので文書に入れない。
    // 入れると、開いた相手の画面のグリッドまで変わってしまう。
    const std::uint64_t before = window.Session().GetDocument().Revision();
    const double first = window.Session().Scene().grid.majorSpacingMm;
    window.RunCommand("grid.edit");
    const double second = window.Session().Scene().grid.majorSpacingMm;
    if (!Explain((std::string("間隔が変わる(") + std::to_string(first) + " → "
                     + std::to_string(second) + ")").c_str(), first != second)) {
        return false;
    }
    if (!Explain("正の数のまま", second > 0.0)) {
        return false;
    }
    return Explain("文書は変わらない",
        window.Session().GetDocument().Revision() == before);
}

//! 閉じた矩形を1つ引いて選ぶ。形をつくる試験の下ごしらえ。
[[nodiscard]] bool DrawClosedRectangle(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Rectangle);
    viewport.ClickAt(QPointF(viewport.width() * 0.35, viewport.height() * 0.35));
    viewport.HoverAt(QPointF(viewport.width() * 0.65, viewport.height() * 0.65));
    viewport.ClickAt(QPointF(viewport.width() * 0.65, viewport.height() * 0.65));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    return !viewport.Selection().entityIds.empty();
}

[[nodiscard]] int CountParts(V2MainWindow& window)
{
    int parts = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::Part) {
            ++parts;
        }
    }
    return parts;
}

[[nodiscard]] bool CaseExtrudeMakesAPart(V2MainWindow& window)
{
    // 閉じた矩形を押し出すと部品が1つできる。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    if (!Explain((std::string("部品が1つできる(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            CountParts(window) == 1)) {
        return false;
    }
    // 厚みと体積を言う。言わないと、狙った板厚になったか確かめられない。
    return Explain("厚みを言う", window.StatusText().contains(QStringLiteral("mm")));
}

[[nodiscard]] bool CasePartCommandsNeedSelection(V2MainWindow& window)
{
    // 何も選ばずに押したら、何を選べばよいかを言う。
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    const std::uint64_t before = window.Session().GetDocument().Revision();
    for (const char* id : {"part.extrude", "part.from_wire_cage", "part.boolean_add",
             "part.boolean_cut"}) {
        window.RunCommand(id);
        if (!Explain((std::string("理由が出る: ") + id).c_str(),
                !window.StatusText().isEmpty())) {
            return false;
        }
    }
    return Explain("文書は変わらない",
        window.Session().GetDocument().Revision() == before);
}

[[nodiscard]] bool CaseExtrudedPartSurvivesSaveAndOpen(V2MainWindow& window)
{
    // 作り方は文書に残る。形そのものは残さないが、作り直せる。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    if (!Explain("部品ができる", CountParts(window) == 1)) {
        return false;
    }
    const std::string path = kachakacha::v2::io::FromPath(
        std::filesystem::temp_directory_path() / "kacha_selftest_part.kcd2");
    std::error_code code;
    std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
    window.SetPathChooser([&path](bool) { return QString::fromStdString(path); });
    window.RunCommand("file.save_as");
    if (!Explain("保存できる",
            window.StatusText().contains(QStringLiteral("保存しました")))) {
        return false;
    }
    window.RunCommand("file.new");
    const bool opened = window.OpenDocumentFile(QString::fromStdString(path));
    std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
    if (!Explain("開き直せる", opened)) {
        return false;
    }
    return Explain((std::string("部品が戻る(実際は ")
                       + std::to_string(CountParts(window)) + ")").c_str(),
        CountParts(window) == 1);
}

[[nodiscard]] bool CaseFabricationAndPatternEndToEnd(V2MainWindow& window)
{
    // 引く → 押し出す → 部材にする → 型紙にする → 1:1 PDF まで通す。
    // これがプラ板から作るときの本筋である。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    if (!Explain("部品ができる", CountParts(window) == 1)) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Part));
    window.RunCommand("fabrication.create");
    if (!Explain((std::string("部材になる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("枚の部材")))) {
        return false;
    }
    window.RunCommand("fabrication.create_pattern");
    if (!Explain((std::string("型紙になる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("原寸")))) {
        return false;
    }
    // 型紙が書き出しの対象に出る。出なければ、出せるのに選べない。
    auto& dock = window.ExportDock();
    if (!Explain((std::string("型紙が1ページある(実際は ")
                     + std::to_string(dock.Counts().patternPages) + ")").c_str(),
            dock.Counts().patternPages >= 1)) {
        return false;
    }
    const auto chosen = kachakacha::v2::app::SetExportPanelTarget(dock.State(),
        kachakacha::v2::app::ExportTarget::CurrentPattern, dock.Counts());
    if (!Explain("型紙を対象にできる", chosen.HasValue())) {
        return false;
    }
    if (!Explain("型紙を対象にする",
            dock.ChooseTarget(kachakacha::v2::app::ExportTarget::CurrentPattern))) {
        return false;
    }
    if (!Explain("1:1 PDF を選べる",
            dock.ChooseFormat(kachakacha::v2::app::ExportFormat::Pdf))) {
        return false;
    }
    const std::string path = kachakacha::v2::io::FromPath(
        std::filesystem::temp_directory_path() / "kacha_selftest_pattern");
    std::error_code code;
    std::filesystem::remove(kachakacha::v2::io::MakePath(path + ".pdf"), code);
    dock.ChoosePath(QString::fromStdString(path));
    if (!Explain((std::string("出せる(理由は ") + dock.ReasonText().toStdString()
                     + ")").c_str(), dock.CanRun())) {
        return false;
    }
    if (!Explain("書き出せる", dock.RunNow())) {
        return false;
    }
    const auto written = kachakacha::v2::io::MakePath(path + ".pdf");
    const bool exists = std::filesystem::exists(written, code);
    const bool hasBytes = exists && std::filesystem::file_size(written, code) > 0;
    std::filesystem::remove(written, code);
    return Explain("PDFが出来ている", hasBytes);
}

//! 選んだ部品を、その形式で出して、中身のあるファイルが残るかを見る。
[[nodiscard]] bool ExportSelectedPartsAs(V2MainWindow& window,
    kachakacha::v2::app::ExportFormat format, const char* suffix)
{
    auto& dock = window.ExportDock();
    if (!Explain("選んだ部品を対象にできる",
            dock.ChooseTarget(kachakacha::v2::app::ExportTarget::SelectedParts))) {
        return false;
    }
    if (!Explain("形式を選べる", dock.ChooseFormat(format))) {
        return false;
    }
    const std::string base = kachakacha::v2::io::FromPath(
        std::filesystem::temp_directory_path() / "kacha_selftest_solid");
    std::error_code code;
    const auto written = kachakacha::v2::io::MakePath(base + suffix);
    std::filesystem::remove(written, code);
    dock.ChoosePath(QString::fromStdString(base));
    if (!Explain((std::string("出せる(理由は ") + dock.ReasonText().toStdString()
                     + ")").c_str(), dock.CanRun())) {
        return false;
    }
    if (!Explain((std::string("書き出せる(") + window.StatusText().toStdString()
                     + ")").c_str(), dock.RunNow())) {
        return false;
    }
    const bool hasBytes = std::filesystem::exists(written, code)
        && std::filesystem::file_size(written, code) > 0;
    std::filesystem::remove(written, code);
    return Explain((std::string("中身のあるファイルが残る: ") + suffix).c_str(), hasBytes);
}

[[nodiscard]] bool CaseSolidExportWritesStlAndStep(V2MainWindow& window)
{
    // 押し出した部品を STL と STEP で出す。
    // 同じ形から両方を出す。別々に近似して食い違わせない。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    if (!Explain("部品ができる", CountParts(window) == 1)) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Part));
    window.RefreshExportCounts();
    if (!ExportSelectedPartsAs(window, kachakacha::v2::app::ExportFormat::Stl, ".stl")) {
        return false;
    }
    return ExportSelectedPartsAs(window, kachakacha::v2::app::ExportFormat::Step, ".step");
}

[[nodiscard]] bool CaseSolidExportNeedsASolid(V2MainWindow& window)
{
    // 立体を作っていないのに STL を頼まれたら断る。
    // 空のファイルを残さない。
    auto& dock = window.ExportDock();
    window.RefreshExportCounts();
    if (!Explain("立体がないので選べない",
            !dock.ChooseTarget(kachakacha::v2::app::ExportTarget::SelectedParts))) {
        return false;
    }
    return Explain((std::string("理由が出る(") + dock.ReasonText().toStdString()
                       + ")").c_str(), !dock.ReasonText().isEmpty());
}

[[nodiscard]] bool CasePatternNeedsFabricationFirst(V2MainWindow& window)
{
    // 順を飛ばしたら、何を先にすればよいかを言う。
    window.RunCommand("fabrication.create_pattern");
    if (!Explain((std::string("順を言う(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("製作モデル")))) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("fabrication.create");
    return Explain((std::string("何を選ぶか言う(") + window.StatusText().toStdString()
                       + ")").c_str(),
        window.StatusText().contains(QStringLiteral("部品を1つ")));
}

[[nodiscard]] bool CaseSampleDocumentOpens(V2MainWindow& window)
{
    // 配る見本が開けて、線が画面へ並ぶこと(WP-12)。
    if (!Explain("見本を開ける", window.ApplyManualState(QStringLiteral("sample")))) {
        return false;
    }
    const auto& snapshot = window.Session().GetDocument().Snapshot();
    if (!Explain((std::string("ものが9つ(実際は ")
                     + std::to_string(snapshot.entities.size()) + ")").c_str(),
            snapshot.entities.size() == 9)) {
        return false;
    }
    if (!Explain((std::string("線が30本(実際は ")
                     + std::to_string(window.Session().Scene().curves.size())
                     + ")").c_str(),
            window.Session().Scene().curves.size() == 30)) {
        return false;
    }
    // 開いた直後は「元に戻す」で前の文書へ帰れない。
    if (!Explain("開いた直後は戻せない",
            !window.Session().GetDocument().CanUndo())) {
        return false;
    }
    return Explain("一覧にも出ている", window.EntityRowCount() > 0);
}

} // namespace

std::vector<SelfTestCase> ModelingCases()
{
    return {
        {"作業平面を作って作業中にできる", &CaseWorkPlaneIsCreatedAndActivated},
        {"グリッドの間隔を変えられる", &CaseGridSpacingCycles},
        {"分割で線が増える", &CaseWireSplitMakesMorePieces},
        {"線を選ばずに編集を押すと理由が出る", &CaseWireEditNeedsSelection},
        {"そろっていない接線接続は断る", &CaseWireConnectRefusesWhenNotAligned},
        {"押し出しで部品ができる", &CaseExtrudeMakesAPart},
        {"部品のコマンドは選択が要る", &CasePartCommandsNeedSelection},
        {"押し出した部品が保存して開き直しても残る", &CaseExtrudedPartSurvivesSaveAndOpen},
        {"引く→押し出す→部材→型紙→PDFまで通る", &CaseFabricationAndPatternEndToEnd},
        {"順を飛ばすと何を先にするか言う", &CasePatternNeedsFabricationFirst},
        {"部品をSTLとSTEPで出せる", &CaseSolidExportWritesStlAndStep},
        {"立体がなければ立体では出せない", &CaseSolidExportNeedsASolid},
        {"配る見本が開ける", &CaseSampleDocumentOpens},
    };
}

} // namespace kachakacha::v2::selftest
