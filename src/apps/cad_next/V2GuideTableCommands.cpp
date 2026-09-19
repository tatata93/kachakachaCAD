//! 形状ガイドの役割表を組み立てるコマンド(V2MainWindow の一部)。
//!
//! 表(役割・番号・向き・元ワイヤー)は core(modeling/GuideSurfaceTable.h)が持つ。
//! これまで表は「見るだけ」で、面はいつも選んだ順の断面から自動で作っていた。
//! それでは平面・案内付きロフト・曲線網・境界埋め・離した面が作れない。
//! ここは、表を人が組み立てる道を core の操作1つずつに対応させる。
//! 何が入っていないか、何が使えないかの判断は core が返す理由をそのまま出す。

#include "V2ChoiceDialog.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <QColor>
#include <QDialog>
#include <QListWidget>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <string>
#include <vector>

using kachakacha::v2::app::GuideSelectionOf;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::modeling::GuideTable;

namespace {

[[nodiscard]] QString Text(std::string_view value)
{
    return QString::fromUtf8(std::string(value).c_str());
}

} // namespace

void V2MainWindow::RunGuideTableCommand(std::string_view id)
{
    // 右は「いまの道具の1枚だけ」になった(指示書 C-09、I-03)。
    // 役割表は部品モードの2枚目から外したので、表を自分で使う人のために
    // その操作のたびに棚を前へ出す。出さないと、表を触っても右に何も見えない。
    ShowShelf(kachakacha::v2::app::Shelf::GuideTable);
    if (id == "guide.set_method") {
        SetGuideMethod();
    } else if (id == "guide.add_row") {
        AddSelectionToGuideTable();
    } else if (id == "guide.append_row") {
        AppendSelectionToGuideRow();
    } else if (id == "guide.row_up") {
        MoveGuideRow(-1);
    } else if (id == "guide.row_down") {
        MoveGuideRow(+1);
    } else if (id == "guide.row_remove") {
        RemoveGuideRow();
    } else if (id == "guide.row_reverse") {
        ReverseGuideRow();
    } else if (id == "guide.build") {
        BuildGuideSurfaceFromTable();
    } else if (id == "guide.clear") {
        ClearGuideTable();
    }
}

void V2MainWindow::SetGuideChoiceChooser(
    std::function<std::optional<int>(const QString& title, const QStringList& items,
        int initial)>
        chooser)
{
    guideChoiceChooser_ = std::move(chooser);
}

std::optional<std::size_t> V2MainWindow::CurrentGuideRow() const
{
    if (guideTableView_ == nullptr || guideTableView_->currentItem() == nullptr) {
        return std::nullopt;
    }
    const int row = guideTableView_->indexOfTopLevelItem(guideTableView_->currentItem());
    if (row < 0 || row >= static_cast<int>(guideTable_.rows.size())) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(row);
}

void V2MainWindow::SelectGuideRow(int row)
{
    if (guideTableView_ == nullptr || row < 0 || row >= guideTableView_->topLevelItemCount()) {
        return;
    }
    guideTableView_->setCurrentItem(guideTableView_->topLevelItem(row));
    RefreshCommandVisibility();
}

namespace {

//! 候補から1つ選ぶ。試験では窓の代わりが答える。
[[nodiscard]] std::optional<int> Choose(V2MainWindow* window,
    const std::function<std::optional<int>(const QString&, const QStringList&, int)>& chooser,
    const QString& title, const QString& label, const QStringList& items, int initial)
{
    if (chooser) {
        return chooser(title, items, initial);
    }
    V2ChoiceDialog dialog(title, label, items, initial, window);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return dialog.ChosenIndex();
}

} // namespace

void V2MainWindow::SetGuideMethod()
{
    const auto& methods = kachakacha::v2::app::GuideSurfaceMethods();
    QStringList items;
    int initial = 0;
    for (std::size_t index = 0; index < methods.size(); ++index) {
        items << Text(kachakacha::v2::app::GuideSurfaceMethodLabelJa(methods[index]));
        if (methods[index] == guideTable_.method) {
            initial = static_cast<int>(index);
        }
    }
    const auto chosen = Choose(this, guideChoiceChooser_, QStringLiteral("面の作り方"),
        QStringLiteral("作り方"), items, initial);
    if (!chosen.has_value() || *chosen < 0 || *chosen >= static_cast<int>(methods.size())) {
        SetStatus(QStringLiteral("面の作り方: やめました。"));
        return;
    }
    // 使わない役割の行が残っていれば core が断る。消して黙らない。
    if (SetGuideTable(kachakacha::v2::modeling::SetGuideTableMethod(guideTable_,
            methods[static_cast<std::size_t>(*chosen)]))) {
        SetStatus(QStringLiteral("面の作り方: %1。使う役割: %2")
                .arg(items.at(*chosen), RolesLabelJa(guideTable_.method)));
    }
}

