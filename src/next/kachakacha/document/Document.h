#pragma once

//! Document。Feature DAG が正本で、Entity は識別と表示だけを持つ。
//!
//! 変更は必ず DocumentCommand を通す。entities/features の配列を直接書き換えない。
//! Command は候補スナップショットへ変更し、検証が通ってから commit する。
//! 失敗したら元の Document をそのまま残す(部分更新しない)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/geometry/GeometryTolerance.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kachakacha::v2::document {

using base::Diagnostic;
using base::DocumentId;
using base::EntityId;
using base::FeatureId;
using base::GroupId;
using domain::Entity;
using domain::EntityKind;
using domain::Feature;

struct Group {
    GroupId id;
    std::string displayName;
    std::optional<GroupId> parentId;
};

struct DocumentSettings {
    geometry::GeometryTolerance tolerance;
    std::optional<GroupId> activeGroupId;
};

//! 読み取り専用の断面。評価やワーカーへはこれを渡す。
struct DocumentSnapshot {
    DocumentId id;
    std::uint64_t revision = 0;
    DocumentSettings settings;
    std::vector<Group> groups;
    std::vector<Entity> entities;
    std::vector<Feature> features;
    //! Featureの評価順。依存の上流から並ぶ。
    std::vector<FeatureId> evaluationOrder;
};

//! commitされた1回の変更。Undo/Redoはこれを使う。
struct DocumentDelta {
    std::string label;      //!< 「直線を引く」などの表示用
    DocumentSnapshot before;
    DocumentSnapshot after;
};

struct CommandResult {
    bool committed = false;
    DocumentDelta delta;
    std::vector<Diagnostic> diagnostics;
};

class Document;

//! 文書を変える唯一の入口。
class DocumentCommand {
public:
    virtual ~DocumentCommand() = default;
    //! 表示用の名前。Undoの一覧に出る。
    [[nodiscard]] virtual std::string Label() const = 0;
    //! 候補スナップショットへ変更を書く。失敗したら診断を返す。
    [[nodiscard]] virtual std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const = 0;
};

class Document {
public:
    explicit Document(DocumentId id, DocumentSettings settings = {});

    [[nodiscard]] const DocumentSnapshot& Snapshot() const noexcept { return snapshot_; }
    [[nodiscard]] std::uint64_t Revision() const noexcept { return snapshot_.revision; }

    //! Commandを走らせる。検証まで通ったときだけ文書が変わる。
    [[nodiscard]] CommandResult Run(const DocumentCommand& command);

    //! 連続ドラッグを1つのUndo単位にまとめる。Begin と End で挟む。
    void BeginCompound(std::string label);
    void EndCompound();
    [[nodiscard]] bool InCompound() const noexcept { return compoundDepth_ > 0; }

    [[nodiscard]] bool CanUndo() const noexcept { return !undoStack_.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !redoStack_.empty(); }
    [[nodiscard]] std::string UndoLabel() const;
    [[nodiscard]] std::string RedoLabel() const;
    bool Undo();
    bool Redo();

    //! ファイルを開く・新規作成・保存はUndo履歴の境界にする。
    void MarkHistoryBoundary();

    // --- 検索 ---
    [[nodiscard]] const Entity* FindEntity(EntityId id) const;
    [[nodiscard]] const Feature* FindFeature(FeatureId id) const;
    [[nodiscard]] std::vector<EntityId> EntitiesOfKind(EntityKind kind) const;

    //! そのEntityを入力にしているFeature(=下流)。削除の影響範囲に使う。
    [[nodiscard]] std::vector<FeatureId> DownstreamFeatures(EntityId id) const;

    //! 構造の検証。壊れた参照・重複ID・循環を見つける。
    [[nodiscard]] static std::vector<Diagnostic> Validate(const DocumentSnapshot& snapshot);

    //! 依存の上流から並べ直す。循環があると空を返す。
    [[nodiscard]] static std::vector<FeatureId> TopologicalOrder(
        const DocumentSnapshot& snapshot);

private:
    DocumentSnapshot snapshot_;
    std::vector<DocumentDelta> undoStack_;
    std::vector<DocumentDelta> redoStack_;
    int compoundDepth_ = 0;
    std::string compoundLabel_;
    std::optional<DocumentSnapshot> compoundBefore_;
};

} // namespace kachakacha::v2::document
