#include "V2Viewport.h"
#include "kachakacha/app/SceneBuilder.h"
#include <QPushButton>
#include <QCheckBox>
#include <QSignalBlocker>
#include <algorithm>

namespace {
using namespace kachakacha::v2;
modeling::ShapeMesh ClipSectionMesh(const modeling::ShapeMesh& source, geometry::Vector3 origin, geometry::Vector3 normal)
{
    auto out=source;out.triangles.clear();out.edges.clear();out.isoLines.clear();out.closed=false;
    const auto distance=[&](geometry::Vector3 p){return geometry::Dot(p-origin,normal);};
    struct Vertex {geometry::Vector3 p,n;};
    for(const auto& triangle:source.triangles){
        std::vector<Vertex> polygon;
        for(std::size_t i=0;i<3;++i){
            const auto j=(i+1)%3;const auto a=triangle.points[i],b=triangle.points[j];
            const double da=distance(a),db=distance(b);const bool ia=da<=0,ib=db<=0;
            if(ia)polygon.push_back({a,triangle.vertexNormals[i]});
            if(ia!=ib){const double t=da/(da-db);polygon.push_back({a+(b-a)*t,geometry::Normalized(triangle.vertexNormals[i]*(1-t)+triangle.vertexNormals[j]*t)});}
        }
        for(std::size_t i=1;i+1<polygon.size();++i){auto clipped=triangle;
            const Vertex vertices[]={polygon[0],polygon[i],polygon[i+1]};
            for(std::size_t j=0;j<3;++j){clipped.points[j]=vertices[j].p;clipped.vertexNormals[j]=vertices[j].n;}
            if(geometry::Cross(clipped.points[1]-clipped.points[0],clipped.points[2]-clipped.points[0]).Length()>1e-12)out.triangles.push_back(clipped);
        }
        // Show the actual cut boundary. It is display geometry, never a persistent wire.
        std::vector<geometry::Vector3> intersections;
        for(std::size_t i=0;i<3;++i){const auto a=triangle.points[i],b=triangle.points[(i+1)%3];const double da=distance(a),db=distance(b);
            if((da<0&&db>0)||(da>0&&db<0))intersections.push_back(a+(b-a)*(da/(da-db)));}
        if(intersections.size()==2)out.edges.push_back(intersections);
    }
    const auto clipLines=[&](const auto& lines,auto& destination){for(const auto& line:lines){
        std::vector<geometry::Vector3> run;
        for(std::size_t i=1;i<line.size();++i){auto a=line[i-1],b=line[i];const double da=distance(a),db=distance(b);
            if(da>0&&db>0){if(run.size()>1)destination.push_back(run);run.clear();continue;}
            if(da>0)a=a+(b-a)*(da/(da-db));
            if(db>0)b=a+(b-a)*(distance(a)/(distance(a)-db));
            if(run.empty())run.push_back(a);run.push_back(b);
            if(db>0){destination.push_back(run);run.clear();}
        }
        if(run.size()>1)destination.push_back(run);
    }};
    clipLines(source.edges,out.edges);clipLines(source.isoLines,out.isoLines);return out;
}
}

void V2Viewport::SetShapeViews(std::vector<ShapeView> shapes)
{
    if(sectionView_) originalShapeViews_=std::move(shapes);
    else {shapeViews_=std::move(shapes);originalShapeViews_.clear();}
    RebuildSectionViews();
}

void V2Viewport::SetSectionView(bool enabled,bool reverse)
{
    if(enabled&&!sectionView_) originalShapeViews_=shapeViews_;
    sectionView_=enabled;sectionReverse_=reverse;
    if(auto* check=window()->findChild<QCheckBox*>(QStringLiteral("sectionView"))){QSignalBlocker block(check);check->setChecked(enabled);}
    sectionSign_=kachakacha::v2::geometry::Dot(kachakacha::v2::view::ForwardOf(orientation_),workPlane_.normal)>0?-1.0:1.0;
    if(reverse)sectionSign_=-sectionSign_;
    RebuildSectionViews();ApplyVisibilityFilter();
    auto* exit=findChild<QPushButton*>(QStringLiteral("exitSection"));
    if(!exit){exit=new QPushButton(QStringLiteral("断面表示中 — 解除"),this);exit->setObjectName(QStringLiteral("exitSection"));
        connect(exit,&QPushButton::clicked,this,[this]{SetSectionView(false);});}
    exit->move(8,116);exit->resize(175,28);exit->setVisible(enabled);exit->raise();
}

bool V2Viewport::DeletionPreviewContains(kachakacha::v2::base::EntityId id) const
{
    return std::find(deletionPreview_.begin(),deletionPreview_.end(),id)!=deletionPreview_.end();
}
void V2Viewport::SetDeletionPreview(std::vector<kachakacha::v2::base::EntityId> ids)
{
    using namespace kachakacha::v2;
    deletionPreview_=std::move(ids);
    if(deletionPreview_.empty()){
        if(beforeDeletionPreview_){session_->SetScene(std::move(*beforeDeletionPreview_));beforeDeletionPreview_.reset();}
    }else{
        beforeDeletionPreview_=session_->Scene();auto snapshot=session_->GetDocument().Snapshot();
        for(auto& entity:snapshot.entities)if(DeletionPreviewContains(entity.id)){entity.visibility=domain::Visibility::Visible;entity.groupId.reset();}
        base::RandomIdGenerator previewIds;
        auto scene=app::RebuildSceneKeepingView(session_->Scene(),snapshot,previewIds);
        // Retain native edges and the scene's other transient overlays during the modal preview.
        for(const auto& curve:beforeDeletionPreview_->curves)if(std::none_of(scene.curves.begin(),scene.curves.end(),[&](const auto& c){return c.entityId==curve.entityId&&c.segmentId==curve.segmentId;}))scene.curves.push_back(curve);
        session_->SetScene(std::move(scene));
    }
    smoothRasterKey_.clear();update();repaint();
}

void V2Viewport::RebuildSectionViews()
{
    hoverShapeCandidates_.reset();
    ++shapeViewRevision_;
    if(sectionView_) shapeViews_=originalShapeViews_;
    else if(!originalShapeViews_.empty()) {shapeViews_=std::move(originalShapeViews_);originalShapeViews_.clear();}
    if(sectionView_) for(auto& shape:shapeViews_) shape.mesh=ClipSectionMesh(shape.mesh,workPlane_.origin+workPlane_.normal*property("sectionOffsetMm").toDouble(),workPlane_.normal*sectionSign_);
    RefreshFaceRolePieces();
    // 当たり判定へ渡す網を作り直す。カーソルが動くたびに三角形を写さないため、
    // ここで1度だけ並べておく。並びは shapeViews_ と同じ(索引で引き当てる)。
    pickMeshes_.clear();
    pickMeshes_.reserve(shapeViews_.size());
    for (const ShapeView& shape : shapeViews_) {
        pickMeshes_.push_back(shape.mesh);
    }
    // 形が入れ替わったので、覚えていた候補は捨てる。
    ForgetPickCycle();
    update();
}

