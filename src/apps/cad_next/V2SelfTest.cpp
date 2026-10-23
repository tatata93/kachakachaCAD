//! 自己診断を回すところ(WP-08)。ケースそのものは別ファイルにある。
//!
//! ケースごとに窓を作り直す。前のケースの状態を持ち越すと、
//! 「単体では通るのに並べると落ちる」という追いにくい失敗が出る。

#include "V2SelfTest.h"
#include "V2ToolEvidence.h"
#include "V2SelfTestReporting.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <random>
#include <exception>
#include <chrono>
#include <cstdlib>
#include <string>
#include <iostream>
#include <vector>

namespace kachakacha::v2::selftest {
std::vector<SelfTestCase> InstructionCases();
std::vector<SelfTestCase> InstructionArrowCases();
std::vector<SelfTestCase> InstructionSheetCases();
std::vector<SelfTestCase> CurvedEmbossCases();
std::vector<SelfTestCase> DocumentOpenCases();
std::vector<SelfTestCase> FaceExtrudeCases();
std::vector<SelfTestCase> ProjectionCases();
std::vector<SelfTestCase> NavigationCases();
namespace {

//! 1ケースだけ動かす。落ちても続けられるように、例外はここで受ける。
[[nodiscard]] bool RunOneCase(const SelfTestCase& item)
{
    V2MainWindow window;
    // 既存の2D作図ケースの明示的な前提。起動・新規の視点は別ケースで検査する。
    window.Viewport().SetViewDirection(ViewDirection::Top);
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
        V2ToolEvidence::BeginCase(item.name);
        const bool passed=item.body(window);
        V2ToolEvidence::EndCase(window,passed);
        return passed;
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
    std::vector<SelfTestCase> cases = WithSuite(BasicCases(), "Basic");
    const auto documentOpen = WithSuite(DocumentOpenCases(), "DocumentOpen");
    cases.insert(cases.end(), documentOpen.begin(), documentOpen.end());
    const std::vector<SelfTestCase> menus = WithSuite(MenuCases(), "Menu");
    cases.insert(cases.end(), menus.begin(), menus.end());
    const std::vector<SelfTestCase> input = WithSuite(InputCases(), "Input");
    cases.insert(cases.end(), input.begin(), input.end());
    const std::vector<SelfTestCase> modeling = WithSuite(ModelingCases(), "Modeling");
    cases.insert(cases.end(), modeling.begin(), modeling.end());
    const auto usability=WithSuite(UsabilityCases(), "Usability");cases.insert(cases.end(),usability.begin(),usability.end());
    const auto images=WithSuite(ImageCases(), "Image");cases.insert(cases.end(),images.begin(),images.end());
    const auto emboss=WithSuite(CurvedEmbossCases(), "CurvedEmboss");cases.insert(cases.end(),emboss.begin(),emboss.end());
    const auto instructions=WithSuite(InstructionCases(), "Instruction");cases.insert(cases.end(),instructions.begin(),instructions.end());
    for(const auto& group:{WithSuite(InstructionArrowCases(), "InstructionArrow"),WithSuite(InstructionSheetCases(), "InstructionSheet")})cases.insert(cases.end(),group.begin(),group.end());
    const std::vector<SelfTestCase> planes = WithSuite(PlaneCases(), "Plane");
    cases.insert(cases.end(), planes.begin(), planes.end());
    const auto dimensions=WithSuite(DimensionCases(), "Dimension");cases.insert(cases.end(),dimensions.begin(),dimensions.end());
    const std::vector<SelfTestCase> drawing = WithSuite(DrawingCases(), "Drawing");
    cases.insert(cases.end(), drawing.begin(), drawing.end());
    const std::vector<SelfTestCase> edit = WithSuite(EditCases(), "Edit");
    cases.insert(cases.end(), edit.begin(), edit.end());
    const std::vector<SelfTestCase> screen = WithSuite(ScreenCases(), "Screen");
    const std::vector<SelfTestCase> facing = WithSuite(FacingCases(), "Facing");
    const std::vector<SelfTestCase> groups = WithSuite(GroupCases(), "Group");
    const std::vector<SelfTestCase> hoModel = WithSuite(HoModelCases(), "HoModel");
    const std::vector<SelfTestCase> humanPath = WithSuite(HumanPathCases(), "HumanPath");
    cases.insert(cases.end(), humanPath.begin(), humanPath.end());
    const std::vector<SelfTestCase> humanPathApprox = WithSuite(HumanPathApproxCases(), "HumanPathApprox");
    cases.insert(cases.end(), humanPathApprox.begin(), humanPathApprox.end());
    const auto output=WithSuite(OutputCases(), "Output");cases.insert(cases.end(),output.begin(),output.end());
    const auto faceExtrude=WithSuite(FaceExtrudeCases(), "FaceExtrude");cases.insert(cases.end(),faceExtrude.begin(),faceExtrude.end());
    const auto projection=WithSuite(ProjectionCases(), "Projection");cases.insert(cases.end(),projection.begin(),projection.end());
    const auto navigation=WithSuite(NavigationCases(), "Navigation");cases.insert(cases.end(),navigation.begin(),navigation.end());
    const std::vector<SelfTestCase> humanPathBoolean = WithSuite(HumanPathBooleanCases(), "HumanPathBoolean");
    cases.insert(cases.end(), humanPathBoolean.begin(), humanPathBoolean.end());
    const std::vector<SelfTestCase> humanPathCorner = WithSuite(HumanPathCornerCases(), "HumanPathCorner");
    cases.insert(cases.end(), humanPathCorner.begin(), humanPathCorner.end());
    const std::vector<SelfTestCase> ribbon = WithSuite(RibbonCases(), "Ribbon");
    cases.insert(cases.end(), ribbon.begin(), ribbon.end());
    const std::vector<SelfTestCase> explorer = WithSuite(ExplorerCases(), "Explorer");
    cases.insert(cases.end(), explorer.begin(), explorer.end());
    const std::vector<SelfTestCase> statusLine = WithSuite(StatusLineCases(), "StatusLine");
    cases.insert(cases.end(), statusLine.begin(), statusLine.end());
    const std::vector<SelfTestCase> drawingMethods = WithSuite(DrawingMethodCases(), "DrawingMethod");
    cases.insert(cases.end(), drawingMethods.begin(), drawingMethods.end());
    const std::vector<SelfTestCase> partPanel = WithSuite(PartPanelCases(), "PartPanel");
    cases.insert(cases.end(), partPanel.begin(), partPanel.end());
    const std::vector<SelfTestCase> array = WithSuite(ArrayCases(), "Array");
    cases.insert(cases.end(), array.begin(), array.end());
    const std::vector<SelfTestCase> panelFrame = WithSuite(PanelFrameCases(), "PanelFrame");
    cases.insert(cases.end(), panelFrame.begin(), panelFrame.end());
    const std::vector<SelfTestCase> partPlace = WithSuite(PartPlaceCases(), "PartPlace");
    cases.insert(cases.end(), partPlace.begin(), partPlace.end());
    const std::vector<SelfTestCase> solid = WithSuite(SolidCases(), "Solid");
    cases.insert(cases.end(), solid.begin(), solid.end());
    const std::vector<SelfTestCase> edgeFinish = WithSuite(EdgeFinishCases(), "EdgeFinish");
    cases.insert(cases.end(), edgeFinish.begin(), edgeFinish.end());
    const std::vector<SelfTestCase> shellSplit = WithSuite(ShellSplitCases(), "ShellSplit");
    cases.insert(cases.end(), shellSplit.begin(), shellSplit.end());
    const std::vector<SelfTestCase> hoverEdit = WithSuite(HoverEditCases(), "HoverEdit");
    cases.insert(cases.end(), hoverEdit.begin(), hoverEdit.end());
    const std::vector<SelfTestCase> generate = WithSuite(GenerateCases(), "Generate");
    cases.insert(cases.end(), generate.begin(), generate.end());
    const std::vector<SelfTestCase> unfold = WithSuite(UnfoldCases(), "Unfold");
    cases.insert(cases.end(), unfold.begin(), unfold.end());
    const std::vector<SelfTestCase> thicken = WithSuite(ThickenCases(), "Thicken");
    cases.insert(cases.end(), thicken.begin(), thicken.end());
    const std::vector<SelfTestCase> surfaceEdit = WithSuite(SurfaceEditCases(), "SurfaceEdit");
    cases.insert(cases.end(), surfaceEdit.begin(), surfaceEdit.end());
    const std::vector<SelfTestCase> surfaceAnalysis = WithSuite(SurfaceAnalysisCases(), "SurfaceAnalysis");
    cases.insert(cases.end(), surfaceAnalysis.begin(), surfaceAnalysis.end());
    const std::vector<SelfTestCase> workPlanePreview = WithSuite(WorkPlanePreviewCases(), "WorkPlanePreview");
    cases.insert(cases.end(), workPlanePreview.begin(), workPlanePreview.end());
    const std::vector<SelfTestCase> regressions = WithSuite(RegressionCases(), "Regression");
    cases.insert(cases.end(), regressions.begin(), regressions.end());
    const std::vector<SelfTestCase> promise = WithSuite(ExtrudePromiseCases(), "ExtrudePromise");
    const std::vector<SelfTestCase> graph = WithSuite(ExtrudeGraphCases(), "ExtrudeGraph");
    const std::vector<SelfTestCase> approxFlow = WithSuite(ApproximationFlowCases(), "ApproximationFlow");
    cases.insert(cases.end(), screen.begin(), screen.end());
    cases.insert(cases.end(), facing.begin(), facing.end());
    cases.insert(cases.end(), groups.begin(), groups.end());
    cases.insert(cases.end(), promise.begin(), promise.end());
    cases.insert(cases.end(), graph.begin(), graph.end());
    cases.insert(cases.end(), approxFlow.begin(), approxFlow.end());
    cases.insert(cases.end(), hoModel.begin(), hoModel.end());
    const std::vector<SelfTestCase> semantics = WithSuite(SemanticStateCases(), "SemanticState");
    cases.insert(cases.end(), semantics.begin(), semantics.end());
    const std::vector<SelfTestCase> pointer = WithSuite(PointerCases(), "Pointer");
    cases.insert(cases.end(), pointer.begin(), pointer.end());
    const std::vector<SelfTestCase> guide = WithSuite(GuideCases(), "Guide");
    cases.insert(cases.end(), guide.begin(), guide.end());
    const std::vector<SelfTestCase> fabrication = WithSuite(FabricationCases(), "Fabrication");
    cases.insert(cases.end(), fabrication.begin(), fabrication.end());
    const std::vector<SelfTestCase> partEdit = WithSuite(PartEditCases(), "PartEdit");
    cases.insert(cases.end(), partEdit.begin(), partEdit.end());
    const std::vector<SelfTestCase> responsive = WithSuite(ResponsiveCases(), "Responsive");
    cases.insert(cases.end(), responsive.begin(), responsive.end());
    const std::vector<SelfTestCase> accuracy = WithSuite(DrawingAccuracyCases(), "DrawingAccuracy");
    cases.insert(cases.end(), accuracy.begin(), accuracy.end());
    const std::vector<SelfTestCase> boundaryFill = WithSuite(BoundaryFillCases(), "BoundaryFill");
    cases.insert(cases.end(), boundaryFill.begin(), boundaryFill.end());
    const std::vector<SelfTestCase> loopFaces = WithSuite(LoopFacesCases(), "LoopFaces");
    cases.insert(cases.end(), loopFaces.begin(), loopFaces.end());
    const auto outlines=WithSuite(ApproxOutlineCases(), "ApproxOutline");cases.insert(cases.end(),outlines.begin(),outlines.end());
    const auto practical=WithSuite(PracticalCases(), "Practical");cases.insert(cases.end(),practical.begin(),practical.end());
    const auto gptFabrication = WithSuite(GptFabricationCases(), "GptFabrication");
    cases.insert(cases.end(), gptFabrication.begin(), gptFabrication.end());
    const auto gptSurface = WithSuite(GptSurfaceCases(), "GptSurface");
    cases.insert(cases.end(), gptSurface.begin(), gptSurface.end());
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

void Note(const char* what)
{
    std::cerr << "  調べた: " << what << '\n';
}

int RunSelfTest()
{
    auto cases = AllCases();
    const auto arguments=QCoreApplication::arguments();const int orderFlag=arguments.indexOf("--self-test-order");
    if(orderFlag>=0){const auto order=arguments.value(orderFlag+1);
        if(order=="reverse")std::reverse(cases.begin(),cases.end());
        else {bool ok=false;const auto seed=order.startsWith("seed:")?order.mid(5).toUInt(&ok):0;
            if(!ok){std::cerr<<"Invalid explicit self-test order\n";return 1;}
            std::mt19937 generator(seed);std::shuffle(cases.begin(),cases.end(),generator);}}
    Reporting reporting(cases);
    if (!reporting.Valid()) {
        std::cerr << reporting.Error().toUtf8().constData() << '\n';
        (void)reporting.Save();
        return 1;
    }
    if (reporting.ListOnly()) return reporting.Save() ? 0 : 1;
    int failed = 0;
    int total = 0;
    for (const SelfTestCase& item : cases) {
        if (!reporting.Contains(item.name)) { continue; }
        ++total;
        // 先に名前を出しておく。途中で落ちたとき、どのケースで落ちたかが残る。
        // 落ちると PASS/FAIL のどちらも出ないので、名前が無いと追えない。
        std::cout << "RUN  " << item.name << std::endl;
        const auto tick = std::chrono::steady_clock::now();
        const auto started = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        const bool passed = RunOneCase(item);
        const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tick).count();
        reporting.Add(item.name, passed, milliseconds, started);
        if (passed) {
            std::cout << "PASS " << item.name << std::endl;
        } else {
            std::cout << "FAIL " << item.name << std::endl;
            ++failed;
        }
    }
    std::cout << "cad_next self-test: " << (total - failed) << " passed, " << failed
              << " failed, " << total << " total\n";
    const bool saved = reporting.Save(); // Failed runs still need a complete report for diagnosis.
    return failed == 0 && total > 0 && saved ? 0 : 1;
}

} // namespace kachakacha::v2::selftest
