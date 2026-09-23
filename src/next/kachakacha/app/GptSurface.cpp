#include "kachakacha/app/GptSurface.h"

#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/geometry/WireChain.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace kachakacha::v2::app {
namespace {
using base::MakeError;
using base::Result;
using geometry::CurveSegment;

Result<GptSurfaceRequest> Fail(const std::string& reason, const std::string& fix)
{
    return Result<GptSurfaceRequest>::Failure(MakeError("GPT-S001", reason, fix));
}

Result<GptSurfaceCurve> Connected(GptSurfaceCurve curve,
    const geometry::GeometryTolerance& tolerance)
{
    std::vector<geometry::ChainInput> inputs;
    for (std::size_t index = 0; index < curve.segments.size(); ++index) {
        std::array<std::uint8_t, 16> bytes{};
        for (std::size_t byte = 0; byte < sizeof(index); ++byte) {
            bytes[15 - byte] = static_cast<std::uint8_t>((index + 1) >> (byte * 8));
        }
        inputs.push_back({{}, base::SegmentId(base::Uuid(bytes)), curve.segments[index]});
    }
    auto chain = geometry::AnalyzeChain(inputs, tolerance);
    if (!chain.HasValue()) {
        return Result<GptSurfaceCurve>::Failure(chain.Diagnostics());
    }
    curve.segments.clear();
    curve.closed = chain.Value().order.closed;
    for (const auto& ordered : chain.Value().order.segments) {
        const auto found = std::find_if(inputs.begin(), inputs.end(), [&](const auto& input) {
            return input.segmentId == ordered.segmentId;
        });
        if (ordered.reversed) {
            const auto reversed = geometry::ReverseCurve(found->segment);
            if (!reversed.HasValue()) { return Result<GptSurfaceCurve>::Failure(reversed.Diagnostics()); }
            curve.segments.push_back(reversed.Value());
        } else {
            curve.segments.push_back(found->segment);
        }
    }
    const auto intersections = geometry::FindSelfIntersections(curve.segments, curve.closed, tolerance);
    if (!intersections.HasValue()) { return Result<GptSurfaceCurve>::Failure(intersections.Diagnostics()); }
    if (!intersections.Value().empty()) {
        return Result<GptSurfaceCurve>::Failure(MakeError("GPT-S002",
            curve.label + "が自己交差しています。", "交差しない外周または断面を選び直してください。"));
    }
    return Result<GptSurfaceCurve>::Success(std::move(curve));
}
} // namespace

Result<GptSurfaceRequest> ValidateGptSurface(GptSurfaceRequest request,
    const geometry::GeometryTolerance& tolerance)
{
    if (!std::isfinite(request.maximumDeviationMm) || request.maximumDeviationMm <= 0.0) {
        return Fail("許容偏差が正の有限値ではありません。", "許容偏差をmmで指定してください。");
    }
    GptSurfaceCurve boundary;
    boundary.label = "外周";
    std::vector<GptSurfaceCurve> checked;
    for (auto curve : request.curves) {
        const bool allowed = request.loft ? curve.role == kGptSectionRole
            : curve.role == kGptBoundaryRole || curve.role == kGptInteriorRole;
        if (!allowed || curve.segments.empty()) {
            return Fail(curve.label + "の役割または線が不正です。", "作り方に合う役割を指定してください。");
        }
        if (!request.loft && curve.role == kGptBoundaryRole) {
            boundary.segments.insert(boundary.segments.end(), curve.segments.begin(), curve.segments.end());
            continue;
        }
        const auto connected = Connected(curve, tolerance);
        if (!connected.HasValue()) { return Result<GptSurfaceRequest>::Failure(connected.Diagnostics()); }
        // 1本の断面の向きは入力の始点を優先する。明示反転もここで保持する。
        auto next = connected.Value();
        if (!next.closed && (next.segments.front().StartPoint() - curve.segments.front().StartPoint()).Length()
            > tolerance.modelLinearMm) {
            std::reverse(next.segments.begin(), next.segments.end());
            for (auto& segment : next.segments) {
                const auto reversed = geometry::ReverseCurve(segment);
                if (!reversed.HasValue()) { return Result<GptSurfaceRequest>::Failure(reversed.Diagnostics()); }
                segment = reversed.Value();
            }
        }
        checked.push_back(std::move(next));
    }
    if (request.loft) {
        if (checked.size() < 2) { return Fail("断面が2つ以上必要です。", "線を選び、断面を追加してください。"); }
        for (const auto& curve : checked) {
            if (curve.closed != checked.front().closed) {
                return Fail("開いた断面と閉じた断面が混ざっています。", "全断面の開閉をそろえてください。");
            }
        }
    } else {
        if (boundary.segments.empty()) { return Fail("外周がありません。", "囲みの線を外周へ追加してください。"); }
        const auto connected = Connected(boundary, tolerance);
        if (!connected.HasValue()) { return Result<GptSurfaceRequest>::Failure(connected.Diagnostics()); }
        if (!connected.Value().closed) { return Fail("外周が閉じていません。", "端点を接続してください。自動で隙間は埋めません。"); }
        checked.insert(checked.begin(), connected.Value());
    }
    request.curves = std::move(checked);
    return Result<GptSurfaceRequest>::Success(std::move(request));
}

