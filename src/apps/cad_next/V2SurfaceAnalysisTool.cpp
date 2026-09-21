//! 「面の解析」の道具。V2SurfaceAnalysisTool.h の頭の注記を見よ。

#include "V2SurfaceAnalysisTool.h"

#include "V2MainWindow.h"
#include "V2SurfaceEditTool.h"
#include "V2Viewport.h"

#include "kachakacha/app/GroupTree.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/SurfaceAnalysis.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/kernel/OcctSurfaceAnalysis.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/SurfaceDeviationLimit.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using kachakacha::v2::app::Rgb;
using kachakacha::v2::app::SurfaceAnalysisMode;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::KernelShapeHandle;
using kachakacha::v2::modeling::SurfaceAnalysisData;

namespace {

//! 一度に塗る面の上限(全部の面を塗るとき)。多すぎると選択のたびに重くなる。
constexpr std::size_t kMaxTargets = 16;

[[nodiscard]] QString Text(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

[[nodiscard]] std::string Millimeters(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), value < 0.01 ? "%.4f" : "%.3f", value);
    return buffer;
}

} // namespace

V2SurfaceAnalysisTool::V2SurfaceAnalysisTool(V2MainWindow& window)
    : window_(window)
{
    dock_ = new V2SurfaceAnalysisDock(&window);
    dock_->SetModeHandler([this](SurfaceAnalysisMode mode) { SetMode(mode); });
    dock_->SetCloseHandler([this] { Close(); });
}

bool V2SurfaceAnalysisTool::Handles(std::string_view commandId) const
{
    return commandId == "view.surface_analysis" || commandId == "view.analysis_zebra";
}

void V2SurfaceAnalysisTool::Run(std::string_view commandId)
{
    if (commandId == "view.analysis_zebra") {
        // ゼブラの入り切り。棚は開かない(面を作りながら、下見の縞を見られる)。
        SetMode(mode_ == SurfaceAnalysisMode::Zebra ? SurfaceAnalysisMode::None
                                                    : SurfaceAnalysisMode::Zebra);
        return;
    }
    shown_ = true;
    window_.RefreshRightShelves();
    Refresh();
    window_.SetStatus(QStringLiteral(
        "面の解析: 見るものを選んでください。選んだ面(選んでいなければ全部の面)と、"
        "面を作る・面の編集の下見を塗ります。棚を閉じても表示は残ります。"));
}

void V2SurfaceAnalysisTool::SetMode(SurfaceAnalysisMode mode)
{
    mode_ = mode;
    Refresh();
    if (mode_ == SurfaceAnalysisMode::None) {
        window_.SetStatus(QStringLiteral("面の解析: 表示を消しました。"));
        return;
    }
    std::string line = "面の解析: " + std::string(kachakacha::v2::app::SurfaceAnalysisModeLabelJa(mode_))
        + "(" + std::to_string(targetCount_) + " 枚)。";
    if (targetCount_ == 0) {
        line += "塗れる面がありません。面を選ぶか、面を作ってください。";
    } else if (mode_ == SurfaceAnalysisMode::GaussianCurvature && !developability_.empty()) {
        line += "製作性の目安: " + developability_ + "(診断材料です。断定ではありません)。";
    }
    window_.SetStatus(Text(line));
}

void V2SurfaceAnalysisTool::Close()
{
    shown_ = false;
    window_.RefreshRightShelves();
    window_.SetStatus(mode_ == SurfaceAnalysisMode::None
            ? QStringLiteral("面の解析: 棚を閉じました。")
            : QStringLiteral("面の解析: 棚を閉じました。表示は残します(もう一度開いて「なし」で消えます)。"));
}

//! 面を作った線を、ずれを測る線として持たせる(作れない表なら持たせない)。
void V2SurfaceAnalysisTool::AddChains(Target& target,
    const kachakacha::v2::modeling::GuideTable& table) const
{
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    const auto request = kachakacha::v2::modeling::ToGuideSurfaceRequest(table, tolerance);
    if (!request.HasValue()) {
        return;
    }
    for (const auto& chain : request.Value().chains) {
        if (!chain.segments.empty()) {
            target.chains.push_back(chain.segments);
        }
    }
    target.exactMm = kachakacha::v2::modeling::SurfaceDeviationLimitMm(
        kachakacha::v2::modeling::GuideSurfaceMethod::LoftSections, tolerance);
    target.limitMm = kachakacha::v2::modeling::SurfaceDeviationLimitMm(request.Value(), tolerance);
}

