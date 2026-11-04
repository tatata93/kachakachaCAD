#include "V2PreparedMeshes.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/app/GeometryJobDiagnostic.h"
#include <array>
#include <exception>
#include <future>
#include <optional>
#include <algorithm>
#include <thread>
#include <vector>

using namespace kachakacha::v2;
base::Result<bool> V2BuildPreparedMeshes(const std::vector<V2PreparedMeshRequest>& requests,
    std::map<std::string,modeling::ShapeMesh>& meshes,bool parallel) {
    using Result=base::Result<modeling::ShapeMesh>;
    std::vector<std::optional<Result>> results(requests.size());
    const auto hardware=std::thread::hardware_concurrency();
    const std::size_t lanes=parallel&&hardware>=4?std::min<std::size_t>(3,requests.size()):1;
    // BuildShapeMesh tessellates a deep topology copy, never a shared editable
    // shape. At most three fixed lanes leave capacity for the visible process
    // and bound native copies instead of launching a thread per small part.
    const auto build=[&](std::size_t lane) {
        for(auto i=lane;i<requests.size();i+=lanes) {
            try {results[i].emplace(kernel::BuildShapeMesh(requests[i].shape,0.0,requests[i].surface?8:0));}
            catch(const std::exception& e){results[i].emplace(Result::Failure(base::MakeError(app::kGeometryJobFailed,"表示形状を作成できませんでした。",e.what())));}
            catch(...){results[i].emplace(Result::Failure(base::MakeError(app::kGeometryJobFailed,"表示形状を作成できませんでした。")));}
        }
    };
    std::vector<std::future<void>> workers;workers.reserve(lanes>0?lanes-1:0);
    for(std::size_t lane=1;lane<lanes;++lane) {
        try {workers.push_back(std::async(std::launch::async,[&,lane]{build(lane);}));}
        catch(...){build(lane);}
    }
    build(0);for(auto& worker:workers)worker.get();
    for(std::size_t i=0;i<requests.size();++i) {
        if(!results[i]||!results[i]->HasValue())return results[i]?base::Result<bool>::Failure(results[i]->Diagnostics()):
            base::Result<bool>::Failure(base::MakeError(app::kGeometryJobFailed,"表示形状の計算結果がありません。"));
        meshes.emplace(requests[i].id,std::move(results[i]->Value()));
        results[i].reset(); // Result::Value is const: release each source immediately after its one copy.
    }
    return base::Result<bool>::Success(true);
}
