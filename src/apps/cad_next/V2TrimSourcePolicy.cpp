#include "V2TrimSourcePolicy.h"
#include "V2DeletionConfirmation.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "kachakacha/document/Commands.h"
#include <QCheckBox>
#include <QStringList>
#include <QString>
#include <QWidget>
#include <algorithm>

namespace {
using namespace kachakacha::v2;
class TrimRemoval final : public document::DocumentCommand {
public:
    explicit TrimRemoval(document::DocumentSnapshot after) : after_(std::move(after)) {}
    std::string Label() const override { return "トリム前の線を削除"; }
    std::vector<base::Diagnostic> Apply(document::DocumentSnapshot& current) const override {
        if (current.id != after_.id || current.revision != after_.revision)
            return {base::MakeError("DOC-C001", "削除の確認中に文書が変わりました。やり直してください。")};
        current = after_;
        return {};
    }
private:
    document::DocumentSnapshot after_;
};
}

v2ui::TrimGenerationTransaction::TrimGenerationTransaction(app::DrawingSession& session, const std::string& label)
    : session_(session), scene_(session.Scene()),
      transaction_(std::make_unique<document::Document::Transaction>(session.GetDocument(), label)) {}

v2ui::TrimGenerationTransaction::~TrimGenerationTransaction()
{
    transaction_.reset(); // Restore the document before restoring its drawing scene.
    if (!committed_ && !rolledBack_) session_.SetScene(std::move(scene_));
}

bool v2ui::TrimGenerationTransaction::Commit()
{
    if (!transaction_) return false;
    committed_ = transaction_->Commit();
    return committed_;
}

void v2ui::TrimGenerationTransaction::Rollback()
{
    transaction_.reset(); session_.SetScene(scene_); rolledBack_ = true;
}

QCheckBox* v2ui::MakeTrimKeepOriginal(QWidget* parent, const char* name)
{
    auto* check = new QCheckBox(QStringLiteral("元の線を残す"), parent);
    check->setObjectName(QString::fromLatin1(name));
    check->setChecked(false);
    check->setToolTip(QStringLiteral("オン: 変更前の線を表示したまま残し、結果を別ワイヤーにします。\nオフ: 赤い下見区間だけを削除し、残りの直線やRは保ちます。\n線全体や寸法も削除する場合は、一覧と3D表示で確認します。"));
    return check;
}

bool v2ui::TrimKeepsOriginal(const QWidget& owner, const char* name)
{
    const auto* check = owner.findChild<QCheckBox*>(QString::fromLatin1(name));
    return check && check->isChecked();
}

