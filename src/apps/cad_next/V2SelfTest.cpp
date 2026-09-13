//! 自己診断を回すところ(WP-08)。ケースそのものは別ファイルにある。
//!
//! ケースごとに窓を作り直す。前のケースの状態を持ち越すと、
//! 「単体では通るのに並べると落ちる」という追いにくい失敗が出る。

#include "V2SelfTest.h"

#include "V2MainWindow.h"

#include <QApplication>
#include <QString>
#include <QStringList>

#include <exception>
#include <iostream>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

//! 1ケースだけ動かす。落ちても続けられるように、例外はここで受ける。
[[nodiscard]] bool RunOneCase(const SelfTestCase& item)
{
    V2MainWindow window;
    // 画面を出さずに試すので、ファイルダイアログを出させない。
    // 出すと、そこで止まったまま返ってこない。
    window.SetPathChooser([](bool) { return QString(); });
    // 押し出しの窓も同じ理由で出さない。出すと、そこで止まったまま返ってこない。
    // 既定は「出した窓をそのまま承知した」と同じ結果にする。
    // 選択肢を確かめるケースは、自分でこれを差し替える。
    window.SetExtrudeChooser([](const kachakacha::v2::app::ExtrudeChoice& initial,
                                 const kachakacha::v2::app::ExtrudeFacts&) {
        return std::optional<kachakacha::v2::app::ExtrudeChoice>(initial);
    });
    window.SetWorkPlaneChooser([](const WorkPlaneChoice& initial,
                                   const kachakacha::v2::app::WorkPlaneFacts&) {
        return std::optional<WorkPlaneChoice>(initial);
    });
    window.SetAssemblyChooser([](double current) { return std::optional<double>(current); });
    // 作り方や役割を聞く窓も出さない。既定は「いま選ばれているもの」をそのまま返す。
    window.SetGuideChoiceChooser([](const QString&, const QStringList&, int initial) {
        return std::optional<int>(initial);
    });
    window.resize(1000, 700);
    window.show();
    QApplication::processEvents();
    try {
        return item.body(window);
    } catch (const std::exception& error) {
        std::cout << "  例外: " << error.what() << std::endl;
        return false;
    } catch (...) {
        std::cout << "  未知の例外" << std::endl;
        return false;
    }
}

//! 種類ごとのケースを 1 本につなげる。
[[nodiscard]] std::vector<SelfTestCase> AllCases()
{
    std::vector<SelfTestCase> cases = BasicCases();
    const std::vector<SelfTestCase> input = InputCases();
    cases.insert(cases.end(), input.begin(), input.end());
    const std::vector<SelfTestCase> modeling = ModelingCases();
    cases.insert(cases.end(), modeling.begin(), modeling.end());
    const std::vector<SelfTestCase> planes = PlaneCases();
    cases.insert(cases.end(), planes.begin(), planes.end());
    const std::vector<SelfTestCase> drawing = DrawingCases();
    cases.insert(cases.end(), drawing.begin(), drawing.end());
    const std::vector<SelfTestCase> edit = EditCases();
    cases.insert(cases.end(), edit.begin(), edit.end());
    const std::vector<SelfTestCase> screen = ScreenCases();
    cases.insert(cases.end(), screen.begin(), screen.end());
    const std::vector<SelfTestCase> semantics = SemanticStateCases();
    cases.insert(cases.end(), semantics.begin(), semantics.end());
    const std::vector<SelfTestCase> guide = GuideCases();
    cases.insert(cases.end(), guide.begin(), guide.end());
    const std::vector<SelfTestCase> fabrication = FabricationCases();
    cases.insert(cases.end(), fabrication.begin(), fabrication.end());
    return cases;
}

} // namespace

int CountOfKind(V2MainWindow& window, kachakacha::v2::domain::EntityKind kind)
{
    int count = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kind) {
            ++count;
        }
    }
    return count;
}

bool Explain(const char* what, bool ok)
{
    if (!ok) {
        std::cerr << "  期待が外れた: " << what << '\n';
    }
    return ok;
}

int RunSelfTest()
{
    const std::vector<SelfTestCase> cases = AllCases();
    int failed = 0;
    for (const SelfTestCase& item : cases) {
        // 先に名前を出しておく。途中で落ちたとき、どのケースで落ちたかが残る。
        // 落ちると PASS/FAIL のどちらも出ないので、名前が無いと追えない。
        std::cout << "RUN  " << item.name << std::endl;
        if (RunOneCase(item)) {
            std::cout << "PASS " << item.name << std::endl;
        } else {
            std::cout << "FAIL " << item.name << std::endl;
            ++failed;
        }
    }
    const int total = static_cast<int>(cases.size());
    std::cout << "cad_next self-test: " << (total - failed) << " passed, " << failed
              << " failed, " << total << " total\n";
    return failed == 0 ? 0 : 1;
}

} // namespace kachakacha::v2::selftest
