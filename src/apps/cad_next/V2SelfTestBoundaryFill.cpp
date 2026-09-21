//! 境界面で、外周と内側に引いた線をまとめて選ぶと、外周を輪にして内側の線を
//! 面が必ず通る線にする(オーナー方針 2026-09-22「人が引くワイヤーは絶対ここを通る面」)。
//!
//!   HP-SF-10 正方形の外周4本と、真ん中を縦に渡す1本を境界の欄へ全部入れる。
//!            枝分かれ(GEO-W002)で止まらず、外周1行 + 通る線1行になり、Enter で面になる。
//!
//!   HP-SF-11 ロフトに断面 3 本とガイド 3 本(3 本目は内側)を普通のクリックで入れる。
//!            3 本とも形に効く作り方が選ばれ、一覧から 1 本外す・戻すができ、Enter で面になる。
//!   HP-SF-12 四辺面を 4 辺から作る。張り方を選べ、Enter で面になる。
//!
//! 線は画面のクリックで引き、欄へは 3D の素のクリックで入れる(人の道)。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2SurfaceDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SurfaceInputState.h"
#include "kachakacha/app/SurfaceRoleAssist.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

#include <algorithm>
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
    int boundaries = 0;
    int through = 0;
    for (const auto& row : rows) {
        boundaries += row.role == ChainRole::BoundarySide ? 1 : 0;
        through += row.role == ChainRole::GuideU ? 1 : 0;
    }
    if (!Explain((std::string("外周4辺 + 通る線1本(実際 境界 ") + std::to_string(boundaries)
                     + "・通る線 " + std::to_string(through) + ")").c_str(),
            boundaries == 4 && through == 1)) {
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

//! 線を全部引く。引けた本数を返す。
[[nodiscard]] int DrawAll(V2MainWindow& window,
    const std::vector<std::pair<Vector3, Vector3>>& lines)
{
    int drawn = 0;
    for (const auto& [from, to] : lines) {
        drawn += DrawLineByClicks(window, from, to) ? 1 : 0;
    }
    return drawn;
}

//! HP-SF-11。
[[nodiscard]] bool CaseLoftTakesEveryRail(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    // 断面は x = 0, 20, 40 の縦線、ガイドは y = 0, 10, 20 の横線(10 は断面の真ん中を通る)。
    const std::vector<std::pair<Vector3, Vector3>> sections{
        {{0, 0, 0}, {0, 20, 0}}, {{20, 0, 0}, {20, 20, 0}}, {{40, 0, 0}, {40, 20, 0}}};
    const std::vector<std::pair<Vector3, Vector3>> rails{
        {{0, 0, 0}, {40, 0, 0}}, {{0, 10, 0}, {40, 10, 0}}, {{0, 20, 0}, {40, 20, 0}}};
    if (!Explain("線を画面で引ける", DrawAll(window, sections) == 3 && DrawAll(window, rails) == 3)) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("surface.create");
    if (!Explain("ロフトのカードが押せる",
            window.SurfaceDock().ClickMethodCard(GuideSurfaceMethod::LoftSections))) {
        return false;
    }
    for (const auto& [from, to] : sections) {
        // 断面は端に近いところを押す(真ん中は内側のガイドとの交点で、どちらを拾うか曖昧)。
        if (!Explain("断面を 3D で押せる", ClickMiddleOf(window, from, (from + to) * 0.5))) {
            return false;
        }
    }
    if (!Explain("ガイドの「ここへ選ぶ」が押せる",
            window.SurfaceDock().ClickActivate(ChainRole::GuideU))) {
        return false;
    }
    for (const auto& [from, to] : rails) {
        if (!Explain("ガイドを 3D で押せる", ClickMiddleOf(window, from, (from + to) * 0.5))) {
            return false;
        }
    }
    const auto& in = window.SurfaceInput();
    if (!Explain((std::string("断面 3 本・ガイド 3 本(実際 ") + std::to_string(in.sections.size())
                     + "・" + std::to_string(in.guides.size()) + ")").c_str(),
            in.sections.size() == 3 && in.guides.size() == 3)) {
        return false;
    }
    if (!Explain("ガイドの一覧に 3 行出ている",
            window.SurfaceDock().SlotEntryTexts(ChainRole::GuideU).size() == 3)) {
        return false;
    }
    if (!Explain((std::string("下見が出る(帯は ") + window.StatusText().toStdString() + ")").c_str(),
            window.SurfacePreviewShown())) {
        return false;
    }
    // 一覧から 1 本外すと 2 本になり、3D でもう一度押すと 3 本に戻る(一覧と 3D は同じもの)。
    if (!Explain("一覧から 3 本目を外せる", window.SurfaceDock().ClickRemoveEntry(ChainRole::GuideU, 2))
        || !Explain("ガイドが 2 本になる", in.guides.size() == 2)
        || !Explain("3D で押し直せる", ClickMiddleOf(window, rails[2].first,
                                        (rails[2].first + rails[2].second) * 0.5))
        || !Explain("ガイドが 3 本に戻る", in.guides.size() == 3)) {
        return false;
    }
    if (!Explain("Enter を窓が受け取る", window.HandleToolKey(Qt::Key_Return, nullptr))) {
        return false;
    }
    return Explain((std::string("面が1枚できる(帯は ") + window.StatusText().toStdString()
                       + ")").c_str(),
        CountOfKind(window, EntityKind::GuideSurface) == 1);
}

//! HP-SF-12。
[[nodiscard]] bool CaseFourEdgePatchFromFourSides(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    const std::vector<std::pair<Vector3, Vector3>> sides{{{-20, -10, 0}, {20, -10, 0}},
        {{20, -10, 0}, {20, 10, 0}}, {{20, 10, 0}, {-20, 10, 0}}, {{-20, 10, 0}, {-20, -10, 0}}};
    if (!Explain("4 辺を画面で引ける", DrawAll(window, sides) == 4)) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("surface.create");
    if (!Explain("四辺面のカードが押せる",
            window.SurfaceDock().ClickMethodCard(GuideSurfaceMethod::FourEdgePatch))) {
        return false;
    }
    for (const auto& [from, to] : sides) {
        if (!Explain("辺を 3D で押せる", ClickMiddleOf(window, from, to))) {
            return false;
        }
    }
    if (!Explain("張り方を選べる(丸み優先)",
            window.SurfaceDock().ChooseFourEdgeStyle(kachakacha::v2::modeling::FourEdgeStyle::Curved))
        || !Explain("張り方が入力に入る",
            window.SurfaceInput().fourEdgeStyle == kachakacha::v2::modeling::FourEdgeStyle::Curved)) {
        return false;
    }
    if (!Explain((std::string("下見が出る(帯は ") + window.StatusText().toStdString() + ")").c_str(),
            window.SurfacePreviewShown())) {
        return false;
    }
    if (!Explain("Enter を窓が受け取る", window.HandleToolKey(Qt::Key_Return, nullptr))) {
        return false;
    }
    return Explain("面が1枚できる", CountOfKind(window, EntityKind::GuideSurface) == 1);
}

//! HP-SF-13。おまかせ: 道具を押し、線を普通に押すだけで役割と作り方が決まる。理由と他の候補が
//! 棚に出て、3D は役割の色になる。もう一度押すと外れる。行ごとに役割を直せ、他の候補も使える。
[[nodiscard]] bool CaseBeginnerRoleAssist(V2MainWindow& window)
{
    using kachakacha::v2::app::WireRoleChoice;
    window.RunCommand("file.new");
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    // 断面は x = 0, 30, 60 の縦線、ガイドは y = 0 と y = 20 の長手の線(断面の両端を通る)。
    const std::vector<std::pair<Vector3, Vector3>> sections{
        {{0, 0, 0}, {0, 20, 0}}, {{30, 0, 0}, {30, 20, 0}}, {{60, 0, 0}, {60, 20, 0}}};
    const std::vector<std::pair<Vector3, Vector3>> rails{
        {{0, 0, 0}, {60, 0, 0}}, {{0, 20, 0}, {60, 20, 0}}};
    if (!Explain("線を画面で引ける", DrawAll(window, sections) == 3 && DrawAll(window, rails) == 2)) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("surface.create");
    if (!Explain("作り方を選ばずに始めるとおまかせ", window.SurfaceInput().autoRoles)) {
        return false;
    }
    // 普通に押す(Ctrl なし)。ガイドは断面の端と重ならない 1/4 の所を押す。
    for (const auto& [from, to] : sections) {
        if (!Explain("断面を 3D で押せる", ClickMiddleOf(window, from, to))) {
            return false;
        }
    }
    for (const auto& [from, to] : rails) {
        if (!Explain("ガイドを 3D で押せる", ClickMiddleOf(window, from, (from + to) * 0.5))) {
            return false;
        }
    }
    const auto& in = window.SurfaceInput();
    const QString assist = window.SurfaceDock().RoleAssistTextJa();
    if (!Explain(("ガイド付きロフトに決まる(" + assist.toStdString() + ")").c_str(),
            in.method == GuideSurfaceMethod::LoftSections && in.guides.size() == 2
                && in.sections.size() == 3)
        || !Explain("理由と他の候補が棚に出る",
            assist.contains(QStringLiteral("おすすめ: ガイド付きロフト"))
                && assist.contains(QStringLiteral("ガイド候補: 2本"))
                && assist.contains(QStringLiteral("断面候補: 3本"))
                && assist.contains(QStringLiteral("他の候補")))
        || !Explain(("下見が出る(帯は " + window.StatusText().toStdString() + ")").c_str(),
            window.SurfacePreviewShown())) {
        return false;
    }
    const auto railColor = window.Viewport().RoleColorOf(in.guides.front());
    const auto sectionColor = window.Viewport().RoleColorOf(in.sections.front());
    if (!Explain("3D はガイドが青、断面が橙", railColor.has_value() && sectionColor.has_value()
                && railColor->blue() > railColor->red() && sectionColor->red() > sectionColor->blue())) {
        return false;
    }
    // もう一度押すと外れ、また押すと戻る。
    if (!Explain("断面をもう一度押せる", ClickMiddleOf(window, sections[1].first, sections[1].second))
        || !Explain("再クリックで外れる(残りの 4 本は輪なので平面になる)",
            kachakacha::v2::app::AllSurfaceEntries(in).size() == 4
                && in.method == GuideSurfaceMethod::PlanarBoundary)
        || !Explain("もう一度押して戻せる", ClickMiddleOf(window, sections[1].first, sections[1].second))
        || !Explain("5 本に戻る", in.sections.size() == 3 && in.guides.size() == 2)) {
        return false;
    }
    // 右の棚で、ガイドの 1 行目を「断面」に直す。人の決めた役割はそのまま使う。
    const auto rail = in.guides.front();
    if (!Explain("行の役割を選べる",
            window.SurfaceDock().ChooseEntryRole(ChainRole::GuideU, 0, WireRoleChoice::Section))
        || !Explain("その線は断面になる",
            std::find(in.sections.begin(), in.sections.end(), rail) != in.sections.end())
        || !Explain("人が決めた印", std::any_of(window.SurfaceRoles().wires.begin(),
                                        window.SurfaceRoles().wires.end(), [&rail](const auto& w) {
                                            return w.id == rail && w.fixedByUser;
                                        }))) {
        return false;
    }
    const auto order = kachakacha::v2::app::SurfaceSectionOrder(in);
    const int shownRow = static_cast<int>(std::find(order.begin(), order.end(), rail) - order.begin());
    if (!Explain("自動に戻せる",
            window.SurfaceDock().ChooseEntryRole(ChainRole::Section, shownRow, WireRoleChoice::Auto))
        || !Explain("元の分け方に戻る", in.guides.size() == 2 && in.sections.size() == 3
                && std::find(in.guides.begin(), in.guides.end(), rail) != in.guides.end())) {
        return false;
    }
    // 他の候補(境界面)を使い、おまかせに戻す。
    int fill = -1;
    const auto& alternatives = window.SurfaceRoles().alternatives;
    for (std::size_t index = 0; index < alternatives.size(); ++index) {
        if (alternatives[index].method == GuideSurfaceMethod::BoundaryFill && alternatives[index].feasible) {
            fill = static_cast<int>(index);
        }
    }
    if (!Explain("境界面の候補を押せる", fill >= 0 && window.SurfaceDock().ClickCandidate(fill))
        || !Explain("外周 4 本 + 通る線 1 本の境界面になる",
            in.method == GuideSurfaceMethod::BoundaryFill && in.boundaries.size() == 4
                && in.guides.size() == 1 && !in.autoRoles)
        || !Explain("おまかせに戻せる", window.SurfaceDock().ClickResumeAuto())
        || !Explain("ガイド付きロフトに戻る", in.autoRoles && in.method == GuideSurfaceMethod::LoftSections
                && in.guides.size() == 2)) {
        return false;
    }
    if (!Explain("Enter を窓が受け取る", window.HandleToolKey(Qt::Key_Return, nullptr))) {
        return false;
    }
    return Explain(("面が1枚できる(帯は " + window.StatusText().toStdString() + ")").c_str(),
        CountOfKind(window, EntityKind::GuideSurface) == 1);
}

} // namespace

std::vector<SelfTestCase> BoundaryFillCases()
{
    return {
        {"HP-SF-13 おまかせ: 線を押すだけで役割と作り方が決まり、理由と候補が出て、役割を直せる",
            CaseBeginnerRoleAssist},
        {"HP-SF-10 境界面は外周と内側の線をまとめて選んでも外周を輪にし内側を通る線にする",
            CaseBoundaryFillTakesInnerLineAsPassThrough},
        {"HP-SF-11 ロフトは断面3本とガイド3本を普通のクリックで受けて全部を使う",
            CaseLoftTakesEveryRail},
        {"HP-SF-12 四辺面を4辺から作り張り方を選べる", CaseFourEdgePatchFromFourSides},
    };
}

} // namespace kachakacha::v2::selftest
