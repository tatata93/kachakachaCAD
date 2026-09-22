//! Model Explorer(左の一覧)を文書から作り直す(正本 3 HTML 2026-09-18、指示書 model_explorer)。
//!
//!   文書
//!   ├ 原点 … 常に最上段。消せず、名前も変えられず、グループへも入らない
//!   ├ 作業面 / グループ(入れ子) / ワイヤー / 面 / 立体 / 近似 / 生成物
//!
//! どのものがどの節に入るかは core(app/ExplorerModel)が決める。
//! 1つずつ ◉ で出し隠しできる(entity の visibility)。グループの ◉ はグループごと。
//! 近似モデルの下には 候補 / 部材 N / 開口・折り線・切れ目 / 生成物 を出す(近似の結果の単位で見る)。

#include "V2MainWindow.h"

#include "V2EntityTree.h"
#include "V2TreeIcons.h"

#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/domain/Entity.h"

#include <QFont>
#include <QLabel>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>

using kachakacha::v2::app::ExplorerSection;
using kachakacha::v2::domain::EntityKind;

namespace {

void MarkIfActiveWorkPlane(QTreeWidgetItem* item, bool active)
{
    if (!active) {
        return;
    }
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    item->setToolTip(0, QStringLiteral("現在の作図面"));
}

//! 節の見出し。名前は変えられず、引きずれず、落とし先にはなる(グループから外すため)。
[[nodiscard]] QTreeWidgetItem* MakeSectionItem(QTreeWidgetItem* parent, ExplorerSection section)
{
    auto* item = new QTreeWidgetItem(parent);
    const QString name = QString::fromUtf8(
        std::string(kachakacha::v2::app::ExplorerSectionNameJa(section)).c_str());
    item->setText(0, name);
    item->setText(1, QStringLiteral("節"));
    item->setIcon(0, section == ExplorerSection::Origin ? V2OriginTreeIcon() : V2GroupTreeIcon());
    item->setFlags((item->flags() | Qt::ItemIsDropEnabled) & ~Qt::ItemIsEditable
        & ~Qt::ItemIsDragEnabled & ~Qt::ItemIsSelectable);
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    return item;
}

} // namespace

//! 文書の行の名前。開いた文書ならその名前、まだなら「文書」。
QString V2MainWindow::DocumentDisplayName() const
{
    const std::string path = documentPath_.toStdString();
    if (path.empty()) {
        return QStringLiteral("文書");
    }
    const std::size_t cut = path.find_last_of("/\\");
    return QString::fromStdString(cut == std::string::npos ? path : path.substr(cut + 1));
}

void V2MainWindow::RefreshEntityList()
{
    if (!entityTree_) {
        return;
    }
    // 書き換えの便りを止めてから作り直す。止めないと、作り直しの途中で
    // 「名前が変わった」と誤って伝わり、名前が入れ替わる。
    const bool blocked = entityTree_->blockSignals(true);
    entityTree_->clear();
    entityItems_.clear();
    axisItems_.fill(nullptr);
    const auto& snapshot = session_->GetDocument().Snapshot();
    // 文書の行。正本の Project に当たる。
    auto* root = new QTreeWidgetItem(entityTree_);
    root->setText(0, DocumentDisplayName());
    root->setText(1, QStringLiteral("文書"));
    root->setFlags((root->flags() | Qt::ItemIsDropEnabled) & ~Qt::ItemIsEditable
        & ~Qt::ItemIsDragEnabled & ~Qt::ItemIsSelectable);
    std::map<ExplorerSection, QTreeWidgetItem*> sections;
    for (const ExplorerSection section : kachakacha::v2::app::ExplorerSections()) {
        sections[section] = MakeSectionItem(root, section);
    }
    BuildOriginRows(sections[ExplorerSection::Origin]);
    // グループは入れ子のまま「グループ」の節の下に(オーナー指示 §9・§10)。
    std::map<std::string, QTreeWidgetItem*> byGroupId;
    BuildGroupItems(byGroupId, sections[ExplorerSection::Groups]);
    for (const auto& entity : snapshot.entities) {
        const ExplorerSection section = kachakacha::v2::app::SectionForEntity(snapshot, entity);
        if (section == ExplorerSection::Origin) {
            continue;   // 原点の行は BuildOriginRows が出した
        }
        if (section == ExplorerSection::Approximation
            && kachakacha::v2::app::GeneratingModelOf(snapshot, entity).has_value()) {
            continue;   // 生成物は近似モデルの行の下(AddGeneratedRows)に出る
        }
        QTreeWidgetItem* parent = sections[section];
        if (section == ExplorerSection::Groups && entity.groupId.has_value()) {
            const auto found = byGroupId.find(entity.groupId->ToString());
            if (found != byGroupId.end() && found->second != nullptr) {
                parent = found->second;
            }
        }
        AddEntityRow(parent, entity);
    }
    entityTree_->expandAll();
    for (int column = 0; column < entityTree_->columnCount(); ++column) {
        entityTree_->resizeColumnToContents(column);
    }
    if (groupLabel_ != nullptr) {
        groupLabel_->setText(ActiveGroupText());
    }
    RefreshActiveGroupCombo();
    entityTree_->blockSignals(blocked);
    RefreshWorkPlaneViews();
    // 作り直したら絞り込みをかけ直す。かけ直さないと、線を1本引いただけで
    // 絞り込みが外れたように見える。
    ApplyEntityTreeFilter();
}