//! 塗る面。選んだ面(形状ガイドの面・部品)があればそれだけ、無ければ見えている面を全部。
//! 面を作る・面の編集の下見の面は、いつも加える(作る前に見るため)。
std::vector<V2SurfaceAnalysisTool::Target> V2SurfaceAnalysisTool::Targets() const
{
    std::vector<Target> targets;
    if (window_.session_ == nullptr || window_.viewport_ == nullptr) {
        return targets;
    }
    const auto& document = window_.session_->GetDocument();
    const auto& snapshot = document.Snapshot();
    const bool deviation = mode_ == SurfaceAnalysisMode::Deviation;
    const auto add = [&](const EntityId& id, const KernelShapeHandle& handle) {
        const auto* entity = document.FindEntity(id);
        if (entity == nullptr || !handle.Valid() || targets.size() >= kMaxTargets
            || !kachakacha::v2::app::EntityEffectivelyVisible(snapshot, *entity)) {
            return;
        }
        for (const Target& existing : targets) {
            if (existing.entityId == id) {
                return;
            }
        }
        Target target{id, handle, entity->displayName.empty() ? std::string("名前のない面")
                                                              : entity->displayName, {}, 0.0, 0.0};
        const auto* feature = deviation ? document.FindFeature(entity->createdBy) : nullptr;
        const auto* definition = feature == nullptr ? nullptr
            : std::get_if<kachakacha::v2::domain::CreateGuideSurfaceDefinition>(&feature->definition);
        if (definition != nullptr) {
            const auto table = kachakacha::v2::app::GuideTableFromDefinition(document,
                window_.session_->Scene(), *definition);
            if (table.HasValue()) {
                AddChains(target, table.Value());
            }
        }
        targets.push_back(std::move(target));
    };
    const auto shapeOf = [this](const EntityId& id) {
        for (const auto* shapes : {&window_.guideShapes_, &window_.partShapes_}) {
            const auto found = shapes->find(id.ToString());
            if (found != shapes->end()) {
                return found->second;
            }
        }
        return KernelShapeHandle{};
    };
    for (const auto& ref : window_.viewport_->Selection().ordered) {
        const auto* entity = document.FindEntity(ref.entityId);
        if (entity != nullptr
            && (entity->kind == EntityKind::GuideSurface || entity->kind == EntityKind::Part)) {
            add(ref.entityId, shapeOf(ref.entityId));
        }
    }
    if (targets.empty()) {
        for (const auto& entity : snapshot.entities) {
            if (entity.kind == EntityKind::GuideSurface) {
                add(entity.id, shapeOf(entity.id));
            }
        }
    }
    // 下見の面(文書にはまだ無い)。
    const auto addPreview = [&](const KernelShapeHandle& handle, const std::string& name,
                                const kachakacha::v2::modeling::GuideTable* table) {
        if (!handle.Valid()) {
            return;
        }
        Target target{EntityId{}, handle, name, {}, 0.0, 0.0};
        if (deviation && table != nullptr) {
            AddChains(target, *table);
        }
        targets.push_back(std::move(target));
    };
    if (window_.surfaceSnapshot_.has_value()) {
        addPreview(window_.surfaceSnapshot_->built.handle, "面を作るの下見",
            &window_.surfaceSnapshot_->table);
        for (const auto& [table, built] : window_.surfaceSnapshot_->batch) {
            addPreview(built.handle, "面を作るの下見", &table);
        }
    }
    if (window_.surfaceEdit_ != nullptr) {
        for (const KernelShapeHandle& handle : window_.surfaceEdit_->PreviewSurfaces()) {
            addPreview(handle, "面の編集の下見", nullptr);
        }
    }
    return targets;
}