v2ui::TrimSourceRemoval v2ui::PrepareTrimSourceRemoval(V2MainWindow& window,
    const std::vector<base::EntityId>& sources, bool keep,
    const std::function<void()>& refreshShapes)
{
    TrimSourceRemoval result;
    if (keep || sources.empty()) return result;
    const auto before = window.Session().GetDocument().Snapshot();
    auto after = before;
    for (const auto id : sources) {
        const auto found = std::find_if(after.entities.begin(), after.entities.end(), [id](const auto& e) { return e.id == id; });
        if (found == after.entities.end()) continue;
        const auto owner = std::find_if(after.features.begin(), after.features.end(), [&](const auto& f) { return f.id == found->createdBy; });
        if (owner != after.features.end() && owner->outputs.size() != 1) {
            result.accepted = false; result.message = QStringLiteral("共有出力のほかの線を消さないため、この削除は取り消しました。"); return result;
        }
        // Explicit geometry is already baked. Legacy fillets carried historical wire
        // inputs despite not reevaluating from them. Removing a source must not erase these curves.
        for (auto& feature : after.features) if (feature.id != found->createdBy
            && std::holds_alternative<domain::CreateWireDefinition>(feature.definition)) {
            const auto oldSize = feature.inputEntityIds.size();
            std::erase(feature.inputEntityIds, id);
            if (oldSize != feature.inputEntityIds.size()) ++feature.revision;
        }
        const auto diagnostics = document::RemoveFeatureCommand(found->createdBy, document::RemovePolicy::Cascade, "トリム前の線を削除").Apply(after);
        for (const auto& d : diagnostics) if (d.IsError()) {
            result.accepted = false; result.message = QString::fromStdString(d.summaryJa); return result;
        }
    }
    const auto gone = [&](base::EntityId id) {
        return std::none_of(after.entities.begin(), after.entities.end(), [id](const auto& e) { return e.id == id; });
    };
    QStringList names;
    std::vector<base::EntityId> preview;
    bool dependent = false;
    for (const auto& entity : before.entities) if (gone(entity.id)) {
        names.push_back(QString::fromStdString(entity.displayName) + " [" + QString::fromStdString(entity.id.ShortSuffix()) + "]");
        preview.push_back(entity.id);
        if (std::find(sources.begin(), sources.end(), entity.id) == sources.end()) dependent = true;
    }
    std::erase_if(after.referenceDimensions, [&](const auto& d) {
        const bool affected = std::any_of(d.targets.begin(), d.targets.end(), gone)
            || std::any_of(d.segments.begin(), d.segments.end(), [&](const auto& s) { return gone(s.entityId); });
        if (affected) { names.push_back(QStringLiteral("寸法: ") + QString::fromStdString(d.label)); dependent = true; }
        return affected;
    });
    if (dependent) {
        window.Viewport().SetDeletionPreview(preview); refreshShapes();
        const int choice = ConfirmDeletion(&window, names);
        window.Viewport().SetDeletionPreview({}); refreshShapes();
        if (choice < 0) {
            result.accepted = false;
            result.message = QStringLiteral("削除を取り消しました。トリム前の線と結果は変更していません。");
            return result;
        }
    }
    if (window.Session().GetDocument().Revision() != before.revision) {
        result.accepted = false;
        result.message = QStringLiteral("確認中に文書が変わりました。トリムをやり直してください。");
        return result;
    }
    result.command = std::make_unique<TrimRemoval>(std::move(after));
    return result;
}

namespace {
bool SameCurve(const geometry::CurveSegment& a, const geometry::CurveSegment& b)
{
    return a.Kind() == b.Kind() && a.ControlPoints() == b.ControlPoints()
        && a.Center() == b.Center() && a.Normal() == b.Normal()
        && a.ReferenceDirection() == b.ReferenceDirection() && a.Radius() == b.Radius()
        && a.StartAngleRad() == b.StartAngleRad() && a.SweepAngleRad() == b.SweepAngleRad();
}
domain::CreateWireDefinition ReplacementWire(const domain::CreateWireDefinition& original,
    const std::vector<geometry::CurveSegment>& chain, base::IdGenerator& ids)
{
    auto result = original; result.segments = chain; result.segmentIds.assign(chain.size(), {});
    std::vector<bool> used(original.segments.size(), false);
    for (std::size_t i = 0; i < chain.size(); ++i) for (std::size_t j = 0; j < original.segments.size(); ++j)
        if (!used[j] && j < original.segmentIds.size() && SameCurve(chain[i], original.segments[j])) {
            result.segmentIds[i] = original.segmentIds[j]; used[j] = true; break;
        }
    // A changed fragment is a new segment. Never transfer an unrelated old ID by order.
    for (auto& id : result.segmentIds) if (id.IsNil()) id = ids.NextTyped<base::IdKind::Segment>();
    return result;
}
}

