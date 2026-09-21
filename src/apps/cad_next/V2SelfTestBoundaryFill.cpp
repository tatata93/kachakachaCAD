//! 境界面で、外周と内側に引いた線をまとめて選ぶと、外周を輪にして内側の線を
//! 面が必ず通る線にする(オーナー方針 2026-09-22「人が引くワイヤーは絶対ここを通る面」)。
//!
//!   HP-SF-10 正方形の外周4本と、真ん中を縦に渡す1本を境界の欄へ全部入れる。
//!            枝分かれ(GEO-W002)で止まらず、外周1行 + 通る線1行になり、Enter で面になる。
//!
//! 線は画面のクリックで引き、欄へは 3D の素のクリックで入れる(人の道)。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::modeling::GuideSurfaceMethod;

//! 線を1本、画面の2か所を押して引く。
[[nodiscard]] bool DrawLineByClicks(V2MainWindow& window, const Vector3& from, const Vector3& to)
{
    auto& viewport = window.Viewport();
    const auto first = viewport.Mapping().Project(from);
    const auto second = viewport.Mapping().Project(to);
    if (!first.has_value() || !second.has_value()) {
        return false;
    }
    window.SelectTool(DrawingTool::Line);
    viewport.ClickAt(QPointF(first->x, first->y));
    viewport.HoverAt(QPointF(second->x, second->y));
    viewport.ClickAt(QPointF(second->x, second->y));
    window.SelectTool(DrawingTool::Select);
    return true;
}

//! 3D で、その線の真ん中を素で押す。
[[nodiscard]] bool ClickMiddleOf(V2MainWindow& window, const Vector3& from, const Vector3& to)
{
    auto& viewport = window.Viewport();
    const auto screen = viewport.Mapping().Project((from + to) * 0.5);
    if (!screen.has_value()) {
        return false;
    }
    viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
    return true;
}

//! HP-SF-10。
[[nodiscard]] bool CaseBoundaryFillTakesInnerLineAsPassThrough(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    const std::vector<std::pair<Vector3, Vector3>> lines{
        {{-20, -20, 0}, {20, -20, 0}}, {{20, -20, 0}, {20, 20, 0}},
        {{20, 20, 0}, {-20, 20, 0}}, {{-20, 20, 0}, {-20, -20, 0}},
        {{0, -20, 0}, {0, 20, 0}}};   // 外周4本と、真ん中を縦に渡す1本
    for (const auto& [from, to] : lines) {
        if (!Explain("線を画面で引ける", DrawLineByClicks(window, from, to))) {
            return false;
        }
    }
    if (!Explain((std::string("線が5本ある(実際 ")
                     + std::to_string(CountOfKind(window, EntityKind::Wire)) + ")").c_str(),
            CountOfKind(window, EntityKind::Wire) == 5)) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("surface.create");
    if (!Explain("境界面のカードが押せる",
            window.SurfaceDock().ClickMethodCard(GuideSurfaceMethod::BoundaryFill))) {
        return false;
    }
    if (!Explain("境界の欄が「ここへ選ぶ」",
            window.SurfaceDock().ActiveSlotShown() == ChainRole::BoundarySide)) {
        return false;
    }
    for (const auto& [from, to] : lines) {
        if (!Explain("線を 3D で押せる", ClickMiddleOf(window, from, to))) {
            return false;
        }
    }
    if (!Explain((std::string("5本とも境界の欄に入る(実際 ")
                     + std::to_string(window.SurfaceInput().boundaries.size()) + ")").c_str(),
            window.SurfaceInput().boundaries.size() == 5)) {
        return false;
    }
    const auto table = window.SurfaceTableFromInput();
    if (!Explain((std::string("枝分かれで止まらず表になる(")
                     + (table.HasValue() ? std::string("ok")
                                         : table.Diagnostics().front().summaryJa)
                     + ")").c_str(),
            table.HasValue())) {
        return false;
    }
    const auto& rows = table.Value().rows;
    if (!Explain((std::string("外周1行 + 通る線1行(実際 ") + std::to_string(rows.size())
                     + " 行)").c_str(),
            rows.size() == 2 && rows[0].role == ChainRole::BoundarySide
                && rows[0].segments.size() == 4 && rows[1].role == ChainRole::GuideU)) {
        return false;
    }
    if (!Explain((std::string("下見が出る(帯は ") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.SurfacePreviewShown())) {
        return false;
    }
    if (!Explain("Enter を窓が受け取る", window.HandleToolKey(Qt::Key_Return, nullptr))) {
        return false;
    }
    return Explain((std::string("面が1枚できる(帯は ") + window.StatusText().toStdString()
                       + ")").c_str(),
        CountOfKind(window, EntityKind::GuideSurface) == 1
            && !window.ShelfShown(Shelf::Surface));
}

} // namespace

std::vector<SelfTestCase> BoundaryFillCases()
{
    return {
        {"HP-SF-10 境界面は外周と内側の線をまとめて選んでも外周を輪にし内側を通る線にする",
            CaseBoundaryFillTakesInnerLineAsPassThrough},
    };
}

} // namespace kachakacha::v2::selftest