const V2SurfaceAnalysisTool::Sampled* V2SurfaceAnalysisTool::DataFor(
    const KernelShapeHandle& handle, bool report)
{
    const auto found = cache_.find(handle.value);
    if (found != cache_.end()) {
        return &found->second;
    }
    auto made = kachakacha::v2::kernel::AnalyzeSurfaceShape(handle, 40, 8);
    if (!made.HasValue()) {
        if (report) {
            window_.ReportDiagnostics(made.Diagnostics());
        }
        return nullptr;
    }
    auto data = std::make_shared<const SurfaceAnalysisData>(made.Value());
    auto triangles = std::make_shared<std::vector<kachakacha::v2::modeling::MeshTriangle>>();
    triangles->reserve(data->triangles.size());
    for (const auto& triangle : data->triangles) {
        triangles->push_back(triangle.triangle);
    }
    return &cache_.emplace(handle.value, Sampled{std::move(data), std::move(triangles)})
                .first->second;
}

//! 面を作るの下見の面(一括なら全部)。
std::vector<KernelShapeHandle> V2SurfaceAnalysisTool::PreviewHandles() const
{
    std::vector<KernelShapeHandle> handles;
    if (window_.surfaceSnapshot_.has_value()) {
        handles.push_back(window_.surfaceSnapshot_->built.handle);
        for (const auto& part : window_.surfaceSnapshot_->batch) {
            handles.push_back(part.second.handle);
        }
    }
    return handles;
}

std::string V2SurfaceAnalysisTool::PreviewDevelopabilityJa()
{
    std::vector<const SurfaceAnalysisData*> data;
    for (const KernelShapeHandle& handle : PreviewHandles()) {
        // 下見のたびに断りを出さない(作れた面が標本を取れないのは、診断の言い落としで済む)。
        if (const Sampled* one = handle.Valid() ? DataFor(handle, false) : nullptr) {
            data.push_back(one->data.get());
        }
    }
    const auto worst = kachakacha::v2::app::WorstDevelopability(data);
    if (worst.labelJa.empty()) {
        return std::string();
    }
    return "製作性の目安: " + worst.labelJa + "(平らに広げるのに要る伸び縮みの目安 "
        + Millimeters(worst.strain * 100.0) + " %。板厚・近似・部材の分け方でも変わる診断材料です。"
          "「面の解析」のガウス曲率で場所を見られます)";
}

V2SurfaceAnalysisTool::Lines V2SurfaceAnalysisTool::ContinuityLines(const Target& target,
    const std::vector<KernelShapeHandle>& neighbors) const
{
    Lines out;
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    const auto measured = kachakacha::v2::kernel::SurfaceEdgeContinuity(target.handle, neighbors,
        tolerance);
    if (!measured.HasValue()) {
        out.noteJa = measured.FirstSummaryJa();
        return out;
    }
    std::map<kachakacha::v2::app::ContinuityGrade, int> counts;
    for (const auto& sample : measured.Value()) {
        const auto grade = kachakacha::v2::app::GradeContinuity(sample);
        ++counts[grade];
        out.lines.push_back({sample.polyline, kachakacha::v2::app::ContinuityGradeColor(grade), 3.5});
    }
    for (const auto& [grade, count] : counts) {
        out.noteJa += (out.noteJa.empty() ? "" : "、")
            + std::string(kachakacha::v2::app::ContinuityGradeLabelJa(grade)) + " "
            + std::to_string(count) + " 本";
    }
    return out;
}

V2SurfaceAnalysisTool::Lines V2SurfaceAnalysisTool::DeviationLines(const Target& target) const
{
    Lines out;
    if (target.chains.empty()) {
        out.noteJa = "線から作っていない面(ずれは出しません)";
        return out;
    }
    const auto measured = kachakacha::v2::kernel::DeviationFromChains(target.handle, target.chains);
    if (!measured.HasValue()) {
        out.noteJa = measured.FirstSummaryJa();
        return out;
    }
    double worst = 0.0;
    for (const auto& sample : measured.Value()) {
        // 同じ色が続く所は 1 本の線にまとめる。
        V2Viewport::AnalysisLine run;
        for (std::size_t k = 0; k + 1 < sample.points.size(); ++k) {
            const double distance = std::max(sample.distancesMm[k], sample.distancesMm[k + 1]);
            worst = std::max(worst, distance);
            const Rgb color = kachakacha::v2::app::DeviationColor(distance, target.exactMm,
                target.limitMm);
            if (!run.points.empty() && !(run.color == color)) {
                out.lines.push_back(run);
                run.points.clear();
            }
            if (run.points.empty()) {
                run.points.push_back(sample.points[k]);
                run.color = color;
                run.width = 3.5;
            }
            run.points.push_back(sample.points[k + 1]);
        }
        if (run.points.size() >= 2) {
            out.lines.push_back(run);
        }
    }
    out.noteJa = "最大 " + Millimeters(worst) + " mm(この作り方の許容 " + Millimeters(target.limitMm)
        + " mm)";
    return out;
}

