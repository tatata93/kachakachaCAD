#include "V2OutputTool.h"
#include "V2OutputPreview.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <QListWidget>
#include <QVariant>
#include <QString>

using namespace kachakacha::v2;
bool V2OutputTool::PreparePreview() {
    const auto placement=Placement();
    std::vector<double> key{double(placement.keepPosition)};
    for(const auto v:{placement.source.origin,placement.source.normal,placement.source.xDirection,
        placement.destination.origin,placement.destination.normal,placement.destination.xDirection})
        key.insert(key.end(),{v.x,v.y,v.z});
    for(int i=0;i<targets_->count();++i)key.push_back(double(targets_->item(i)->checkState()));
    if(key==previewKey_)return true;
    if(!Prepare())return false;
    std::vector<modeling::ShapeMesh> meshes;
    for(const auto& asset:prepared_) {
        modeling::ShapeMesh mesh;
        if(asset.entity.kind==domain::EntityKind::Part||asset.entity.kind==domain::EntityKind::GuideSurface) {
            const auto made=kernel::BuildShapeMesh(asset.shape,0.01);
            setProperty("outputMeshBuilds",property("outputMeshBuilds").toULongLong()+1);
            if(!made.HasValue()){SetProblem(QString::fromStdString(made.FirstSummaryJa()));return false;}
            mesh=made.Value();
        } else {
            geometry::Bounds3 bounds;
            for(const auto& c:asset.curves) {
                std::vector<geometry::Vector3> edge;
                for(int k=0;k<=64;++k){edge.push_back(c.Evaluate(k/64.));bounds.Add(edge.back());}
                mesh.edges.push_back(std::move(edge));
            }
            if(asset.curves.empty()){mesh.edges.push_back({asset.frame.origin});bounds.Add(asset.frame.origin);}
            mesh.minimum=bounds.minimum;mesh.maximum=bounds.maximum;
        }
        meshes.push_back(std::move(mesh));
    }
    preview_->SetModels(std::move(meshes));previewKey_=std::move(key);
    setProperty("outputPreparationBuilds",property("outputPreparationBuilds").toULongLong()+1);
    return true;
}