//! 原点: 基準平面 3 枚(top_XY / front_XZ / side_YZ)と 3 軸。V1 と同じく最上段に固定。
void V2MainWindow::BuildOriginRows(QTreeWidgetItem* originRoot)
{
    const auto& snapshot = session_->GetDocument().Snapshot();
    originRoot->setToolTip(0, QStringLiteral(
        "初期の基準平面(top_XY / front_XZ / side_YZ)と軸。削除やグループへの移動はできません"));
    for (const auto& entity : snapshot.entities) {
        if (!kachakacha::v2::app::IsOriginPlane(snapshot, entity.id)) {
            continue;
        }
        QTreeWidgetItem* item = AddEntityRow(originRoot, entity);
        // 種類は「作業平面」のまま(絞り込みの字を変えない)。基準であることは説明で。
        // 長い種類名は名前の列を潰す(PC 画面 2026-09-19 で名前が消えた)。
        item->setText(1, QStringLiteral("作業平面"));
        item->setToolTip(0, QStringLiteral("原点の基準平面。消せず、グループへも入りません。"));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsDragEnabled);
    }
    const char* axisNames[3] = {"X軸", "Y軸", "Z軸"};
    for (int axis = 0; axis < 3; ++axis) {
        auto* item = new QTreeWidgetItem(originRoot);
        item->setText(0, QString::fromUtf8(axisNames[axis]));
        item->setText(1, QStringLiteral("軸"));
        item->setIcon(0, V2AxisTreeIcon(axis));
        item->setFlags((item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable
            & ~Qt::ItemIsDragEnabled);
        item->setCheckState(0, viewport_ != nullptr && viewport_->AxisVisible(axis)
                ? Qt::Checked
                : Qt::Unchecked);
        axisItems_[static_cast<std::size_t>(axis)] = item;
    }
}

//! ものの行。名前は F2 で変えられ、◉(チェック)で 1 つずつ出し隠しできる。
QTreeWidgetItem* V2MainWindow::AddEntityRow(QTreeWidgetItem* parent,
    const kachakacha::v2::domain::Entity& entity)
{
    auto* item = new QTreeWidgetItem(parent);
    const QString name = entity.displayName.empty() ? QStringLiteral("(名前なし)")
                                                    : QString::fromUtf8(entity.displayName.c_str());
    item->setText(0, name);
    item->setText(1, QString::fromUtf8(
        std::string(kachakacha::v2::app::ExplorerKindNameJa(entity.kind)).c_str()));
    item->setIcon(0, V2EntityTreeIcon(entity.kind));
    item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    item->setCheckState(0, entity.visibility == kachakacha::v2::domain::Visibility::Visible
            ? Qt::Checked
            : Qt::Unchecked);
    MarkIfActiveWorkPlane(item, entity.kind == EntityKind::WorkPlane
        && entity.id == activeWorkPlaneId_);
    entityItems_.emplace_back(item, entity.id);
    if (entity.kind == EntityKind::FabricationModel) {
        AddApproximationRows(item, entity);
        AddGeneratedRows(item, entity);
    }
    return item;
}

//! 近似モデルの下の「生成物」: この近似モデルから「生成」(固定・展開)で作ったもの(F-15)。
//! 行はふつうのものの行(選べる・◉ で出し隠し・名前を変えられる・グループへ引きずれる)。
//! 近似の計算が失敗していても出す(生成物は独立したもの)。グループに入れたものは
//! グループの節に出る(ほかの物と同じ決まり、core の SectionForEntity)。
void V2MainWindow::AddGeneratedRows(QTreeWidgetItem* modelItem,
    const kachakacha::v2::domain::Entity& model)
{
    const auto& snapshot = session_->GetDocument().Snapshot();
    std::vector<const kachakacha::v2::domain::Entity*> generated;
    for (const auto& entity : snapshot.entities) {
        if (kachakacha::v2::app::SectionForEntity(snapshot, entity) == ExplorerSection::Approximation
            && kachakacha::v2::app::GeneratingModelOf(snapshot, entity) == model.id) {
            generated.push_back(&entity);
        }
    }
    if (generated.empty()) {
        return;
    }
    auto* node = new QTreeWidgetItem(modelItem);
    node->setText(0, QStringLiteral("生成物"));
    node->setText(1, QStringLiteral("%1 個").arg(static_cast<int>(generated.size())));
    node->setToolTip(0, QStringLiteral("この近似モデルから「生成」で作ったもの。"
                                       "近似モデルを消しても残ります(固定したものは独立)。"));
    node->setFlags(node->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsDragEnabled);
    for (const auto* entity : generated) {
        AddEntityRow(node, *entity);
    }
}

