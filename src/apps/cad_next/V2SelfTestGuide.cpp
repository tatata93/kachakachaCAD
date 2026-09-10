//! 形状ガイドの役割表を人が組み立てる道の自己試験(AT-GEO-008)。
//!
//! 表は見るだけだったので、平面・案内付きロフト・曲線網・境界埋め・離した面は
//! 画面から作れなかった。ここでは、線を役割付きで表へ入れ、既存行へ足し、向きを直し、
//! 表から面を作り、保存して開き直すと表と面が戻ることを確かめる。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <QPointF>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideSurfaceMethod;

//! 上から見て、100mm 四方の枠を 4 本の線で描く。
//! 下辺は左→右、右辺は下→上、上辺は右→左、左辺は上→下(一筆書きの向き)。
//! ただし上辺だけはわざと左→右に引いて、向きを直す道を通す。
[[nodiscard]] std::vector<kachakacha::v2::base::EntityId> DrawFrame(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(300.0);
    const auto at = [&viewport](double x, double y) {
        return QPointF(viewport.width() * x, viewport.height() * y);
    };
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.ClickAt(at(0.3, 0.7));   // 下辺 左→右
    viewport.ClickAt(at(0.7, 0.7));
    viewport.ClickAt(at(0.7, 0.7));   // 右辺 下→上
    viewport.ClickAt(at(0.7, 0.3));
    viewport.ClickAt(at(0.3, 0.3));   // 上辺 左→右(逆向き)
    viewport.ClickAt(at(0.7, 0.3));
    viewport.ClickAt(at(0.3, 0.3));   // 左辺 上→下
    viewport.ClickAt(at(0.3, 0.7));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return kachakacha::v2::app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire)
        .entityIds;
}

void SelectOne(V2MainWindow& window, const kachakacha::v2::base::EntityId& id)
{
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(id);
    window.Viewport().SetSelection(one);
}

[[nodiscard]] int GuideSurfaceCount(V2MainWindow& window)
{
    return static_cast<int>(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::GuideSurface)
                                .entityIds.size());
}

