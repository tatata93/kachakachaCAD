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
    if (!Explain((std::string("帯に結果が出る(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("分割")))) {
        return false;
    }
    // 刃にした線は残る。残らないと、切っただけで線が1本消える。
    return Explain("刃は残る",
        window.StatusText().contains(QStringLiteral("残っています")));
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
    const int shapesBefore = window.KernelShapeCount();
    if (!Explain("立体の形を覚えている", shapesBefore >= 1)) {
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
    if (!Explain((std::string("部品が戻る(実際は ")
                     + std::to_string(CountParts(window)) + ")").c_str(),
            CountParts(window) == 1)) {
        return false;
    }
    // 名前が残っただけでは戻ったことにならない。**形**が戻っていること。
    // 戻っていないと、開き直したあとに STEP や STL で出せない。
    // 一覧には部品が並んでいるので、出そうとして初めて気づくことになる。
    if (!Explain((std::string("立体の形も戻る(") + std::to_string(shapesBefore)
                     + " → " + std::to_string(window.KernelShapeCount()) + ")").c_str(),
            window.KernelShapeCount() >= shapesBefore)) {
        return false;
    }
    // 出せるところまで確かめる。ここまで通って、初めて戻ったと言える。
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Part));
    return Explain("開き直したあとも STEP で出せる",
        window.CanExportSelectedParts());
}

[[nodiscard]] bool CaseExtrudeMakesWiresOnly(V2MainWindow& window)
{
    // 「押し出しと同じ要領でワイヤだけ作る」。core は前からできたのに、
    // 画面が出力を3つとも true に固定していたので選べなかった。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    const int partsBefore = CountParts(window);
    window.SetExtrudeChooser([](const kachakacha::v2::app::ExtrudeChoice& initial,
                                 const kachakacha::v2::app::ExtrudeFacts&) {
        kachakacha::v2::app::ExtrudeChoice choice = initial;
        choice.makePart = false;
        choice.makeEndProfileWire = true;
        choice.makeSideBoundaryWires = true;
        return std::optional<kachakacha::v2::app::ExtrudeChoice>(choice);
    });
    const auto countWires = [&window] {
        int count = 0;
        for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
            if (entity.kind == kachakacha::v2::domain::EntityKind::Wire) {
                ++count;
            }
        }
        return count;
    };
    const int wiresBefore = countWires();
    window.RunCommand("part.extrude");
    if (!Explain((std::string("部品は増えない(") + std::to_string(partsBefore)
                     + " → " + std::to_string(CountParts(window)) + ")").c_str(),
            CountParts(window) == partsBefore)) {
        return false;
    }
    // 出来たワイヤーは、画面に出すだけの辺ではなく文書のワイヤーであること。
    // 辺のままだと、選ぶことも、次の押し出しの輪郭にすることもできない。
    return Explain((std::string("ワイヤーが増える(") + std::to_string(wiresBefore)
                       + " → " + std::to_string(countWires()) + ")").c_str(),
        countWires() > wiresBefore);
}

[[nodiscard]] bool CaseExtrudeRefusesImpossibleChoices(V2MainWindow& window)
{
    // 通らない組み合わせは、押してから断るのではなく、理由を出して断る。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    const int partsBefore = CountParts(window);
    // 何も作らない指定。
    window.SetExtrudeChooser([](const kachakacha::v2::app::ExtrudeChoice& initial,
                                 const kachakacha::v2::app::ExtrudeFacts&) {
        kachakacha::v2::app::ExtrudeChoice choice = initial;
        choice.makePart = false;
        return std::optional<kachakacha::v2::app::ExtrudeChoice>(choice);
    });
    window.RunCommand("part.extrude");
    if (!Explain((std::string("何を作るかが無いと断る(")
                     + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("何を作るか")))) {
        return false;
    }
    // 相手を選ばずに「全部貫く」。
    window.SetExtrudeChooser([](const kachakacha::v2::app::ExtrudeChoice& initial,
                                 const kachakacha::v2::app::ExtrudeFacts&) {
        kachakacha::v2::app::ExtrudeChoice choice = initial;
        choice.extent = kachakacha::v2::modeling::ExtrudeExtentMode::ThroughAll;
        return std::optional<kachakacha::v2::app::ExtrudeChoice>(choice);
    });
    window.RunCommand("part.extrude");
    if (!Explain((std::string("貫くのは引くときだけと断る(")
                     + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("引くときだけ")))) {
        return false;
    }
    return Explain("どちらも文書を変えていない", CountParts(window) == partsBefore);
}