//! 近似モデルの下: 候補(方式)と 部材 N。近似の結果の単位で読めるようにする。
//! 行は結果の写しであって、ものではない(選んでも 3D では近似モデルが選ばれる)。
void V2MainWindow::AddApproximationRows(QTreeWidgetItem* modelItem,
    const kachakacha::v2::domain::Entity& entity)
{
    const auto found = fabricationModels_.find(entity.id.ToString());
    if (found == fabricationModels_.end()) {
        return;
    }
    const auto& evaluation = found->second;
    auto* candidate = new QTreeWidgetItem(modelItem);
    candidate->setText(0, evaluation.bandMesh.has_value() ? QStringLiteral("候補: 帯で近似")
                                                            : QStringLiteral("候補: 面ごとに展開"));
    candidate->setText(1, QStringLiteral("候補"));
    candidate->setFlags(candidate->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsDragEnabled);
    auto* parts = new QTreeWidgetItem(modelItem);
    parts->setText(0, QStringLiteral("部材"));
    parts->setText(1, QStringLiteral("%1 枚").arg(static_cast<int>(evaluation.panels.size())));
    parts->setFlags(parts->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsDragEnabled);
    for (std::size_t index = 0; index < evaluation.panels.size(); ++index) {
        auto* part = new QTreeWidgetItem(parts);
        part->setText(0, QStringLiteral("部材 %1").arg(static_cast<int>(index + 1)));
        part->setText(1, QStringLiteral("近似部品"));
        part->setFlags(part->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsDragEnabled);
    }
    // 役割の線(開口・折り線・切れ目)。どの線がこの近似モデルの何になっているかを、
    // 近似の単位で読めるようにする(F-15)。線そのものはワイヤーの節にある(ここは写し)。
    const auto& snapshot = session_->GetDocument().Snapshot();
    const auto* feature = session_->GetDocument().FindFeature(entity.createdBy);
    const auto* definition = feature == nullptr
        ? nullptr
        : std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(&feature->definition);
    if (definition == nullptr) {
        return;
    }
    const std::pair<const std::vector<kachakacha::v2::base::EntityId>*, QString> roles[] = {
        {&definition->openingWires, QStringLiteral("開口")},
        {&definition->foldWires, QStringLiteral("折り線")},
        {&definition->reliefCutWires, QStringLiteral("切れ目")}};
    for (const auto& [wires, role] : roles) {
        if (wires->empty()) {
            continue;
        }
        auto* group = new QTreeWidgetItem(modelItem);
        group->setText(0, role);
        group->setText(1, QStringLiteral("%1 本").arg(static_cast<int>(wires->size())));
        group->setFlags(group->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsDragEnabled);
        for (const auto& id : *wires) {
            const auto* wire = FindEntityByIdText(snapshot, id.ToString());
            auto* row = new QTreeWidgetItem(group);
            row->setText(0, wire == nullptr || wire->displayName.empty()
                    ? QStringLiteral("(見つからない線)")
                    : QString::fromUtf8(wire->displayName.c_str()));
            row->setText(1, role + QStringLiteral("の線"));
            row->setFlags(row->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsDragEnabled);
        }
    }
}

