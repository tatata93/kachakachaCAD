#include "V2ToolSelection.h"
#include "V2ProjectionTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2EntityTree.h"
#include "kachakacha/app/SurfaceRoleAssist.h"
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidgetItem>
#include <QVariant>
#include <QStringList>
#include <QString>
#include <QVariantList>
#include <algorithm>
using namespace kachakacha::v2;
namespace {
constexpr int Marker = Qt::UserRole + 46;
QColor RoleColor(bool source) {
    const auto c=app::WireRoleColor(source?app::WireRoleChoice::Boundary:app::WireRoleChoice::PassThrough);
    return QColor(c.r,c.g,c.b);
}
void RestoreRow(QTreeWidgetItem* row) {
    const auto old=row->data(0,Marker).toList();if(old.isEmpty())return;
    row->setText(0,old[0].toString());row->setForeground(0,old[1].value<QBrush>());
    row->setFont(0,old[2].value<QFont>());row->setToolTip(0,old[3].toString());
    row->setFlags(Qt::ItemFlags(old[4].toInt()));row->setData(0,Marker,{});
}
void MarkRow(QTreeWidgetItem* row,const QString& role,QColor color) {
    if(!row)return;
    if(row->data(0,Marker).isValid()){row->setText(0,row->text(0)+QStringLiteral(" [%1]").arg(role));return;}
    row->setData(0,Marker,QVariantList{row->text(0),QVariant::fromValue(row->foreground(0)),
        QVariant::fromValue(row->font(0)),row->toolTip(0),static_cast<int>(row->flags())});
    row->setText(0,QStringLiteral("[%1] %2").arg(role,row->text(0)));
    row->setForeground(0,QBrush(color));auto font=row->font(0);font.setBold(true);row->setFont(0,font);
    row->setFlags(row->flags()&~Qt::ItemIsEditable);
    row->setToolTip(0,role+QStringLiteral(" — 投影ツールの一時表示。元の名前・表示状態は変わりません。"));
}
}
void V2ProjectionTool::SetPickRole(int role) {
    if(V2ToolSelection::Active(*window_.viewport_))V2ToolSelection::End(*window_.viewport_);
    role_=role;pickWire_->setChecked(role==0);pickTarget_->setChecked(role==1);
    const QStringList prompts={QStringLiteral("① 元ワイヤー: 3Dビューまたは左ペインで追加します。"),
        QStringLiteral("② 投影先: 面の内側をクリック。立体はフェイスを選びます。"),
        QStringLiteral("巻き付け元の基準点をクリックしてください。"),
        QStringLiteral("投影先の配置基準点をクリックしてください。"),
        QStringLiteral("投影方向にする直線をクリックしてください。")};
    pickPrompt_->setText(prompts[role]);window_.SetStatus(prompts[role]);
}
void V2ProjectionTool::ClearMarks() {
    window_.viewport_->SetRoleColors({});window_.viewport_->SetFaceRoleColors({});
    window_.viewport_->SetSegmentRoleColors({});
    window_.viewport_->HideToolRoleLabels();
    QSignalBlocker blocked(window_.entityTree_);
    for(const auto& [row,id]:window_.entityItems_)RestoreRow(row);
}
void V2ProjectionTool::RefreshMarks() {
    std::vector<std::pair<base::EntityId,QColor>> wires;
    std::vector<V2Viewport::SegmentRoleColor> segments;
    std::vector<V2Viewport::FaceRoleColor> faces;
    std::vector<V2Viewport::PlacedRoleLabel> labels;
    QSignalBlocker blocked(window_.entityTree_);
    for(const auto& [row,id]:window_.entityItems_)RestoreRow(row);
    for(std::size_t i=0;i<sources_.size();++i) {
        const auto& s=sources_[i];const auto color=RoleColor(true);
        if(s.ref.segmentId)segments.push_back({s.ref.entityId,*s.ref.segmentId,color});else wires.emplace_back(s.ref.entityId,color);
        const auto role=QStringLiteral("元 %1").arg(i+1);
        if(!s.input.curves.empty())labels.push_back({s.input.curves.front().Evaluate(.5),role,color});
        MarkRow(window_.ItemOfEntity(s.ref.entityId),role,color);
        wireRows_->item(static_cast<int>(i))->setForeground(QBrush(color));
    }
    for(std::size_t i=0;i<targets_.size();++i) {
        const auto& t=targets_[i];const auto color=RoleColor(false);
        const auto role=QStringLiteral("先 %1").arg(i+1);
        faces.push_back({t.ref.entityId,t.ref.pickedFaceIndex,color});
        labels.push_back({t.ref.hitPoint,role,color});
        auto* row=window_.ItemOfEntity(t.ref.entityId);
        const auto face=t.ref.pickedFaceIndex?QStringLiteral(" / F%1").arg(*t.ref.pickedFaceIndex+1):QString();
        if(row&&row->data(0,Marker).isValid()) {
            row->setText(0,row->text(0)+QStringLiteral(" [%1%2]").arg(role,face));
        } else MarkRow(row,role+face,color);
        targetRows_->item(static_cast<int>(i))->setForeground(QBrush(color));
    }
    pickWire_->setText(QStringLiteral("① 元ワイヤーを選ぶ（%1）").arg(sources_.size()));
    pickTarget_->setText(QStringLiteral("② 投影先を選ぶ（%1面）").arg(targets_.size()));
    window_.viewport_->SetRoleColors(std::move(wires));window_.viewport_->SetFaceRoleColors(std::move(faces));
    window_.viewport_->SetSegmentRoleColors(std::move(segments));
    window_.viewport_->ShowToolRoleLabels(std::move(labels));
}
void V2ProjectionTool::TakeActiveSelection() {
    const auto selection=window_.viewport_->Selection();
    for(const auto& id:selection.entityIds) {
        const auto* e=window_.session_->GetDocument().FindEntity(id);if(!e)continue;
        app::SelectionRef ref;ref.entityId=id;
        for(const auto& candidate:selection.ordered)if(candidate.entityId==id){ref=candidate;break;}
        if(role_==0&&e->kind==domain::EntityKind::Wire)AddWire(ref,false);
        if(role_==1&&(e->kind==domain::EntityKind::GuideSurface||e->kind==domain::EntityKind::Part))AddTarget(ref,false);
    }
}
