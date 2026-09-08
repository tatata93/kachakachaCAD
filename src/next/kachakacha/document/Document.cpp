#include "kachakacha/document/Document.h"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace kachakacha::v2::document {

using base::MakeError;

namespace {

constexpr const char* kDuplicateEntity = "DOC-V001";
constexpr const char* kDuplicateFeature = "DOC-V002";
constexpr const char* kBrokenReference = "DOC-V003";
constexpr const char* kCycle = "DOC-V004";
constexpr const char* kOrphanEntity = "DOC-V005";
constexpr const char* kEmptyId = "DOC-V006";

} // namespace

Document::Document(DocumentId id, DocumentSettings settings)
{
    snapshot_.id = id;
    snapshot_.settings = std::move(settings);
    snapshot_.revision = 1;
}

// ---------------- 検証 ----------------

std::vector<Diagnostic> Document::Validate(const DocumentSnapshot& snapshot)
{
    std::vector<Diagnostic> diagnostics;

    std::unordered_set<std::string> entityIds;
    for (const Entity& entity : snapshot.entities) {
        if (entity.id.IsNil()) {
            diagnostics.push_back(MakeError(kEmptyId,
                "IDの無いオブジェクトがあります。", entity.displayName));
            continue;
        }
        if (!entityIds.insert(entity.id.ToString()).second) {
            diagnostics.push_back(MakeError(kDuplicateEntity,
                "同じIDのオブジェクトが2つあります。", entity.id.ToString()));
        }
    }

    std::unordered_set<std::string> featureIds;
    for (const Feature& feature : snapshot.features) {
        if (feature.id.IsNil()) {
            diagnostics.push_back(MakeError(kEmptyId,
                "IDの無い操作履歴があります。", feature.displayName));
            continue;
        }
        if (!featureIds.insert(feature.id.ToString()).second) {
            diagnostics.push_back(MakeError(kDuplicateFeature,
                "同じIDの操作履歴が2つあります。", feature.id.ToString()));
        }
    }

    // 入力参照が実在すること。最寄りへ勝手に付け替えない。
    for (const Feature& feature : snapshot.features) {
        for (const EntityId& input : feature.inputEntityIds) {
            if (entityIds.find(input.ToString()) == entityIds.end()) {
                diagnostics.push_back(MakeError(kBrokenReference,
                    "操作履歴が、もう無いものを参照しています。",
                    feature.displayName + " → " + input.ToString()));
            }
        }
        for (const domain::FeatureOutput& output : feature.outputs) {
            if (entityIds.find(output.entityId.ToString()) == entityIds.end()) {
                diagnostics.push_back(MakeError(kBrokenReference,
                    "操作履歴の出力が見つかりません。",
                    feature.displayName + " → " + output.key));
            }
        }
    }

    // すべての幾何Entityは、いずれかのFeatureの出力であること。
    for (const Entity& entity : snapshot.entities) {
        if (entity.createdBy.IsNil()
            || featureIds.find(entity.createdBy.ToString()) == featureIds.end()) {
            diagnostics.push_back(MakeError(kOrphanEntity,
                "どの操作からも作られていないオブジェクトがあります。",
                entity.displayName + " (" + entity.id.ToString() + ")"));
        }
    }

    if (TopologicalOrder(snapshot).size() != snapshot.features.size()) {
        diagnostics.push_back(MakeError(kCycle,
            "操作履歴が循環しています。",
            "ある操作が、自分の下流の結果を入力にしています。"));
    }
    return diagnostics;
}

std::vector<FeatureId> Document::TopologicalOrder(const DocumentSnapshot& snapshot)
{
    // EntityId -> それを作ったFeatureId
    std::unordered_map<std::string, std::string> producer;
    for (const Feature& feature : snapshot.features) {
        for (const domain::FeatureOutput& output : feature.outputs) {
            producer[output.entityId.ToString()] = feature.id.ToString();
        }
    }
    // 決定的にするため、FeatureId順に並べてから走査する。
    std::vector<const Feature*> ordered;
    ordered.reserve(snapshot.features.size());
    for (const Feature& feature : snapshot.features) {
        ordered.push_back(&feature);
    }
    std::sort(ordered.begin(), ordered.end(),
        [](const Feature* l, const Feature* r) { return l->id < r->id; });

    std::unordered_map<std::string, int> remaining;
    std::unordered_map<std::string, std::vector<std::string>> downstream;
    for (const Feature* feature : ordered) {
        int count = 0;
        for (const EntityId& input : feature->inputEntityIds) {
            const auto found = producer.find(input.ToString());
            if (found == producer.end() || found->second == feature->id.ToString()) {
                continue;
            }
            ++count;
            downstream[found->second].push_back(feature->id.ToString());
        }
        remaining[feature->id.ToString()] = count;
    }

    std::unordered_map<std::string, const Feature*> byId;
    for (const Feature* feature : ordered) {
        byId[feature->id.ToString()] = feature;
    }

    // 入次数0のものを、ID順で取り出していく。
    std::set<std::string> ready;
    for (const auto& [id, count] : remaining) {
        if (count == 0) {
            ready.insert(id);
        }
    }
    std::vector<FeatureId> result;
    result.reserve(snapshot.features.size());
    while (!ready.empty()) {
        const std::string current = *ready.begin();
        ready.erase(ready.begin());
        result.push_back(byId[current]->id);
        for (const std::string& next : downstream[current]) {
            if (--remaining[next] == 0) {
                ready.insert(next);
            }
        }
    }
    return result;
}

// ---------------- Command ----------------