//! グループの行を、入れ子のまま「グループ」の節の下に作る。
//!
//! 親から順に作る。親がまだ無ければその場で親を作りにいく。
//! 先に場所を取ってから親を作るのは、壊れた文書に輪があっても止まるためである。
void V2MainWindow::BuildGroupItems(std::map<std::string, QTreeWidgetItem*>& byGroupId,
    QTreeWidgetItem* groupsRoot)
{
    const auto& snapshot = session_->GetDocument().Snapshot();
    groupItems_.clear();
    std::function<QTreeWidgetItem*(const kachakacha::v2::base::GroupId&)> make;
    make = [&](const kachakacha::v2::base::GroupId& id) -> QTreeWidgetItem* {
        const std::string key = id.ToString();
        if (const auto found = byGroupId.find(key); found != byGroupId.end()) {
            return found->second;
        }
        const kachakacha::v2::document::Group* group = nullptr;
        for (const auto& candidate : snapshot.groups) {
            if (candidate.id == id) {
                group = &candidate;
                break;
            }
        }
        if (group == nullptr) {
            return nullptr;
        }
        byGroupId.emplace(key, nullptr);
        QTreeWidgetItem* parent = group->parentId.has_value() ? make(*group->parentId)
                                                              : nullptr;
        auto* made = new QTreeWidgetItem(parent != nullptr ? parent : groupsRoot);
        const bool active = snapshot.settings.activeGroupId.has_value()
            && *snapshot.settings.activeGroupId == id;
        made->setText(0, QString::fromStdString(
            group->displayName + (active ? " ←作業中" : "")));
        made->setText(1, QStringLiteral("グループ"));
        made->setIcon(0, V2GroupTreeIcon());
        if (active) {
            QFont font = made->font(0);
            font.setBold(true);
            made->setFont(0, font);
        }
        // グループは名前を変えられる。引きずって別のグループへ移せる。
        made->setFlags(made->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled
            | Qt::ItemIsDropEnabled | Qt::ItemIsUserCheckable);
        made->setCheckState(0, group->visible ? Qt::Checked : Qt::Unchecked);
        byGroupId[key] = made;
        groupItems_.emplace_back(made, id);
        return made;
    };
    // グループが空でも木に出す。出さないと、作った直後に何も見えない。
    for (const auto& group : snapshot.groups) {
        (void)make(group.id);
    }
}

//! ものの行の ◉ が変わった。1つずつの出し隠し(文書の visibility)。変わっていなければ偽。
bool V2MainWindow::ToggleEntityVisibilityFromItem(QTreeWidgetItem* item)
{
    const auto* entityId = EntityForItem(item);
    if (item == nullptr || entityId == nullptr) {
        return false;
    }
    const auto* entity = session_->GetDocument().FindEntity(*entityId);
    if (entity == nullptr) {
        return false;
    }
    const bool wantVisible = item->checkState(0) == Qt::Checked;
    const bool isVisible = entity->visibility == kachakacha::v2::domain::Visibility::Visible;
    if (wantVisible == isVisible) {
        return false;
    }
    // 名前と id は文書を変える前に写す。AdoptCurrentDocument が木を作り直すので、
    // そのあとの item と entityId は消えた行を指す(配布版の自己試験が HP-XP-02 で落ちた
    // 2026-09-19 の原因)。
    const QString name = item->text(0);
    const kachakacha::v2::base::EntityId id = *entityId;
    const auto done = session_->GetDocument().Run(kachakacha::v2::document::SetVisibilityCommand(
        {id}, wantVisible ? kachakacha::v2::domain::Visibility::Visible
                          : kachakacha::v2::domain::Visibility::Hidden));
    if (!done.committed) {
        ReportDiagnostics(done.diagnostics);
    }
    AdoptCurrentDocument();
    SetStatus(wantVisible ? QStringLiteral("「%1」を出しました。").arg(name)
                          : QStringLiteral("「%1」を隠しました。").arg(name));
    return true;
}

// ---- 試験と場面づくりから読む ----

//! 一覧の「節とグループ」の行(試験から読む)。文書の行の下の節(原点が 0)に、
//! グループの行(入れ子を平らに)を続ける。
std::vector<QTreeWidgetItem*> V2MainWindow::ExplorerRows() const
{
    std::vector<QTreeWidgetItem*> rows;
    if (entityTree_ == nullptr || entityTree_->topLevelItemCount() == 0) {
        return rows;
    }
    QTreeWidgetItem* root = entityTree_->topLevelItem(0);
    for (int index = 0; index < root->childCount(); ++index) {
        rows.push_back(root->child(index));
    }
    for (const auto& entry : groupItems_) {
        rows.push_back(entry.first);
    }
    return rows;
}

int V2MainWindow::GroupRowCount() const
{
    return static_cast<int>(ExplorerRows().size());
}

QString V2MainWindow::GroupRowText(int row) const
{
    const auto rows = ExplorerRows();
    if (row < 0 || row >= static_cast<int>(rows.size())) {
        return QString();
    }
    return rows[static_cast<std::size_t>(row)]->text(0);
}

int V2MainWindow::OriginChildCount() const
{
    const auto rows = ExplorerRows();
    return rows.empty() ? 0 : rows.front()->childCount();
}

QString V2MainWindow::OriginChildText(int row) const
{
    if (row < 0 || row >= OriginChildCount()) {
        return QString();
    }
    return ExplorerRows().front()->child(row)->text(0);
}