[[nodiscard]] bool CaseGuideTableBuildsPlanarAndSurvivesReopen(V2MainWindow& window)
{
    const auto wires = DrawFrame(window);
    if (!Explain((std::string("枠の線が4本ある(実際は ") + std::to_string(wires.size())
                     + ")").c_str(),
            wires.size() == 4)) {
        return false;
    }
    // 作り方を「平面」にする。窓の代わりに位置で答える。
    window.SetGuideChoiceChooser([](const QString& title, const QStringList& items, int) {
        for (int index = 0; index < items.size(); ++index) {
            if (title.contains(QStringLiteral("作り方"))
                && items.at(index).contains(QStringLiteral("平面"))) {
                return std::optional<int>(index);
            }
            if (title.contains(QStringLiteral("役割"))
                && items.at(index) == QStringLiteral("外形")) {
                return std::optional<int>(index);
            }
        }
        return std::optional<int>();
    });
    window.RunCommand("guide.set_method");
    if (!Explain((std::string("平面にできる(") + window.StatusText().toStdString() + ")")
                     .c_str(),
            window.GuideRoleTable().method == GuideSurfaceMethod::PlanarBoundary)) {
        return false;
    }
    // 下辺を外形の新しい行にし、右辺・上辺・左辺を同じ行へ足す。
    SelectOne(window, wires[0]);
    window.RunCommand("guide.add_row");
    if (!Explain((std::string("外形の行ができる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.GuideRowCount() == 1
                && window.GuideRoleTable().rows.front().role == ChainRole::OuterBoundary)) {
        return false;
    }
    window.SelectGuideRow(0);
    for (std::size_t index = 1; index < wires.size(); ++index) {
        SelectOne(window, wires[index]);
        window.RunCommand("guide.append_row");
    }
    if (!Explain((std::string("4本が1行につながる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.GuideRoleTable().rows.front().segments.size() == 4)) {
        return false;
    }
    if (!Explain("閉じていると出る",
            window.GuideRowText(0, 3).contains(QStringLiteral("閉じている")))) {
        return false;
    }
    // 外形が残っているうちは、断面だけの作り方へは変えられない。
    window.SetGuideChoiceChooser([](const QString&, const QStringList& items, int) {
        for (int index = 0; index < items.size(); ++index) {
            if (items.at(index).contains(QStringLiteral("ロフト"))) {
                return std::optional<int>(index);
            }
        }
        return std::optional<int>();
    });
    window.RunCommand("guide.set_method");
    if (!Explain("使わない役割の行が残っていると作り方を変えられない",
            window.GuideRoleTable().method == GuideSurfaceMethod::PlanarBoundary)) {
        return false;
    }
    // 向きを反転して、また戻す(反転した行を保存する道は次のケースで見る)。
    window.SelectGuideRow(0);
    window.RunCommand("guide.row_reverse");
    if (!Explain("逆になる", window.GuideRoleTable().rows.front().reversed)) {
        return false;
    }
    const int surfacesBefore = GuideSurfaceCount(window);
    window.RunCommand("guide.build");
    if (!Explain((std::string("表から面ができる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            GuideSurfaceCount(window) == surfacesBefore + 1)) {
        return false;
    }
    // 保存して開き直すと、表(役割・向き・作り方)と面が戻る。
    const std::string path = kachakacha::v2::io::FromPath(
        std::filesystem::temp_directory_path() / "kacha_selftest_guide_table.kcd2");
    std::error_code code;
    std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
    window.SetPathChooser([&path](bool) { return QString::fromStdString(path); });
    window.RunCommand("file.save_as");
    if (!Explain("保存できる", window.StatusText().contains(QStringLiteral("保存しました")))) {
        return false;
    }
    window.RunCommand("file.new");
    if (!Explain("新しい文書では表が空", window.GuideRowCount() == 0)) {
        return false;
    }
    const bool opened = window.OpenDocumentFile(QString::fromStdString(path));
    std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
    if (!Explain("開き直せる", opened)) {
        return false;
    }
    if (!Explain((std::string("面が作り直される(") + window.StatusText().toStdString()
                     + ")").c_str(),
            GuideSurfaceCount(window) == 1 && window.KernelShapeCount() >= 1)) {
        return false;
    }
    const auto& table = window.GuideRoleTable();
    return Explain("表が同じ形で戻る(平面・外形1行・4本・逆向き)",
        table.method == GuideSurfaceMethod::PlanarBoundary && table.rows.size() == 1
            && table.rows.front().role == ChainRole::OuterBoundary
            && table.rows.front().segments.size() == 4 && table.rows.front().reversed);
}

[[nodiscard]] bool CaseGuideRowCommandsNeedARow(V2MainWindow& window)
{
    // 行を選んでいないときは、行に効くコマンドは押せない(押せるのに何も起きない、を避ける)。
    const auto wires = DrawFrame(window);
    if (wires.size() != 4) {
        return false;
    }
    if (!Explain("表が空なら「表から面を作る」は押せない",
            !window.CommandEnabled("guide.build", nullptr))) {
        return false;
    }
    window.SetGuideChoiceChooser([](const QString&, const QStringList&, int) {
        return std::optional<int>(0);
    });
    SelectOne(window, wires[0]);
    window.RunCommand("guide.add_row");
    if (!Explain("行が入れば「表から面を作る」は押せる",
            window.CommandEnabled("guide.build", nullptr))) {
        return false;
    }
    if (!Explain("行を選ぶ前は「行を削除」は押せない",
            !window.CommandEnabled("guide.row_remove", nullptr))) {
        return false;
    }
    window.SelectGuideRow(0);
    if (!Explain("行を選べば「行を削除」が押せる", window.CommandEnabled("guide.row_remove", nullptr))) {
        return false;
    }
    window.RunCommand("guide.row_remove");
    if (!Explain("消える", window.GuideRowCount() == 0)) {
        return false;
    }
    window.RunCommand("guide.clear");
    return Explain("空の表は空のまま", window.GuideRowCount() == 0);
}

} // namespace

std::vector<SelfTestCase> GuideCases()
{
    return {
        {"役割表から平面を作り開き直しても戻る", &CaseGuideTableBuildsPlanarAndSurvivesReopen},
        {"表の行に効くコマンドは行を選んでから", &CaseGuideRowCommandsNeedARow},
    };
}

} // namespace kachakacha::v2::selftest