//! 重ねる線(U/V 線・曲率コーム・境目・ずれ)。同じ面・同じ隣なら作り直さない。
const V2SurfaceAnalysisTool::Lines& V2SurfaceAnalysisTool::LinesFor(const Target& target,
    const SurfaceAnalysisData& data, const std::vector<KernelShapeHandle>& neighbors)
{
    std::pair<std::uint64_t, std::string> key{target.handle.value,
        std::to_string(static_cast<int>(mode_))};
    if (mode_ == SurfaceAnalysisMode::Continuity) {
        for (const KernelShapeHandle& neighbor : neighbors) {
            key.second += "," + std::to_string(neighbor.value);
        }
    }
    const auto found = lineCache_.find(key);
    if (found != lineCache_.end()) {
        return found->second;
    }
    Lines out;
    switch (mode_) {
    case SurfaceAnalysisMode::IsoCurves:
        for (const auto& line : data.isoLines) {
            out.lines.push_back({line, Rgb{60, 120, 200}, 1.2});
        }
        out.noteJa = "U/V 線 " + std::to_string(data.isoLines.size()) + " 本";
        break;
    case SurfaceAnalysisMode::CurvatureComb: {
        double strongest = 0.0;
        for (const auto& comb : data.combs) {
            for (const auto& sample : comb) {
                strongest = std::max(strongest, sample.curvature);
            }
        }
        if (!(strongest > 1.0e-9)) {
            out.noteJa = "縁はまっすぐ(曲率 0)";
            break;
        }
        // いちばん長い歯を面の大きさの 12 % にする。歯は曲率の中心と反対側へ出す。
        const double scale = 0.12 * std::max(data.sizeMm, 1.0) / strongest;
        for (const auto& comb : data.combs) {
            V2Viewport::AnalysisLine envelope{{}, Rgb{200, 60, 160}, 1.6};
            for (const auto& sample : comb) {
                const Vector3 tip = sample.point - sample.towardCenter * (sample.curvature * scale);
                if (sample.curvature > 0.0) {
                    out.lines.push_back({{sample.point, tip}, Rgb{200, 60, 160}, 1.0});
                }
                envelope.points.push_back(tip);
            }
            out.lines.push_back(std::move(envelope));
        }
        out.noteJa = "縁の最大曲率 " + Millimeters(strongest) + " /mm(半径 "
            + Millimeters(1.0 / strongest) + " mm)";
        break;
    }
    case SurfaceAnalysisMode::Continuity:
        out = ContinuityLines(target, neighbors);
        break;
    case SurfaceAnalysisMode::Deviation:
        out = DeviationLines(target);
        break;
    case SurfaceAnalysisMode::GaussianCurvature: {
        const auto summary = kachakacha::v2::app::ClassifyDevelopability(data);
        out.noteJa = summary.labelJa + "(伸び縮みの目安 " + Millimeters(summary.strain * 100.0) + " %)";
        break;
    }
    case SurfaceAnalysisMode::None:
    case SurfaceAnalysisMode::Zebra:
    case SurfaceAnalysisMode::MeanCurvature:
        break;
    }
    return lineCache_.emplace(std::move(key), std::move(out)).first->second;
}

