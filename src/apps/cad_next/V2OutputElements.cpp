#include "V2OutputTool.h"
#include "V2MainWindow.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/kernel/OcctOutput.h"
#include <QString>
#include <algorithm>
using namespace kachakacha::v2;
bool V2OutputTool::CaptureElements() {
    const auto& selection=window_.viewport_->Selection();if(selection.ordered.empty())return true;
    const auto scene=app::BuildSceneFromDocument(window_.session_->GetDocument().Snapshot(),*window_.ids_);
    std::vector<V2OutputAsset> selected;
    for(const auto& ref:selection.ordered){
        const auto found=std::find_if(assets_.begin(),assets_.end(),[&](const auto& asset){return asset.entity.id==ref.entityId;});
        const auto* original=window_.session_->GetDocument().FindEntity(ref.entityId);
        if(original&&original->kind==domain::EntityKind::FabricationModel){SetProblem(QStringLiteral("近似モデルは「物体全体」で指定した対象部材を出力してください。"));return false;}
        if(found==assets_.end()){SetProblem(QStringLiteral("選択部分の元形状がありません。"));return false;}
        auto asset=*found;
        if(ref.kind==app::SelectionElementKind::Face && ref.pickedFaceIndex){
            const auto face=kernel::OutputFace(asset.shape,*ref.pickedFaceIndex);if(!face.HasValue()){SetProblem(QStringLiteral("選択した面を取り出せません。"));return false;}
            asset.shape=face.Value();asset.curves.clear();asset.entity.kind=domain::EntityKind::GuideSurface;asset.entity.displayName+=" / 面";
        }else if(ref.kind==app::SelectionElementKind::Vertex || ref.kind==app::SelectionElementKind::ControlPoint){
            asset.shape={};asset.curves.clear();asset.entity.kind=domain::EntityKind::Point;asset.frame.origin=ref.hitPoint;asset.entity.displayName+=" / 点";
        }else if(ref.kind==app::SelectionElementKind::Edge){
            if(asset.entity.kind==domain::EntityKind::Wire){
                const auto curve=std::find_if(scene.curves.begin(),scene.curves.end(),[&](const auto& c){return c.entityId==ref.entityId && ref.segmentId && c.segmentId==*ref.segmentId;});
                if(curve==scene.curves.end()){SetProblem(QStringLiteral("選択した線の区間を取り出せません。"));return false;}asset.curves={curve->segment};
            }else {const auto edge=kernel::OutputEdge(asset.shape,ref.hitPoint);if(!edge.HasValue()){SetProblem(QStringLiteral("選択した辺を取り出せません。"));return false;}asset.curves={edge.Value()};}
            asset.shape={};asset.entity.kind=domain::EntityKind::Wire;asset.entity.displayName+=" / 辺";
        }else if(ref.kind==app::SelectionElementKind::Face && asset.entity.kind!=domain::EntityKind::GuideSurface){SetProblem(QStringLiteral("面の参照がありません。選び直してください。"));return false;}
        selected.push_back(std::move(asset));
    }
    assets_=std::move(selected);return !assets_.empty();
}
