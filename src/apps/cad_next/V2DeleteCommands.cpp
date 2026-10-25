#include <QString>
#include <QStringList>
// Selection deletion is prepared without touching the document, previewed, then committed atomically.
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2DrawingSelection.h"
#include "V2ToolSelection.h"
#include "V2EntityTree.h"
#include <QTreeWidgetItem>
#include "kachakacha/document/Commands.h"
#include <QDialog>
#include <QObject>
#include <QWidget>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

namespace {
using namespace kachakacha::v2;
class DeletePlan final : public document::DocumentCommand {
public:
    explicit DeletePlan(document::DocumentSnapshot after):after_(std::move(after)){}
    std::string Label() const override {return V2DrawingSelection::DeleteLabel;}
    std::vector<base::Diagnostic> Apply(document::DocumentSnapshot& current) const override {
        if(current.id!=after_.id||current.revision!=after_.revision)
            return {base::MakeError("DOC-C001","削除の確認中に文書が変わりました。選び直してください。")};
        current=after_;return {};
    }
private: document::DocumentSnapshot after_;
};
void ExpandFolderSelection(const document::DocumentSnapshot& before,const std::set<base::GroupId>& allFolders,app::SelectionSet& selection){
    // A tool may mirror only accepted input kinds into the viewport. Folder contents
    // come from the document, and always mean whole entities (not picked segments).
    for(const auto& entity:before.entities)if(entity.groupId&&allFolders.contains(*entity.groupId)){
        if(std::find(selection.entityIds.begin(),selection.entityIds.end(),entity.id)==selection.entityIds.end())selection.entityIds.push_back(entity.id);
        std::erase_if(selection.ordered,[&](const auto& ref){return ref.entityId==entity.id;});
        app::SelectionRef whole;whole.entityId=entity.id;selection.ordered.push_back(whole);
    }
}
int ConfirmDeletion(QWidget* parent,const QStringList& names,bool hasFolders){
        bool keepContents=false;QDialog dialog(parent);dialog.setObjectName(QStringLiteral("confirmDependentDelete"));dialog.setWindowTitle(QStringLiteral("使っているものも削除"));
        auto* layout=new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(!hasFolders?QStringLiteral("この要素を使っているものがあります。\n次の要素が消えます。削除しますか？\n3Dビューの赤色が削除対象です。"):
            QStringLiteral("グループと中の要素を削除しますか？\n中身を残してグループだけ解除することもできます。\n3Dビューの赤色が削除対象です。"),&dialog));
        auto* list=new QListWidget(&dialog);list->addItems(names);list->setMinimumSize(330,160);layout->addWidget(list);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,&dialog);
        auto* confirm=buttons->addButton(QStringLiteral("一覧の要素を削除"),QDialogButtonBox::AcceptRole);confirm->setObjectName(QStringLiteral("confirmDelete"));
        if(hasFolders){auto* keep=buttons->addButton(QStringLiteral("グループを解除（中身を残す）"),QDialogButtonBox::ActionRole);
            keep->setObjectName("keepGroupContents");QObject::connect(keep,&QPushButton::clicked,&dialog,[&]{keepContents=true;dialog.accept();});}
        buttons->button(QDialogButtonBox::Cancel)->setDefault(true);layout->addWidget(buttons);
        QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return -1;
    return keepContents?1:0;
}

}