void V2SurfaceAnalysisTool::Refresh()
{
    if (window_.viewport_ == nullptr) {
        return;
    }
    const std::vector<Target> targets = mode_ == SurfaceAnalysisMode::None ? std::vector<Target>{}
                                                                           : Targets();
    std::vector<V2Viewport::AnalysisView> views;
    std::vector<const SurfaceAnalysisData*> data;
    std::vector<std::string> notes;
    std::vector<KernelShapeHandle> handles;
    for (const Target& target : targets) {
        handles.push_back(target.handle);
    }
    // 境目の隣には、塗らない面(選んでいない面)も入れる。つながる相手は選ばれていないことが多い。
    std::vector<KernelShapeHandle> neighbors = handles;
    if (mode_ == SurfaceAnalysisMode::Continuity) {
        for (const auto& entry : window_.guideShapes_) {
            if (neighbors.size() >= 2 * kMaxTargets) {
                break;
            }
            if (std::none_of(neighbors.begin(), neighbors.end(),
                    [&entry](const KernelShapeHandle& h) { return h.value == entry.second.value; })) {
                neighbors.push_back(entry.second);
            }
        }
    }
    std::set<std::uint64_t> used;
    for (const Target& target : targets) {
        const Sampled* one = DataFor(target.handle);
        if (one == nullptr) {
            continue;
        }
        used.insert(target.handle.value);
        data.push_back(one->data.get());
        V2Viewport::AnalysisView view;
        view.entityId = target.entityId;
        view.mode = mode_;
        view.data = one->data;
        view.triangles = one->triangles;
        const Lines& lines = LinesFor(target, *one->data, neighbors);
        view.lines = lines.lines;
        notes.push_back(target.nameJa + (lines.noteJa.empty() ? "" : ": " + lines.noteJa));
        views.push_back(std::move(view));
    }
    // 目盛りは全部で共通にする(面どうしの曲がり方を見比べられる)。
    const double scale = kachakacha::v2::app::CurvatureScale(data,
        mode_ == SurfaceAnalysisMode::GaussianCurvature);
    for (auto& view : views) {
        view.scale = scale;
    }
    // 使わなくなった標本と線は捨てる(下見は作り直すたびに番号が変わる)。面を作るの下見は
    // 解析を出していなくても製作性の目安に使うので残す。
    for (const KernelShapeHandle& handle : PreviewHandles()) {
        used.insert(handle.value);
    }
    for (auto it = cache_.begin(); it != cache_.end();) {
        it = used.count(it->first) == 0 ? cache_.erase(it) : std::next(it);
    }
    for (auto it = lineCache_.begin(); it != lineCache_.end();) {
        it = used.count(it->first.first) == 0 ? lineCache_.erase(it) : std::next(it);
    }
    targetCount_ = views.size();
    const auto worst = kachakacha::v2::app::WorstDevelopability(data);
    developability_ = worst.labelJa.empty() ? std::string()
        : worst.labelJa + "(伸び縮みの目安 " + Millimeters(worst.strain * 100.0) + " %)";
    if (!views.empty() || !window_.viewport_->AnalysisViews().empty()) {
        window_.viewport_->SetAnalysisViews(std::move(views));
    }
    RefreshDock(targets, notes, data);
}

void V2SurfaceAnalysisTool::RefreshDock(const std::vector<Target>& targets,
    const std::vector<std::string>& notes, const std::vector<const SurfaceAnalysisData*>& data)
{
    if (!shown_) {
        return;
    }
    QString targetText;
    if (mode_ == SurfaceAnalysisMode::None) {
        targetText = QStringLiteral("見るものを選ぶと、選んだ面(選んでいなければ全部の面)と下見を塗ります。");
    } else if (targets.empty()) {
        targetText = QStringLiteral("塗れる面がありません。3D で面を選ぶか、面を作ってください。");
    } else {
        std::string text = std::to_string(notes.size()) + " 枚を塗っています";
        text += targets.size() >= kMaxTargets ? "(多いので先頭の " + std::to_string(kMaxTargets)
                + " 枚まで。見たい面を選ぶと、その面だけ塗ります)。"
                                              : "。";
        for (const std::string& note : notes) {
            text += "\n・" + note;
        }
        targetText = Text(text);
    }
    std::vector<QString> legend;
    for (const std::string& line : kachakacha::v2::app::AnalysisLegendJa(mode_, data)) {
        legend.push_back(Text(line));
    }
    dock_->ShowState(mode_, targetText, legend);
}