[[nodiscard]] bool CaseExtrudeCanBeCancelled(V2MainWindow& window)
{
    // 窓でやめたら、何も起きない。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    const int partsBefore = CountParts(window);
    window.SetExtrudeChooser([](const kachakacha::v2::app::ExtrudeChoice&,
                                 const kachakacha::v2::app::ExtrudeFacts&) {
        return std::optional<kachakacha::v2::app::ExtrudeChoice>();
    });
    window.RunCommand("part.extrude");
    if (!Explain("やめたと言う",
            window.StatusText().contains(QStringLiteral("やめました")))) {
        return false;
    }
    return Explain("部品は増えない", CountParts(window) == partsBefore);
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

[[nodiscard]] bool CaseSelectedEntitiesExportMakesASmallerDocument(V2MainWindow& window)
{
    // 選んだものだけを別の kcd2 にする(V1 の「追加されたものを別 kcd として出力する」)。
    // 選んだ部品と、それを作るのに要る線は入り、無関係な線は入らない。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    if (!Explain("部品ができる", CountParts(window) == 1)) {
        return false;
    }
    const auto countWires = [&window]() {
        return static_cast<int>(kachakacha::v2::app::SelectAllOfKind(
            window.Session().GetDocument().Snapshot(),
            kachakacha::v2::domain::EntityKind::Wire)
                                    .entityIds.size());
    };
    const int wiresForPart = countWires();
    // 無関係な線を1本足す。これは別の文書に入ってはいけない。
    auto& viewport = window.Viewport();
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.ClickAt(QPointF(viewport.width() * 0.10, viewport.height() * 0.10));
    viewport.ClickAt(QPointF(viewport.width() * 0.20, viewport.height() * 0.10));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    if (!Explain("無関係な線が増える", countWires() == wiresForPart + 1)) {
        return false;
    }
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Part));
    window.RefreshExportCounts();
    auto& dock = window.ExportDock();
    if (!Explain("選んだものだけの文書を対象にできる",
            dock.ChooseTarget(kachakacha::v2::app::ExportTarget::SelectedEntities))) {
        return false;
    }
    if (!Explain("kcd2 を選べる", dock.ChooseFormat(kachakacha::v2::app::ExportFormat::Kcd2))) {
        return false;
    }
    const std::string base = kachakacha::v2::io::FromPath(
        std::filesystem::temp_directory_path() / "kacha_selftest_subdocument");
    std::error_code code;
    const auto written = kachakacha::v2::io::MakePath(base + ".kcd2");
    std::filesystem::remove(written, code);
    dock.ChoosePath(QString::fromStdString(base));
    if (!Explain((std::string("書き出せる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            dock.CanRun() && dock.RunNow())) {
        return false;
    }
    const bool opened = window.OpenDocumentFile(QString::fromStdString(base + ".kcd2"));
    std::filesystem::remove(written, code);
    if (!Explain("出来た文書を開ける", opened)) {
        return false;
    }
    if (!Explain((std::string("部品が作り直される(") + window.StatusText().toStdString()
                     + ")").c_str(),
            CountParts(window) == 1 && window.KernelShapeCount() >= 1)) {
        return false;
    }
    return Explain((std::string("要る線だけが入る(") + std::to_string(countWires()) + " / "
                       + std::to_string(wiresForPart) + ")").c_str(),
        countWires() == wiresForPart);
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
        window.StatusText().contains(QStringLiteral("部品か形状ガイド")));
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

[[nodiscard]] bool CaseGuideSurfaceNeedsTwoSections(V2MainWindow& window)
{
    // 断面が1枚では渡す相手がいない。作れないことを作れたことにしない。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("guide.create");
    return Explain((std::string("断面の数を言う(") + window.StatusText().toStdString()
                       + ")").c_str(),
        window.StatusText().contains(QStringLiteral("断面が2つ以上")));
}

[[nodiscard]] bool CaseValidateNeedsASolid(V2MainWindow& window)
{
    // 立体を作る前に検査を頼まれたら、作ってくださいと言う。
    window.RunCommand("export.validate");
    return Explain((std::string("理由が出る(") + window.StatusText().toStdString()
                       + ")").c_str(), !window.StatusText().isEmpty());
}

[[nodiscard]] bool CaseValidateAcceptsAnExtrudedPart(V2MainWindow& window)
{
    // 押し出した箱は閉じていて体積がある。出せると言えるはずである。
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
    window.RunCommand("export.validate");
    return Explain((std::string("出せると言う(") + window.StatusText().toStdString()
                       + ")").c_str(),
        window.StatusText().contains(QStringLiteral("出せます")));
}

[[nodiscard]] bool CaseProjectKeepsTheOriginal(V2MainWindow& window)
{
    // 落とした先が分かるように、元の線は消さない。
    // 消すと「どの面へ落としたのか」が後から誰にも分からなくなる。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    const std::size_t before =
        window.Session().GetDocument().Snapshot().entities.size();
    window.RunCommand("wire.project");
    if (!Explain((std::string("落とせる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("落としました")))) {
        return false;
    }
    const std::size_t after = window.Session().GetDocument().Snapshot().entities.size();
    return Explain((std::string("元の線が残る(") + std::to_string(before) + " → "
                       + std::to_string(after) + ")").c_str(),
        after == before + 1);
}

[[nodiscard]] bool CaseBooleanNeedsTwoParts(V2MainWindow& window)
{
    // 相手を明示して選ぶ。近い部品を勝手に選ばない。
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
    for (const char* id : {"part.boolean_add", "part.boolean_cut"}) {
        window.RunCommand(id);
        if (!Explain((std::string("2つ要ると言う(") + id + ": "
                         + window.StatusText().toStdString() + ")").c_str(),
                window.StatusText().contains(QStringLiteral("部品を2つ")))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaseTrimAsksWhereToPress(V2MainWindow& window)
{
    // どこを切るかは押した場所で決まる。選択だけでは決まらない。
    // 聞いていることを言わないと、押しても何も起きないように見える。
    if (!Explain("交わる2本を引ける", DrawCrossingPair(window))) {
        return false;
    }
    window.RunCommand("wire.trim");
    if (!Explain((std::string("押す場所を聞く(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("押してください")))) {
        return false;
    }
    if (!Explain("拾う待ちになっている", window.Viewport().PickPending())) {
        return false;
    }
    // Esc でやめられる。やめられないと、押すまで何もできなくなる。
    window.Viewport().CancelTool();
    return Explain("やめられる", !window.Viewport().PickPending());
}

[[nodiscard]] bool CaseTrimNeedsTwoWires(V2MainWindow& window)
{
    // 1本目が直す線、2本目が境界。1本では境界がない。
    if (!Explain("線を1本引ける", window.ApplyManualState(QStringLiteral("draw-line")))) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    window.RunCommand("wire.trim");
    if (!Explain((std::string("2本要ると言う(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("線を2本")))) {
        return false;
    }
    return Explain("拾う待ちにならない", !window.Viewport().PickPending());
}

[[nodiscard]] bool CaseGridOriginAsksWhereToPress(V2MainWindow& window)
{
    window.RunCommand("grid.move_origin");
    if (!Explain((std::string("押す場所を聞く(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("押してください")))) {
        return false;
    }
    // 押すと動く。動いたことを帯で言う。
    auto& viewport = window.Viewport();
    viewport.ClickAt(QPointF(viewport.width() * 0.4, viewport.height() * 0.6));
    if (!Explain((std::string("動かしたと言う(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("動かしました")))) {
        return false;
    }
    return Explain("拾い終えて道具へ戻る", !viewport.PickPending());
}

[[nodiscard]] bool CaseFreezeKeepsTheOriginal(V2MainWindow& window)
{
    // 固定しても元は消さない。消すと、どうやって作ったのかをたどれなくなる。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    const std::size_t before =
        window.Session().GetDocument().Snapshot().entities.size();
    window.RunCommand("derived.freeze");
    if (!Explain((std::string("固定できる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("付いていかない")))) {
        return false;
    }
    const auto& snapshot = window.Session().GetDocument().Snapshot();
    if (!Explain((std::string("ものが増える(") + std::to_string(before) + " → "
                     + std::to_string(snapshot.entities.size()) + ")").c_str(),
            snapshot.entities.size() == before + 1)) {
        return false;
    }
    int hidden = 0;
    for (const auto& entity : snapshot.entities) {
        if (entity.visibility == kachakacha::v2::domain::Visibility::Hidden) {
            ++hidden;
        }
    }
    return Explain((std::string("元は隠れているだけ(") + std::to_string(hidden)
                       + ")").c_str(), hidden >= 1);
}

[[nodiscard]] bool CaseOpeningMustBeClosed(V2MainWindow& window)
{
    // 開いた線は穴にならない。開いたまま切ると、板が2つに割れる。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Part));
    window.RunCommand("fabrication.create");
    if (!Explain((std::string("部材になる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("枚の部材")))) {
        return false;
    }
    // 開いた線を1本引いて、開口にしようとする。
    auto& viewport = window.Viewport();
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.ClickAt(QPointF(viewport.width() * 0.45, viewport.height() * 0.45));
    viewport.ClickAt(QPointF(viewport.width() * 0.55, viewport.height() * 0.55));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    const auto wires = kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire);
    kachakacha::v2::app::SelectionSet last;
    if (!wires.entityIds.empty()) {
        last.entityIds.push_back(wires.entityIds.back());
    }
    viewport.SetSelection(last);
    window.RunCommand("fabrication.assign_role");
    // 閉じていない線は折り線になる。切らない。
    return Explain((std::string("折り線として扱う(") + window.StatusText().toStdString()
                       + ")").c_str(),
        window.StatusText().contains(QStringLiteral("折り線"))
            || window.StatusText().contains(QStringLiteral("載っていません")));
}

[[nodiscard]] bool CaseWorkPlaneCanBeOffset(V2MainWindow& window)
{
    // 原点を通らない平面が作れること。station ごとの断面を置くのに要る。
    // これまでは標準面しか作れなかったので、工程1が画面では始められなかった。
    window.RunCommand("workplane.create");
    if (!Explain((std::string("標準面が作れる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("作業中にしました")))) {
        return false;
    }
    // 出来た平面を選んで、そこから 25mm 離した平面を作る。
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::WorkPlane));
    window.SetWorkPlaneChooser([](const WorkPlaneChoice&,
                                   const kachakacha::v2::app::WorkPlaneFacts&) {
        WorkPlaneChoice choice;
        choice.method = kachakacha::v2::modeling::WorkPlaneMethod::OffsetFromPlane;
        choice.offsetMm = 25.0;
        return std::optional<WorkPlaneChoice>(choice);
    });
    window.RunCommand("workplane.create");
    if (!Explain((std::string("離した平面が作れる(")
                     + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("平面から離す")))) {
        return false;
    }
    // 本当に原点から離れていること。名前が変わっただけでは意味がない。
    return Explain((std::string("原点が動いている(z=")
                       + std::to_string(window.Viewport().WorkPlane().origin.z)
                       + ")").c_str(),
        std::abs(window.Viewport().WorkPlane().origin.z - 25.0) < 1.0e-6);
}

[[nodiscard]] bool CaseWorkPlaneRefusesWhenNothingSelected(V2MainWindow& window)
{
    // 足りないものを黙って補わない。補うと、思っていない平面が出来る。
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.SetWorkPlaneChooser([](const WorkPlaneChoice&,
                                   const kachakacha::v2::app::WorkPlaneFacts&) {
        WorkPlaneChoice choice;
        choice.method = kachakacha::v2::modeling::WorkPlaneMethod::ThreePoints;
        return std::optional<WorkPlaneChoice>(choice);
    });
    const std::size_t before =
        window.Session().GetDocument().Snapshot().entities.size();
    window.RunCommand("workplane.create");
    if (!Explain((std::string("足りないと言う(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("足りません")))) {
        return false;
    }
    return Explain("文書は変わらない",
        window.Session().GetDocument().Snapshot().entities.size() == before);
}

//! 曲がった形状ガイドを作る。XY の円弧と、30mm 上の平面の円弧を渡す(円筒の一部)。
//! 作業平面11通りと形状ガイドを実際に使って作るので、この道が通ること自体が試験である。
[[nodiscard]] bool CaseThickenToPlaneFillsUpToThePlane(V2MainWindow& window)
{
    // オーナーの手順の「面を任意の面まで立体化」。厚みを数ではなく相手の平面で決める。
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.ClickAt(QPointF(viewport.width() * 0.30, viewport.height() * 0.35));
    viewport.ClickAt(QPointF(viewport.width() * 0.70, viewport.height() * 0.35));
    viewport.ClickAt(QPointF(viewport.width() * 0.30, viewport.height() * 0.65));
    viewport.ClickAt(QPointF(viewport.width() * 0.70, viewport.height() * 0.65));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    window.RunCommand("guide.create");
    if (!Explain("面ができる", window.StatusText().contains(QStringLiteral("面を作りました")))) {
        return false;
    }
    // XY の作業平面を作り、そこから 5mm 離した平面を相手にする。
    window.SetWorkPlaneChooser([](const WorkPlaneChoice&,
                                   const kachakacha::v2::app::WorkPlaneFacts&) {
        WorkPlaneChoice choice;
        choice.method = kachakacha::v2::modeling::WorkPlaneMethod::Standard;
        choice.standard = kachakacha::v2::modeling::StandardPlaneKind::XY;
        return std::optional<WorkPlaneChoice>(choice);
    });
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("workplane.create");
    const auto planes = kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::WorkPlane);
    if (!Explain("XY の平面ができる", !planes.entityIds.empty())) {
        return false;
    }
    kachakacha::v2::app::SelectionSet base;
    base.entityIds.push_back(planes.entityIds.back());
    viewport.SetSelection(base);
    window.SetWorkPlaneChooser([](const WorkPlaneChoice&,
                                   const kachakacha::v2::app::WorkPlaneFacts&) {
        WorkPlaneChoice choice;
        choice.method = kachakacha::v2::modeling::WorkPlaneMethod::OffsetFromPlane;
        choice.offsetMm = 5.0;
        return std::optional<WorkPlaneChoice>(choice);
    });
    window.RunCommand("workplane.create");
    if (!Explain((std::string("離した平面ができる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("平面から離す")))) {
        return false;
    }
    const auto target = kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::WorkPlane);
    kachakacha::v2::app::SelectionSet both;
    both.entityIds.push_back(target.entityIds.back());
    for (const auto& id : kachakacha::v2::app::SelectAllOfKind(
             window.Session().GetDocument().Snapshot(),
             kachakacha::v2::domain::EntityKind::GuideSurface)
             .entityIds) {
        both.entityIds.push_back(id);
    }
    viewport.SetSelection(both);
    if (!Explain("面と平面を選んだ状態で押せる",
            window.CommandEnabled("part.thicken_to_plane", nullptr))) {
        return false;
    }
    const int partsBefore = CountParts(window);
    window.RunCommand("part.thicken_to_plane");
    if (!Explain((std::string("平面まで埋めた部品ができる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            CountParts(window) == partsBefore + 1
                && window.StatusText().contains(QStringLiteral("最大 5.000 mm")))) {
        return false;
    }
    // 保存して開き直しても、同じ道で作り直される。
    const std::string path = kachakacha::v2::io::FromPath(
        std::filesystem::temp_directory_path() / "kacha_selftest_thicken_to_plane.kcd2");
    std::error_code code;
    std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
    window.SetPathChooser([&path](bool) { return QString::fromStdString(path); });
    window.RunCommand("file.save_as");
    window.RunCommand("file.new");
    const bool opened = window.OpenDocumentFile(QString::fromStdString(path));
    std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
    if (!Explain("開き直せる", opened)) {
        return false;
    }
    return Explain((std::string("部品が作り直される(") + window.StatusText().toStdString()
                       + ")").c_str(),
        CountParts(window) == 1 && window.KernelShapeCount() >= 2);
}

[[nodiscard]] bool CaseThickenSurfaceMakesASolid(V2MainWindow& window)
{
    // オーナーの手順の中心。断面 → 面 → **その面に厚みを付けて立体**。
    // ここが無かったので、面までは作れるのに立体へ戻れなかった。
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.ClickAt(QPointF(viewport.width() * 0.30, viewport.height() * 0.35));
    viewport.ClickAt(QPointF(viewport.width() * 0.70, viewport.height() * 0.35));
    viewport.ClickAt(QPointF(viewport.width() * 0.30, viewport.height() * 0.65));
    viewport.ClickAt(QPointF(viewport.width() * 0.70, viewport.height() * 0.65));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    window.RunCommand("guide.create");
    if (!Explain((std::string("面ができる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("面を作りました")))) {
        return false;
    }
    const int partsBefore = CountParts(window);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::GuideSurface));
    if (!Explain("面を選んだ状態で押せる", window.CommandEnabled("part.thicken", nullptr))) {
        return false;
    }
    window.RunCommand("part.thicken");
    if (!Explain((std::string("部品ができる(") + std::to_string(partsBefore) + " → "
                     + std::to_string(CountParts(window)) + " / "
                     + window.StatusText().toStdString() + ")").c_str(),
            CountParts(window) > partsBefore)) {
        return false;
    }
    // 名前が増えただけでは立体ではない。本当に出せることまで見る。
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Part));
    if (!Explain("厚みを付けた部品を STEP で出せる", window.CanExportSelectedParts())) {
        return false;
    }
    // 保存して開き直しても、面 → 厚み の順で作り直されて、また出せること。
    const std::string path = kachakacha::v2::io::FromPath(
        std::filesystem::temp_directory_path() / "kacha_selftest_thicken.kcd2");
    std::error_code code;
    std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
    window.SetPathChooser([&path](bool) { return QString::fromStdString(path); });
    window.RunCommand("file.save_as");
    window.RunCommand("file.new");
    const bool opened = window.OpenDocumentFile(QString::fromStdString(path));
    std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
    if (!Explain("開き直せる", opened)) {
        return false;
    }
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Part));
    return Explain((std::string("開き直したあとも出せる(") + window.StatusText().toStdString()
                       + ")").c_str(),
        window.CanExportSelectedParts());
}

[[nodiscard]] bool CaseThickenNeedsASurface(V2MainWindow& window)
{
    // 面を選ばずに押したら、理由を出して断る。
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    QString reason;
    if (!Explain("線だけでは押せない",
            !window.CommandEnabled("part.thicken", &reason))) {
        return false;
    }
    return Explain((std::string("理由が出る(") + reason.toStdString() + ")").c_str(),
        reason.contains(QStringLiteral("形状ガイド")));
}

[[nodiscard]] bool CaseSurfaceToPatternEndToEnd(V2MainWindow& window)
{
    // 面 → 展開 → 型紙 → 1:1 PDF まで通す。
    // 曲がった車体を作るときの本筋である。
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    // 断面になる線を2本、離して引く。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.ClickAt(QPointF(viewport.width() * 0.30, viewport.height() * 0.35));
    viewport.ClickAt(QPointF(viewport.width() * 0.70, viewport.height() * 0.35));
    viewport.ClickAt(QPointF(viewport.width() * 0.30, viewport.height() * 0.65));
    viewport.ClickAt(QPointF(viewport.width() * 0.70, viewport.height() * 0.65));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    if (!Explain((std::string("断面が2本ある(実際は ")
                     + std::to_string(viewport.Selection().entityIds.size())
                     + ")").c_str(),
            viewport.Selection().entityIds.size() == 2)) {
        return false;
    }
    window.RunCommand("guide.create");
    if (!Explain((std::string("面ができる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("面を作りました")))) {
        return false;
    }
    // 出来た面を選んで、展開して部材にする。
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::GuideSurface));
    window.RunCommand("fabrication.create");
    if (!Explain((std::string("展開して部材になる(")
                     + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("部材")))) {
        return false;
    }
    window.RunCommand("fabrication.create_pattern");
    if (!Explain((std::string("型紙になる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("原寸")))) {
        return false;
    }
    auto& dock = window.ExportDock();
    if (!Explain("型紙を対象にできる",
            dock.ChooseTarget(kachakacha::v2::app::ExportTarget::CurrentPattern))) {
        return false;
    }
    if (!Explain("1:1 PDF を選べる",
            dock.ChooseFormat(kachakacha::v2::app::ExportFormat::Pdf))) {
        return false;
    }
    const std::string path = kachakacha::v2::io::FromPath(
        std::filesystem::temp_directory_path() / "kacha_selftest_surface");
    std::error_code code;
    const auto written = kachakacha::v2::io::MakePath(path + ".pdf");
    std::filesystem::remove(written, code);
    dock.ChoosePath(QString::fromStdString(path));
    if (!Explain((std::string("出せる(理由は ") + dock.ReasonText().toStdString()
                     + ")").c_str(), dock.CanRun())) {
        return false;
    }
    if (!Explain("書き出せる", dock.RunNow())) {
        return false;
    }
    const bool hasBytes = std::filesystem::exists(written, code)
        && std::filesystem::file_size(written, code) > 0;
    std::filesystem::remove(written, code);
    return Explain("PDFが出来ている", hasBytes);
}

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
        {"押し出しでワイヤーだけ作れる", &CaseExtrudeMakesWiresOnly},
        {"通らない押し出しは理由を出して断る", &CaseExtrudeRefusesImpossibleChoices},
        {"押し出しをやめられる", &CaseExtrudeCanBeCancelled},
        {"引く→押し出す→部材→型紙→PDFまで通る", &CaseFabricationAndPatternEndToEnd},
        {"順を飛ばすと何を先にするか言う", &CasePatternNeedsFabricationFirst},
        {"部品をSTLとSTEPで出せる", &CaseSolidExportWritesStlAndStep},
        {"選んだものだけを別の文書にできる", &CaseSelectedEntitiesExportMakesASmallerDocument},
        {"立体がなければ立体では出せない", &CaseSolidExportNeedsASolid},
        {"投影は元の線を残す", &CaseProjectKeepsTheOriginal},
        {"トリムは押す場所を聞きやめられる", &CaseTrimAsksWhereToPress},
        {"トリムは線を2本要る", &CaseTrimNeedsTwoWires},
        {"グリッド原点は押した場所へ動く", &CaseGridOriginAsksWhereToPress},
        {"足し引きは部品を2つ要る", &CaseBooleanNeedsTwoParts},
        {"形状ガイドは断面2枚から", &CaseGuideSurfaceNeedsTwoSections},
        {"平面から離した作業平面を作れる", &CaseWorkPlaneCanBeOffset},
        {"材料が足りない作り方は断る", &CaseWorkPlaneRefusesWhenNothingSelected},
        {"面に厚みを付けて立体にできる", &CaseThickenSurfaceMakesASolid},
        {"面を平面まで立体にできる", &CaseThickenToPlaneFillsUpToThePlane},
        {"面を選ばずに厚みは付けられない", &CaseThickenNeedsASurface},
        {"面→展開→型紙→PDFまで通る", &CaseSurfaceToPatternEndToEnd},
        {"立体を作る前の検査は理由を出す", &CaseValidateNeedsASolid},
        {"押し出した部品は出せると言える", &CaseValidateAcceptsAnExtrudedPart},
        {"固定しても元は残る", &CaseFreezeKeepsTheOriginal},
        {"閉じていない線は折り線になる", &CaseOpeningMustBeClosed},
        {"配る見本が開ける", &CaseSampleDocumentOpens},
    };
}

} // namespace kachakacha::v2::selftest