QString V2MainWindow::RolesLabelJa(kachakacha::v2::modeling::GuideSurfaceMethod method)
{
    QString label;
    for (const ChainRole role : kachakacha::v2::modeling::RolesForMethod(method)) {
        if (!label.isEmpty()) {
            label += QStringLiteral("・");
        }
        label += QString::fromStdString(kachakacha::v2::modeling::ChainRoleLabelJa(role));
    }
    return label;
}

void V2MainWindow::AddSelectionToGuideTable()
{
    using kachakacha::v2::domain::EntityKind;
    const auto& roles = kachakacha::v2::modeling::RolesForMethod(guideTable_.method);
    // 離した面は、線ではなく既にある形状ガイドを指す。
    if (guideTable_.method == GuideSurfaceMethod::OffsetGuide) {
        for (const auto& id : viewport_->Selection().entityIds) {
            const auto* entity = session_->GetDocument().FindEntity(id);
            if (entity != nullptr && entity->kind == EntityKind::GuideSurface) {
                if (SetGuideTable(kachakacha::v2::modeling::AddSourceSurfaceRow(guideTable_,
                        id, entity->displayName))) {
                    SetStatus(QStringLiteral("元の面: %1 を表へ入れました。")
                            .arg(QString::fromStdString(entity->displayName)));
                }
                return;
            }
        }
        SetStatus(QStringLiteral("元の面: 形状ガイドの面を1つ選んでください。"));
        return;
    }
    // 役割が1つしか無い作り方では聞かない。聞くのは、選び分けが要るときだけ。
    int roleIndex = 0;
    if (roles.size() > 1) {
        QStringList items;
        for (const ChainRole role : roles) {
            items << QString::fromStdString(kachakacha::v2::modeling::ChainRoleLabelJa(role));
        }
        const auto chosen = Choose(this, guideChoiceChooser_, QStringLiteral("行の役割"),
            QStringLiteral("役割"), items, 0);
        if (!chosen.has_value() || *chosen < 0 || *chosen >= static_cast<int>(roles.size())) {
            SetStatus(QStringLiteral("行の役割: やめました。"));
            return;
        }
        roleIndex = *chosen;
    }
    const ChainRole role = roles[static_cast<std::size_t>(roleIndex)];
    int added = 0;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto chosen = GuideSelectionOf(session_->GetDocument(), session_->Scene(), id);
        if (!chosen.has_value()) {
            continue;
        }
        if (!SetGuideTable(kachakacha::v2::modeling::AddSelectionAsNewRow(guideTable_, role,
                *chosen))) {
            return;
        }
        ++added;
    }
    if (added == 0) {
        SetStatus(QStringLiteral("表へ: 線を1つ以上選んでください。"));
        return;
    }
    SetStatus(QStringLiteral("表へ: %1 の行を %2 つ足しました(全 %3 行)。")
            .arg(QString::fromStdString(kachakacha::v2::modeling::ChainRoleLabelJa(role)))
            .arg(added)
            .arg(static_cast<int>(guideTable_.rows.size())));
}

void V2MainWindow::AppendSelectionToGuideRow()
{
    const auto row = CurrentGuideRow();
    if (!row.has_value()) {
        SetStatus(QStringLiteral("既存行へ追加: 先に表の行を1つ選んでください。"));
        return;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    int added = 0;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto chosen = GuideSelectionOf(session_->GetDocument(), session_->Scene(), id);
        if (!chosen.has_value()) {
            continue;
        }
        // 端につながらなければ逆向きも試す。それでもだめなら core の理由を出す。
        if (!SetGuideTable(kachakacha::v2::app::AppendSelectionToRow(guideTable_, *row,
                *chosen, tolerance))) {
            return;
        }
        ++added;
    }
    if (added == 0) {
        SetStatus(QStringLiteral("既存行へ追加: 線を1つ以上選んでください。"));
        return;
    }
    SelectGuideRow(static_cast<int>(*row));
    SetStatus(QStringLiteral("既存行へ追加: %1行目に %2 本足しました(いま %3 本)。")
            .arg(static_cast<int>(*row) + 1)
            .arg(added)
            .arg(static_cast<int>(guideTable_.rows[*row].segments.size())));
}

void V2MainWindow::MoveGuideRow(int delta)
{
    const auto row = CurrentGuideRow();
    if (!row.has_value()) {
        SetStatus(QStringLiteral("行を動かす: 先に表の行を1つ選んでください。"));
        return;
    }
    if (SetGuideTable(kachakacha::v2::modeling::MoveRow(guideTable_, *row, delta))) {
        const int moved = static_cast<int>(*row) + delta;
        SelectGuideRow(moved);
        SetStatus(QStringLiteral("行を動かす: %1行目を%2行目へ。")
                .arg(static_cast<int>(*row) + 1)
                .arg(moved + 1));
    }
}

void V2MainWindow::RemoveGuideRow()
{
    const auto row = CurrentGuideRow();
    if (!row.has_value()) {
        SetStatus(QStringLiteral("行を削除: 先に表の行を1つ選んでください。"));
        return;
    }
    if (SetGuideTable(kachakacha::v2::modeling::RemoveRow(guideTable_, *row))) {
        SetStatus(QStringLiteral("行を削除: %1行目を消しました(残り %2 行)。")
                .arg(static_cast<int>(*row) + 1)
                .arg(static_cast<int>(guideTable_.rows.size())));
    }
}