void V2MainWindow::DeleteSelected()
{
    using namespace kachakacha::v2;
    auto selection=viewport_->Selection();
    std::set<base::GroupId> folders;
    for(const auto& [row,id]:groupItems_)if(row->isSelected())folders.insert(id);
    if(selection.entityIds.empty()&&folders.empty()){SetStatus(QStringLiteral("削除: 先に選んでください。"));return;}
    auto after=session_->GetDocument().Snapshot();const auto before=after;
    auto allFolders=folders;
    for(std::size_t pass=0;pass<before.groups.size();++pass)
        for(const auto& group:before.groups)if(group.parentId&&allFolders.contains(*group.parentId))allFolders.insert(group.id);
    ExpandFolderSelection(before,allFolders,selection);
    std::erase_if(after.groups,[&](const auto& group){return allFolders.contains(group.id);});
    if(after.settings.activeGroupId&&allFolders.contains(*after.settings.activeGroupId))after.settings.activeGroupId.reset();
    std::set<base::FeatureId> roots;
    std::set<base::SegmentId> cutIds;
    QStringList partialNames;bool dependencyCascade=false;
    const auto fail=[&](const QString& message){SetStatus(QStringLiteral("削除: %1").arg(message));};
    const auto remove=[&](base::FeatureId id){
        const auto count=after.features.size();
        const auto diagnostics=document::RemoveFeatureCommand(id,document::RemovePolicy::Cascade,"削除").Apply(after);
        if(count>after.features.size()+1)dependencyCascade=true;
        for(const auto& d:diagnostics)if(d.IsError()){fail(QString::fromStdString(d.summaryJa));return false;}
        return true;
    };
    for(const auto& id:selection.entityIds){
        const auto* original=session_->GetDocument().FindEntity(id);
        if(!original)continue;
        roots.insert(original->createdBy);
        auto entity=std::find_if(after.entities.begin(),after.entities.end(),[&](const auto& e){return e.id==id;});
        if(entity==after.entities.end())continue; // already included by another selected parent's cascade
        const auto owner=entity->createdBy;
        std::set<base::SegmentId> cuts;bool whole=false,bad=false;
        for(const auto& ref:selection.ordered)if(ref.entityId==id){
            if(ref.kind==app::SelectionElementKind::Object)whole=true;
            else if(ref.kind==app::SelectionElementKind::Edge&&ref.segmentId&&!ref.segmentId->IsNil())cuts.insert(*ref.segmentId);
            else bad=true;
        }
        if(entity->kind!=domain::EntityKind::Wire||whole||(cuts.empty()&&!bad)){if(!remove(owner))return;continue;}
        auto feature=std::find_if(after.features.begin(),after.features.end(),[&](const auto& f){return f.id==owner;});
        const auto* wire=feature!=after.features.end()?std::get_if<domain::CreateWireDefinition>(&feature->definition):nullptr;
        if(bad||!wire||entity->editPolicy==domain::EditPolicy::Derived||feature->outputs.size()!=1){
            fail(QStringLiteral("この部分要素は直接削除できません。ワイヤー全体を選ぶか、現在状態を固定してください。"));return;
        }
        auto kept=*wire;
        // Old in-memory wire definitions can lack persistent IDs. Resolve only against the current scene.
        if(kept.segmentIds.empty())for(const auto& curve:session_->Scene().curves)if(curve.entityId==id)kept.segmentIds.push_back(curve.segmentId);
        if(kept.segments.size()!=kept.segmentIds.size()
            ||std::set<base::SegmentId>(kept.segmentIds.begin(),kept.segmentIds.end()).size()!=kept.segmentIds.size()
            ||std::any_of(cuts.begin(),cuts.end(),[&](auto c){return std::find(kept.segmentIds.begin(),kept.segmentIds.end(),c)==kept.segmentIds.end();})){
            fail(QStringLiteral("線分の選択が古いため削除できません。選び直してください。"));return;
        }
        const auto inputs=feature->inputEntityIds;
        // Remove consumers first, while retaining the unselected source segments.
        std::vector<base::FeatureId> consumers;
        for(const auto& f:after.features)if(std::find(f.inputEntityIds.begin(),f.inputEntityIds.end(),id)!=f.inputEntityIds.end())consumers.push_back(f.id);
        if(!consumers.empty())dependencyCascade=true;
        for(auto consumer:consumers)if(std::any_of(after.features.begin(),after.features.end(),[&](const auto& f){return f.id==consumer;})&&!remove(consumer))return;
        const auto segments=kept.segments;const auto ids=kept.segmentIds;kept.segments.clear();kept.segmentIds.clear();
        for(std::size_t i=0;i<segments.size();++i)if(!cuts.contains(ids[i])){kept.segments.push_back(segments[i]);kept.segmentIds.push_back(ids[i]);}
        cutIds.insert(cuts.begin(),cuts.end());
        if(kept.segments.empty()){if(!remove(owner))return;}
        else {
            const auto diagnostics=document::UpdateFeatureDefinitionCommand(owner,std::move(kept),inputs,"線分を削除").Apply(after);
            for(const auto& d:diagnostics)if(d.IsError()){fail(QString::fromStdString(d.summaryJa));return;}
            partialNames.push_back(QString::fromStdString(original->displayName)+QStringLiteral("（選択した線分）"));
        }
    }
    const auto gone=[&](base::EntityId id){return std::none_of(after.entities.begin(),after.entities.end(),[&](const auto& e){return e.id==id;});};
    QStringList names=partialNames;std::vector<base::EntityId> preview;
    bool dependents=dependencyCascade||!folders.empty();bool keepContents=false;
    for(const auto& group:before.groups)if(allFolders.contains(group.id))names.push_back(QStringLiteral("グループ: ")+QString::fromStdString(group.displayName));
    for(const auto& entity:before.entities)if(gone(entity.id)){
        preview.push_back(entity.id);names.push_back(QString::fromStdString(entity.displayName));
        if(!roots.contains(entity.createdBy)||std::find(selection.entityIds.begin(),selection.entityIds.end(),entity.id)==selection.entityIds.end())dependents=true;
    }
    for(auto id:selection.entityIds)if(std::find(preview.begin(),preview.end(),id)==preview.end())preview.push_back(id);
    std::erase_if(after.referenceDimensions,[&](const auto& d){
        const bool affected=std::any_of(d.targets.begin(),d.targets.end(),gone)
            ||std::any_of(d.segments.begin(),d.segments.end(),[&](const auto& s){return gone(s.entityId)||cutIds.contains(s.segmentId);});
        if(affected){names.push_back(QStringLiteral("寸法: ")+QString::fromStdString(d.label));dependents=true;}
        return affected;
    });
    if(dependents){
        viewport_->SetDeletionPreview(preview);RefreshShapeViews();
        const int choice=ConfirmDeletion(this,names,!folders.empty());
        const bool accepted=choice>=0;keepContents=choice==1;
        viewport_->SetDeletionPreview({});RefreshShapeViews();
        if(!accepted){fail(QStringLiteral("使っているものを含む削除を取り消しました。"));return;}
    }
    if(keepContents){after=before;for(const auto id:folders){const auto diagnostics=document::RemoveGroupCommand(id).Apply(after);
        for(const auto& d:diagnostics)if(d.IsError()){ReportDiagnostics(diagnostics);return;}}}
    const bool hadUndo=session_->GetDocument().CanUndo();
    const auto result=session_->GetDocument().Run(DeletePlan(std::move(after)));
    if(!result.committed){ReportDiagnostics(result.diagnostics);return;}
    V2DrawingSelection::Of(*viewport_).Deleted(*session_,before.revision,hadUndo);
    if(!V2DrawingSelection::IsDrawing(session_->CurrentTool())&&!V2ToolSelection::Active(*viewport_))session_->CancelTool();
    AdoptCurrentDocument();RefreshShapeViews();RefreshEntityList();
    SetStatus(keepContents?QStringLiteral("グループを解除しました。中身は残っています。Ctrl+Zで戻せます。"):
        QStringLiteral("削除しました（%1項目）。Ctrl+Zでまとめて戻せます。").arg(names.size()));
}
