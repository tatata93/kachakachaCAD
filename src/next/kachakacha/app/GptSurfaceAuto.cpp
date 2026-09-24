#include "kachakacha/app/GptSurfaceAuto.h"
#include "kachakacha/app/LoopGraph.h"
#include <algorithm>
#include <set>

namespace kachakacha::v2::app {
namespace {
using base::Result;
using AutoResult = Result<GptSurfaceAutoResult>;
AutoResult Fail(const std::string& message)
{
    return AutoResult::Failure(base::MakeError("GPT-S007", message,
        "画面の番号で線を確認してください。自動判定を外すと、一覧で外周と通る線を指定できます。"));
}
struct InputEdge {
    domain::SegmentRef ref;
    geometry::CurveSegment curve;
};
Result<std::vector<InputEdge>> Collect(const modeling::SnapScene& scene,
    const domain::CreateGuideSurfaceDefinition& input)
{
    std::vector<InputEdge> edges;
    std::set<std::pair<base::EntityId, base::SegmentId>> used;
    for (const auto& chain : input.chains) {
        for (const auto& ref : chain.segments) {
            bool found = false;
            for (const auto& curve : scene.curves) {
                if (curve.entityId != ref.entityId || (!ref.segmentId.IsNil() && ref.segmentId != curve.segmentId)) { continue; }
                if (ref.startParameter != 0.0 || ref.endParameter != 1.0
                    || !used.insert({curve.entityId, curve.segmentId}).second) {
                    return Result<std::vector<InputEdge>>::Failure(base::MakeError("GPT-S007",
                        "重複または部分参照があります。", "入力を追加し直してください。"));
                }
                edges.push_back({{curve.entityId, curve.segmentId}, curve.segment});
                found = true;
            }
            if (!found) {
                return Result<std::vector<InputEdge>>::Failure(base::MakeError("GPT-S007",
                    "入力線を解決できません。", "表示と元ワイヤーを確認してください。"));
            }
        }
    }
    std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) {
        return std::make_pair(a.ref.entityId, a.ref.segmentId) < std::make_pair(b.ref.entityId, b.ref.segmentId);
    });
    return Result<std::vector<InputEdge>>::Success(std::move(edges));
}
}

AutoResult AutoGptSurfaceBoundary(const modeling::SnapScene& scene,
    const domain::CreateGuideSurfaceDefinition& input,
    const geometry::GeometryTolerance& tolerance, std::size_t candidate)
{
    const auto collected = Collect(scene, input);
    if (!collected.HasValue()) { return AutoResult::Failure(collected.Diagnostics()); }
    const auto& sources = collected.Value();
    // 閉路列挙の指数的増加を制限し、候補を黙って切り捨てない。
    if (sources.size() > 24) { return Fail("自動判定は24辺までです。手動で役割を指定してください。"); }
    LoopGraph graph(tolerance.modelLinearMm);
    std::vector<LoopEdge> edges;
    for (std::size_t at = 0; at < sources.size(); ++at) {
        edges.push_back(MakeLoopEdge(at, {sources[at].curve}, graph));
    }
    PruneDeadEnds(edges, graph.NodeCount());
    auto cycles = FindLoopCycles(edges, 512);
    if (cycles.size() >= 512) { return Fail("外周候補が多すぎます。手動で役割を指定してください。"); }
    std::stable_sort(cycles.begin(), cycles.end(), [&](const auto& a, const auto& b) {
        return EnclosedArea(edges, a) > EnclosedArea(edges, b);
    });
    if (cycles.empty()) { return Fail("閉じた外周がまだありません。囲みの残りの線を追加してください。"); }
    GptSurfaceAutoResult result;
    result.definition = input;
    result.definition.chains.clear();
    result.definition.roles.clear();
    result.candidateCount = cycles.size();
    result.candidateIndex = candidate % cycles.size();
    const auto& cycle = cycles[result.candidateIndex];
    result.boundaryCount = cycle.edges.size();
    const auto append = [&](std::size_t edge, int role, bool reversed) {
        result.definition.chains.push_back({{sources[edge].ref}, {reversed}});
        result.definition.roles.push_back(role);
    };
    for (std::size_t at = 0; at < cycle.edges.size(); ++at) {
        append(cycle.edges[at], kGptBoundaryRole, !cycle.forward[at]);
    }
    for (std::size_t edge = 0; edge < sources.size(); ++edge) {
        if (std::find(cycle.edges.begin(), cycle.edges.end(), edge) == cycle.edges.end()) {
            append(edge, kGptInteriorRole, false);
        }
    }
    return AutoResult::Success(std::move(result));
}
}
