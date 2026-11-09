#include "V2OutputTool.h"
#include "V2OutputPreview.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <QListWidget>
#include <QVariant>
#include <QString>
#include <QElapsedTimer>
#include <iostream>

using namespace kachakacha::v2;
bool V2OutputTool::EnsurePrepared() {
    if(!previewKey_.empty()&&preparedKey_==previewKey_)return true;
    if(previewKey_.empty()||!Prepare())return false;
    preparedKey_=previewKey_;return true;
}
bool V2OutputTool::PreparePreview() {
    const auto placement=Placement();
    std::vector<double> key{double(placement.keepPosition)};
    for(const auto v:{placement.source.origin,placement.source.normal,placement.source.xDirection,
        placement.destination.origin,placement.destination.normal,placement.destination.xDirection})
        key.insert(key.end(),{v.x,v.y,v.z});
    for(int i=0;i<targets_->count();++i)key.push_back(double(targets_->item(i)->checkState()));
    if(key==previewKey_)return true;
    QElapsedTimer timer;timer.start();
    if(!placement.Valid()){SetProblem(QStringLiteral("配置の法線と面内方向は、長さ0や平行にできません。"));return false;}
    // Preview is a rigid display transform of an owned source snapshot. The
    // current native geometry is built and checked by EnsurePrepared at the
    // confirmation boundary, never replaced by this display mesh.
    preparedKey_.clear();prepared_.clear();
    std::vector<modeling::ShapeMesh> meshes;
    for(int i=0;i<targets_->count();++i) {
        if(targets_->item(i)->checkState()!=Qt::Checked)continue;
        auto asset=assets_[static_cast<std::size_t>(i)];
        modeling::ShapeMesh mesh;
        if(asset.entity.kind==domain::EntityKind::Part||asset.entity.kind==domain::EntityKind::GuideSurface) {
            auto cached=previewSourceMeshes_.find(asset.shape.value);
            if(cached==previewSourceMeshes_.end()){
                const auto made=kernel::BuildShapeMesh(asset.shape,0.01);
                setProperty("outputMeshBuilds",property("outputMeshBuilds").toULongLong()+1);
                if(!made.HasValue()){SetProblem(QString::fromStdString(made.FirstSummaryJa()));return false;}
                cached=previewSourceMeshes_.emplace(asset.shape.value,made.Value()).first;
            }
            mesh=cached->second;
            for(auto& triangle:mesh.triangles){
                for(auto& point:triangle.points)point=placement.Point(point);
                triangle.normal=placement.Direction(triangle.normal);
                for(auto& normal:triangle.vertexNormals)normal=placement.Direction(normal);
            }
            for(auto* paths:{&mesh.edges,&mesh.isoLines})for(auto& path:*paths)for(auto& point:path)point=placement.Point(point);
            modeling::RefreshBounds(mesh);
        } else {
            geometry::Bounds3 bounds;
            for(const auto& c:asset.curves) {
                std::vector<geometry::Vector3> edge;
                for(int k=0;k<=64;++k){edge.push_back(placement.Point(c.Evaluate(k/64.)));bounds.Add(edge.back());}
                mesh.edges.push_back(std::move(edge));
            }
            if(asset.curves.empty()){const auto point=placement.Point(asset.frame.origin);mesh.edges.push_back({point});bounds.Add(point);}
            mesh.minimum=bounds.minimum;mesh.maximum=bounds.maximum;
        }
        asset.frame.origin=placement.Point(asset.frame.origin);asset.frame.normal=placement.Direction(asset.frame.normal);
        asset.frame.xDirection=placement.Direction(asset.frame.xDirection);prepared_.push_back(std::move(asset));
        meshes.push_back(std::move(mesh));
    }
    if(prepared_.empty()){SetProblem(QStringLiteral("出す対象にチェックしてください。"));return false;}
    preview_->SetModels(std::move(meshes));previewKey_=std::move(key);
    if(qEnvironmentVariableIsSet("KACHACAD_OUTPUT_PERF_OUTPUT"))std::cout<<"output mesh total"<<timer.elapsed()<<std::endl;
    setProperty("outputPreparationBuilds",property("outputPreparationBuilds").toULongLong()+1);
    return true;
}
