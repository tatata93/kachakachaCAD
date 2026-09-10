//! 原点の3面・3軸と、作業平面の棚(V1 の「平面を作る」タブ)のケース。
//!
//! 作図は「どの平面の上に描くか」を決めてから始まる。ここが無いと
//! 工程1が画面から始められない。棚は core(app/WorkPlaneOptions)の判断を
//! そのまま出しているだけなので、ここでは「棚を通って文書に平面が入るか」を見る。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2WorkPlaneDock.h"

#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <QString>

#include <cmath>
#include <string>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::modeling::StandardPlaneKind;
using kachakacha::v2::modeling::WorkPlaneMethod;

//! 棚の道を使う。差し替えた答え(窓の代わり)を外す。
void UseDock(V2MainWindow& window)
{
    window.SetWorkPlaneChooser({});
}

[[nodiscard]] bool CaseOriginNodeShowsPlanesAndAxes(V2MainWindow& window)
{
    // 一覧の最上部に「原点」があり、3面と3軸が入っている(V1 と同じ)。
    if (!Explain((std::string("最上部が原点(") + window.GroupRowText(0).toStdString()
                     + ")").c_str(),
            window.GroupRowText(0) == QStringLiteral("原点"))) {
        return false;
    }
    if (!Explain((std::string("原点の子が6つ(実際は ")
                     + std::to_string(window.OriginChildCount()) + ")").c_str(),
            window.OriginChildCount() == 6)) {
        return false;
    }
    const char* expected[6] = {"top_XY", "front_XZ", "side_YZ", "X軸", "Y軸", "Z軸"};
    for (int row = 0; row < 6; ++row) {
        if (!Explain((std::string("並びが ") + expected[row] + "(実際は "
                         + window.OriginChildText(row).toStdString() + ")").c_str(),
                window.OriginChildText(row) == QString::fromUtf8(expected[row]))) {
            return false;
        }
    }
    // 軸はチェックで消せる。消しても文書は変わらない(見え方だけ)。
    const auto revision = window.Session().GetDocument().Revision();
    window.SetAxisShown(2, false);
    if (!Explain("Z軸を消せる", !window.Viewport().AxisVisible(2))) {
        return false;
    }
    if (!Explain("X軸は残る", window.Viewport().AxisVisible(0))) {
        return false;
    }
    window.SetAxisShown(2, true);
    if (!Explain("Z軸を戻せる", window.Viewport().AxisVisible(2))) {
        return false;
    }
    if (!Explain("文書は変わらない", window.Session().GetDocument().Revision() == revision)) {
        return false;
    }
    // 開いた直後は上面 XY が作業中(法線が +Z)。
    return Explain("上面 XY が作業中",
        std::abs(window.Viewport().WorkPlane().normal.z - 1.0) < 1.0e-9);
}