CommandResult Document::Run(const DocumentCommand& command)
{
    CommandResult result;
    // 候補へ書く。ここで失敗しても本体は無傷。
    DocumentSnapshot candidate = snapshot_;
    std::vector<Diagnostic> diagnostics = command.Apply(candidate);
    const bool applyFailed = std::any_of(diagnostics.begin(), diagnostics.end(),
        [](const Diagnostic& diagnostic) { return diagnostic.IsError(); });
    if (applyFailed) {
        result.diagnostics = std::move(diagnostics);
        return result;
    }

    std::vector<Diagnostic> structural = Validate(candidate);
    if (std::any_of(structural.begin(), structural.end(),
            [](const Diagnostic& diagnostic) { return diagnostic.IsError(); })) {
        diagnostics.insert(diagnostics.end(), structural.begin(), structural.end());
        result.diagnostics = std::move(diagnostics);
        return result;
    }
    diagnostics.insert(diagnostics.end(), structural.begin(), structural.end());

    candidate.evaluationOrder = TopologicalOrder(candidate);
    candidate.revision = snapshot_.revision + 1;

    DocumentDelta delta;
    delta.label = command.Label();
    delta.before = snapshot_;
    delta.after = candidate;

    if (compoundDepth_ > 0) {
        // まとめている間は履歴へ積まない。EndCompound で1つにする。
        snapshot_ = std::move(candidate);
    } else {
        undoStack_.push_back(delta);
        redoStack_.clear();
        snapshot_ = std::move(candidate);
    }

    result.committed = true;
    result.delta = std::move(delta);
    result.diagnostics = std::move(diagnostics);
    return result;
}

void Document::BeginCompound(std::string label)
{
    if (compoundDepth_ == 0) {
        compoundLabel_ = std::move(label);
        compoundBefore_ = snapshot_;
    }
    ++compoundDepth_;
}

void Document::EndCompound()
{
    if (compoundDepth_ == 0) {
        return;
    }
    --compoundDepth_;
    if (compoundDepth_ > 0 || !compoundBefore_.has_value()) {
        return;
    }
    // 中身が変わっていなければ履歴を汚さない。
    if (compoundBefore_->revision != snapshot_.revision) {
        DocumentDelta delta;
        delta.label = compoundLabel_;
        delta.before = *compoundBefore_;
        delta.after = snapshot_;
        undoStack_.push_back(std::move(delta));
        redoStack_.clear();
    }
    compoundBefore_.reset();
    compoundLabel_.clear();
}

std::string Document::UndoLabel() const
{
    return undoStack_.empty() ? std::string() : undoStack_.back().label;
}

std::string Document::RedoLabel() const
{
    return redoStack_.empty() ? std::string() : redoStack_.back().label;
}

bool Document::Undo()
{
    if (undoStack_.empty()) {
        return false;
    }
    DocumentDelta delta = undoStack_.back();
    undoStack_.pop_back();
    snapshot_ = delta.before;
    redoStack_.push_back(std::move(delta));
    return true;
}

bool Document::Redo()
{
    if (redoStack_.empty()) {
        return false;
    }
    DocumentDelta delta = redoStack_.back();
    redoStack_.pop_back();
    snapshot_ = delta.after;
    undoStack_.push_back(std::move(delta));
    return true;
}

std::vector<Diagnostic> Document::ResetTo(DocumentSnapshot snapshot)
{
    std::vector<Diagnostic> problems = Validate(snapshot);
    for (const Diagnostic& diagnostic : problems) {
        if (diagnostic.IsError()) {
            // 壊れたものを入れない。入れると、そのあとの操作が全部あてにならない。
            return problems;
        }
    }
    snapshot_ = std::move(snapshot);
    undoStack_.clear();
    redoStack_.clear();
    compoundDepth_ = 0;
    return problems;
}

void Document::MarkHistoryBoundary()
{
    undoStack_.clear();
    redoStack_.clear();
}

// ---------------- 検索 ----------------

const Entity* Document::FindEntity(EntityId id) const
{
    for (const Entity& entity : snapshot_.entities) {
        if (entity.id == id) {
            return &entity;
        }
    }
    return nullptr;
}

const Feature* Document::FindFeature(FeatureId id) const
{
    for (const Feature& feature : snapshot_.features) {
        if (feature.id == id) {
            return &feature;
        }
    }
    return nullptr;
}

std::vector<EntityId> Document::EntitiesOfKind(EntityKind kind) const
{
    std::vector<EntityId> ids;
    for (const Entity& entity : snapshot_.entities) {
        if (entity.kind == kind) {
            ids.push_back(entity.id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<FeatureId> Document::DownstreamFeatures(EntityId id) const
{
    // そのEntityから始めて、出力を入力にしているFeatureを辿る。
    std::unordered_map<std::string, std::vector<const Feature*>> consumers;
    for (const Feature& feature : snapshot_.features) {
        for (const EntityId& input : feature.inputEntityIds) {
            consumers[input.ToString()].push_back(&feature);
        }
    }
    std::set<std::string> seen;
    std::vector<EntityId> pending{id};
    std::vector<FeatureId> result;
    while (!pending.empty()) {
        const EntityId current = pending.back();
        pending.pop_back();
        for (const Feature* feature : consumers[current.ToString()]) {
            if (!seen.insert(feature->id.ToString()).second) {
                continue;
            }
            result.push_back(feature->id);
            for (const domain::FeatureOutput& output : feature->outputs) {
                pending.push_back(output.entityId);
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace kachakacha::v2::document