Result<GptSurfaceRequest> ResolveGptSurface(const document::Document& document,
    const modeling::SnapScene& scene, const domain::CreateGuideSurfaceDefinition& definition)
{
    if (!definition.gptBuilder || (definition.method != kGptBoundaryMethod && definition.method != kGptSectionsMethod)
        || definition.chains.size() != definition.roles.size()) {
        return Fail("GPT版の作り方または役割が不正です。", "外周または断面の入力を指定し直してください。");
    }
    GptSurfaceRequest request;
    request.loft = definition.method == kGptSectionsMethod;
    request.maximumDeviationMm = definition.gptToleranceMm;
    std::set<std::pair<base::EntityId, base::SegmentId>> used;
    for (std::size_t row = 0; row < definition.chains.size(); ++row) {
        const auto& chain = definition.chains[row];
        GptSurfaceCurve curve;
        curve.role = definition.roles[row];
        curve.label = "入力 " + std::to_string(row + 1);
        if (!chain.reversed.empty() && chain.reversed.size() != chain.segments.size()) {
            return Fail("線の向きの情報が不正です。", "該当する入力を追加し直してください。");
        }
        for (std::size_t refIndex = 0; refIndex < chain.segments.size(); ++refIndex) {
            const auto& ref = chain.segments[refIndex];
            const auto* entity = document.FindEntity(ref.entityId);
            if (entity == nullptr || entity->kind != domain::EntityKind::Wire
                || ref.startParameter != 0.0 || ref.endParameter != 1.0) {
                return Fail(curve.label + "の元ワイヤーを解決できません。", "ワイヤー全体を選び直してください。");
            }
            std::vector<CurveSegment> segments;
            for (const auto& source : scene.curves) {
                if (source.entityId != ref.entityId || (!ref.segmentId.IsNil() && source.segmentId != ref.segmentId)) { continue; }
                if (!used.insert({source.entityId, source.segmentId}).second) {
                    return Fail(entity->displayName + "が重複しています。", "同じ線を複数の役割へ入れないでください。");
                }
                segments.push_back(source.segment);
            }
            if (segments.empty()) { return Fail(entity->displayName + "の曲線がありません。", "元ワイヤーのエラーを直してください。"); }
            if (!chain.reversed.empty() && chain.reversed[refIndex]) {
                std::reverse(segments.begin(), segments.end());
                for (auto& segment : segments) {
                    const auto reverse = geometry::ReverseCurve(segment);
                    if (!reverse.HasValue()) { return Result<GptSurfaceRequest>::Failure(reverse.Diagnostics()); }
                    segment = reverse.Value();
                }
            }
            curve.segments.insert(curve.segments.end(), segments.begin(), segments.end());
        }
        request.curves.push_back(std::move(curve));
    }
    return ValidateGptSurface(std::move(request), document.Snapshot().settings.tolerance);
}
} // namespace kachakacha::v2::app
