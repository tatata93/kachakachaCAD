#include "V2PreparedMeshes.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/app/GeometryJobDiagnostic.h"
#include <array>
#include <exception>
#include <future>
#include <optional>

using namespace kachakacha::v2;
base::Result<bool> V2BuildPreparedMeshes(const std::vector<V2PreparedMeshRequest>& requests,
    std::map<std::string,modeling::ShapeMesh>& meshes,bool parallel) {
    using Result=base::Result<modeling::ShapeMesh>;
    std::vector<std::optional<Result>> results(requests.size());
    const std::size_t lanes=parallel&&requests.size()>1?2:1;
    // BuildShapeMesh tessellates a deep topology copy, never a shared editable
    // shape. Two fixed lanes bound memory and avoid a thread per small part.
    const auto build=[&](std::size_t lane) {
        for(auto i=lane;i<requests.size();i+=lanes) {
            try {results[i].emplace(kernel::BuildShapeMesh(requests[i].shape,0.0,requests[i].surface?8:0));}
            catch(const std::exception& e){results[i].emplace(Result::Failure(base::MakeError(app::kGeometryJobFailed,"表示形状を作成できませんでした。",e.what())));}
            catch(...){results[i].emplace(Result::Failure(base::MakeError(app::kGeometryJobFailed,"表示形状を作成できませんでした。")));}
        }
    };
    std::future<void> worker;
    if(lanes==2) {
        try {worker=std::async(std::launch::async,[&]{build(1);});}
        catch(...){build(1);}
    }
    build(0);if(worker.valid())worker.get();
    for(std::size_t i=0;i<requests.size();++i) {
        if(!results[i]||!results[i]->HasValue())return results[i]?base::Result<bool>::Failure(results[i]->Diagnostics()):
            base::Result<bool>::Failure(base::MakeError(app::kGeometryJobFailed,"表示形状の計算結果がありません。"));
        meshes.emplace(requests[i].id,std::move(results[i]->Value()));
        results[i].reset(); // Result::Value is const: release each source immediately after its one copy.
    }
    return base::Result<bool>::Success(true);
}