namespace {
bool NativeConsumer(const document::DocumentSnapshot& snapshot, base::EntityId source) {
    return std::any_of(snapshot.features.begin(), snapshot.features.end(), [&](const auto& f) {
        return std::find(f.inputEntityIds.begin(), f.inputEntityIds.end(), source) != f.inputEntityIds.end()
            && !std::holds_alternative<domain::CreateWireDefinition>(f.definition)
            && !std::holds_alternative<domain::CreatePointDefinition>(f.definition);
    });
}
void RemapDimensions(document::DocumentSnapshot& after, base::EntityId source,
    const std::vector<std::pair<base::SegmentId, base::EntityId>>& owners, QStringList& removed) {
    std::erase_if(after.referenceDimensions, [&](auto& d) {
        bool missing = false, changed = false; std::vector<base::EntityId> targets;
        for (auto& ref : d.segments) if (ref.entityId == source) {
            const auto owner = std::find_if(owners.begin(), owners.end(), [&](const auto& o) { return o.first == ref.segmentId; });
            if (owner == owners.end()) missing = true;
            else { ref.entityId = owner->second; changed = true; targets.push_back(owner->second); }
        }
        if (missing) removed.push_back(QStringLiteral("寸法: ") + QString::fromStdString(d.label));
        if (changed && !missing) {
            std::erase(d.targets, source);
            for (const auto id : targets) if (std::find(d.targets.begin(), d.targets.end(), id) == d.targets.end()) d.targets.push_back(id);
        }
        return missing;
    });
}
}
v2ui::TrimSourceRemoval v2ui::PrepareTrimSourceReplacement(V2MainWindow& window,
    base::EntityId source, const std::vector<std::vector<geometry::CurveSegment>>& chains,
    bool keep, base::IdGenerator& ids, const std::function<void()>& refreshShapes)
{
    if (keep || chains.empty()) return PrepareTrimSourceRemoval(window, {source}, keep, refreshShapes);
    TrimSourceRemoval result;
    const auto before = window.Session().GetDocument().Snapshot(); auto after = before;
    auto entity = std::find_if(after.entities.begin(), after.entities.end(), [&](const auto& e) { return e.id == source; });
    auto feature = entity == after.entities.end() ? after.features.end()
        : std::find_if(after.features.begin(), after.features.end(), [&](const auto& f) { return f.id == entity->createdBy; });
    if (feature == after.features.end() || feature->outputs.size() != 1) {
        result.accepted = false; result.message = QStringLiteral("この共有出力は他の線を変えずにトリムできません。何も変更していません。"); return result;
    }
    domain::CreateWireDefinition original;
    if (const auto* baked = std::get_if<domain::CreateWireDefinition>(&feature->definition)) original = *baked;
    else {
        for (const auto& curve : window.Session().Scene().curves) if (curve.entityId == source) {
            original.segments.push_back(curve.segment); original.segmentIds.push_back(curve.segmentId);
        }
        original.construction = entity->construction;
        feature->type = domain::FeatureType::TransformWire; feature->inputEntityIds.clear();
        entity->editPolicy = domain::EditPolicy::Source;
    }
    std::vector<geometry::CurveSegment> flat;for (const auto& chain : chains) flat.insert(flat.end(), chain.begin(), chain.end());
    const auto all = ReplacementWire(original, flat, ids);
    const auto prototype = *entity; const auto owner = feature->id;
    std::vector<std::pair<base::SegmentId, base::EntityId>> segmentOwners;
    std::size_t offset = 0;
    for (std::size_t i = 0; i < chains.size(); ++i) {
        auto wire = original; wire.segments = chains[i];
        wire.segmentIds.assign(all.segmentIds.begin() + offset, all.segmentIds.begin() + offset + chains[i].size()); offset += chains[i].size();
        auto output = prototype;
        if (i == 0) { feature->definition = wire; ++feature->revision; ++entity->revision; }
        else {
            domain::Feature added; added.id = ids.NextTyped<base::IdKind::Feature>(); added.type = domain::FeatureType::TransformWire;
            added.displayName = "トリム"; added.definition = wire;
            output.id = ids.NextTyped<base::IdKind::Entity>(); output.createdBy = added.id;
            output.displayName += " " + std::to_string(i + 1); added.outputs = {{"wire", output.id, output.kind}};
            after.features.push_back(std::move(added)); after.entities.push_back(output);
            // Appending invalidates iterators; look up the original on the next pass only when needed.
            feature = std::find_if(after.features.begin(), after.features.end(), [&](const auto& f) { return f.id == owner; });
            entity = std::find_if(after.entities.begin(), after.entities.end(), [&](const auto& e) { return e.id == source; });
        }
        for (const auto id : wire.segmentIds) segmentOwners.push_back({id, output.id});
    }
    QStringList dimensions; RemapDimensions(after, source, segmentOwners, dimensions);
    if (!dimensions.empty() && ConfirmDeletion(&window, dimensions) < 0) {
        result.accepted = false; result.message = QStringLiteral("寸法の削除を取り消しました。線は変更していません。"); return result;
    }
    result.reusesSource = true; result.rebuildShapes = NativeConsumer(before, source);
    result.command = std::make_unique<TrimRemoval>(std::move(after)); return result;
}