void V2MainWindow::ReverseGuideRow()
{
    const auto row = CurrentGuideRow();
    if (!row.has_value()) {
        SetStatus(QStringLiteral("向きを反転: 先に表の行を1つ選んでください。"));
        return;
    }
    if (SetGuideTable(kachakacha::v2::modeling::ReverseRow(guideTable_, *row))) {
        SelectGuideRow(static_cast<int>(*row));
        SetStatus(QStringLiteral("向きを反転: %1行目を%2にしました。")
                .arg(static_cast<int>(*row) + 1)
                .arg(guideTable_.rows[*row].reversed ? QStringLiteral("逆")
                                                      : QStringLiteral("正")));
    }
}

void V2MainWindow::BuildGuideSurfaceFromTable()
{
    if (guideTable_.rows.empty()) {
        SetStatus(QStringLiteral("表から面を作る: 表が空です。先に行を入れてください。"));
        return;
    }
    // 離した面の距離は、右の「距離」の欄から取る。0 は core が断る。
    GuideTable table = guideTable_;
    if (table.method == GuideSurfaceMethod::OffsetGuide) {
        table.offsetDistanceMm = ExtrudeDistanceMm();
    }
    const auto built = BuildSurfaceFromTable(table, true);
    if (!built.has_value()) {
        return;
    }
    (void)AdoptGuideSurface(table, *built, kachakacha::v2::app::GuideTableInputIds(table),
        "形状ガイド");
}

void V2MainWindow::ClearGuideTable()
{
    const GuideSurfaceMethod method = guideTable_.method;
    guideTable_ = GuideTable{};
    guideTable_.method = method;
    RefreshGuideTable();
    RefreshCommandVisibility();
    SetStatus(QStringLiteral("表を空にしました。作り方は %1 のままです。")
            .arg(Text(kachakacha::v2::app::GuideSurfaceMethodLabelJa(method))));
}

//! 役割表の見た目を書き直す(V2MainWindow.cpp から移した。
//! あちらが 1500 行の上限に当たったため。中身は変えていない)。
void V2MainWindow::RefreshGuideTable()
{
    if (guideTableView_ == nullptr) {
        return;
    }
    guideTableView_->clear();
    const auto views = kachakacha::v2::modeling::BuildGuideTableView(guideTable_,
        session_->GetDocument().Snapshot().settings.tolerance);
    for (const auto& view : views) {
        auto* item = new QTreeWidgetItem(guideTableView_);
        item->setText(0, QString::fromStdString(view.roleLabelJa));
        item->setText(1, QString::number(view.number));
        item->setText(2, QString::number(static_cast<int>(view.segmentCount)));
        item->setText(3, QString::fromStdString(view.connectionLabelJa));
        item->setText(4, QString::fromStdString(view.directionLabelJa));
        item->setText(5, QString::fromStdString(view.sourceLabelJa));
        // 色は core の式が決める。画面で作らないので、3Dと必ず同じ色になる。
        const QColor color(view.color.red, view.color.green, view.color.blue);
        item->setForeground(0, color);
        item->setData(0, Qt::UserRole, color);
    }
    for (int column = 0; column < guideTableView_->columnCount(); ++column) {
        guideTableView_->resizeColumnToContents(column);
    }
    // 3Dへ同じ色で出す。色は core の式が決めるので、表と3Dがずれようがない。
    viewport_->SetGuideTableRows(views);
    // 足りない役割の案内は、そのつど出し直す。前の案内を残すと、
    // 入れ終わったあとも「入っていません」が並んだままになる。
    ClearGuideGuidance();
    // **まだ何も入れていない表について「入っていません」と言わない。**
    // 「面を作る」が入口になったので、役割表は自分で開いた人だけが使う。
    // 空のままの表の不足を知らせへ並べると、何もしていないのに
    // 赤い行が出たままになり、本当の失敗が埋もれる(01〜03 の絵で見えた)。
    if (!guideTable_.rows.empty()) {
        for (const std::string& line : kachakacha::v2::modeling::MissingRoleGuidanceJa(
                 guideTable_)) {
            AddGuideGuidance(
                QStringLiteral("UI-R009 %1").arg(QString::fromStdString(line)));
        }
    }
}

void V2MainWindow::ClearGuideGuidance()
{
    if (diagnosticList_ == nullptr) {
        return;
    }
    for (int row = diagnosticList_->count() - 1; row >= 0; --row) {
        if (diagnosticList_->item(row)->text().startsWith(QStringLiteral("UI-R009"))) {
            delete diagnosticList_->takeItem(row);
        }
    }
}

void V2MainWindow::AddGuideGuidance(const QString& text)
{
    if (diagnosticList_ == nullptr) {
        return;
    }
    diagnosticList_->addItem(text);
}