[[nodiscard]] bool CaseDockMakesPlaneByPointAndNormal(V2MainWindow& window)
{
    // 位置と向きを数で指定して平面を作る(V1 の「数値指定」)。選ぶものは要らない。
    UseDock(window);
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("workplane.create");
    V2WorkPlaneDock* dock = window.WorkPlaneDock();
    if (!Explain("棚がある", dock != nullptr)) {
        return false;
    }
    if (!Explain((std::string("棚へ案内する(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("平面を作る")))) {
        return false;
    }
    WorkPlaneChoice choice;
    choice.method = WorkPlaneMethod::PointNormal;
    choice.name = "断面A";
    choice.origin = kachakacha::v2::geometry::Vector3{0.0, 0.0, 40.0};
    choice.normal = kachakacha::v2::geometry::Vector3{0.0, 1.0, 0.0};
    choice.uAxis = kachakacha::v2::geometry::Vector3{1.0, 0.0, 0.0};
    dock->SetChoice(choice);
    if (!Explain((std::string("押せる(") + dock->NeedsText().toStdString() + ")").c_str(),
            dock->CanCreate())) {
        return false;
    }
    const int before = CountOfKind(window, kachakacha::v2::domain::EntityKind::WorkPlane);
    dock->PressCreate();
    if (!Explain((std::string("平面が増える(") + window.StatusText().toStdString()
                     + ")").c_str(),
            CountOfKind(window, kachakacha::v2::domain::EntityKind::WorkPlane) == before + 1)) {
        return false;
    }
    if (!Explain("付けた名前になる", window.StatusText().contains(QStringLiteral("断面A")))) {
        return false;
    }
    const auto& plane = window.Viewport().WorkPlane();
    if (!Explain((std::string("作業中の平面が y 向き(") + std::to_string(plane.normal.y)
                     + ")").c_str(),
            std::abs(plane.normal.y - 1.0) < 1.0e-9)) {
        return false;
    }
    return Explain((std::string("z=40 を通る(") + std::to_string(plane.origin.z) + ")").c_str(),
        std::abs(plane.origin.z - 40.0) < 1.0e-9);
}

[[nodiscard]] bool CaseDockOffsetsFromComboPlane(V2MainWindow& window)
{
    // 基準平面はコンボで選べる。何も選んでいなくても、原点の3面から離せる。
    UseDock(window);
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("workplane.create");
    V2WorkPlaneDock* dock = window.WorkPlaneDock();
    if (dock == nullptr) {
        return false;
    }
    const auto front = kachakacha::v2::app::OriginPlaneId(
        window.Session().GetDocument().Snapshot(), StandardPlaneKind::ZX);
    if (!Explain("front_XZ がある", front.has_value())) {
        return false;
    }
    WorkPlaneChoice choice;
    choice.method = WorkPlaneMethod::OffsetFromPlane;
    choice.referencePlaneId = front;
    choice.offsetMm = 12.0;
    dock->SetChoice(choice);
    if (!Explain((std::string("コンボの平面で足りる(") + dock->NeedsText().toStdString()
                     + ")").c_str(),
            dock->CanCreate())) {
        return false;
    }
    dock->PressCreate();
    if (!Explain((std::string("離した平面ができる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("平面から離す")))) {
        return false;
    }
    // 正面 XZ の法線は ±Y。12mm 離れているのは y。
    const auto& plane = window.Viewport().WorkPlane();
    return Explain((std::string("y が 12 離れる(") + std::to_string(plane.origin.y)
                       + ")").c_str(),
        std::abs(std::abs(plane.origin.y) - 12.0) < 1.0e-9);
}

[[nodiscard]] bool CaseDockSaysWhatIsMissing(V2MainWindow& window)
{
    // 押してから断らない。足りないものは棚に出て、押せない。
    UseDock(window);
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("workplane.create");
    V2WorkPlaneDock* dock = window.WorkPlaneDock();
    if (dock == nullptr) {
        return false;
    }
    WorkPlaneChoice choice;
    choice.method = WorkPlaneMethod::TwoEdges;
    dock->SetChoice(choice);
    if (!Explain((std::string("足りないと出る(") + dock->NeedsText().toStdString()
                     + ")").c_str(),
            dock->NeedsText().contains(QStringLiteral("足りません")))) {
        return false;
    }
    if (!Explain("押せない", !dock->CanCreate())) {
        return false;
    }
    const int before = CountOfKind(window, kachakacha::v2::domain::EntityKind::WorkPlane);
    dock->PressCreate();
    if (!Explain("押しても増えない",
            CountOfKind(window, kachakacha::v2::domain::EntityKind::WorkPlane) == before)) {
        return false;
    }
    // 3点は数の欄で代えられるので、何も選んでいなくても押せる。
    choice.method = WorkPlaneMethod::ThreePoints;
    dock->SetChoice(choice);
    return Explain((std::string("3点は数で足りる(") + dock->NeedsText().toStdString()
                       + ")").c_str(),
        dock->CanCreate());
}

[[nodiscard]] bool CaseOriginPlanesSurviveSaveAndOpen(V2MainWindow& window)
{
    // 原点の3面は保存しても二重にならない(開き直すと足し直すので)。
    UseDock(window);
    const int before = CountOfKind(window, kachakacha::v2::domain::EntityKind::WorkPlane);
    if (!Explain("はじめは3面", before == 3)) {
        return false;
    }
    window.RunCommand("file.new");
    return Explain((std::string("新しい文書でも3面(実際は ")
                       + std::to_string(
                             CountOfKind(window, kachakacha::v2::domain::EntityKind::WorkPlane))
                       + ")").c_str(),
        CountOfKind(window, kachakacha::v2::domain::EntityKind::WorkPlane) == 3);
}

} // namespace

std::vector<SelfTestCase> PlaneCases()
{
    return {
        {"一覧の原点に3面と3軸が出て、軸はチェックで消せる", &CaseOriginNodeShowsPlanesAndAxes},
        {"作業平面の棚で位置と向きを数で指定して作れる", &CaseDockMakesPlaneByPointAndNormal},
        {"作業平面の棚でコンボの基準平面から離せる", &CaseDockOffsetsFromComboPlane},
        {"作業平面の棚は足りないものをその場で言い、押せない", &CaseDockSaysWhatIsMissing},
        {"新しい文書にも原点の3面がある", &CaseOriginPlanesSurviveSaveAndOpen},
    };
}

} // namespace kachakacha::v2::selftest
