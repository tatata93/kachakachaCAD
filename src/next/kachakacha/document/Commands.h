#pragma once

//! 具体的な DocumentCommand。UIも保存も、文書を変えるときは必ずここを通る。

#include "kachakacha/document/Document.h"

namespace kachakacha::v2::document {

//! 1つのFeatureと、その出力Entityを足す。
class AddFeatureCommand final : public DocumentCommand {
public:
    AddFeatureCommand(Feature feature, std::vector<Entity> outputs, std::string label);
    [[nodiscard]] std::string Label() const override { return label_; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    Feature feature_;
    std::vector<Entity> outputs_;
    std::string label_;
};

//! 下流をどう扱うか。黙って消さない。
enum class RemovePolicy {
    //! 下流があるなら断る。
    RefuseIfUsed,
    //! 下流もまとめて消す。プレビューで件数を見せてから使う。
    Cascade,
};

class RemoveFeatureCommand final : public DocumentCommand {
public:
    RemoveFeatureCommand(FeatureId featureId, RemovePolicy policy, std::string label);
    [[nodiscard]] std::string Label() const override { return label_; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    FeatureId featureId_;
    RemovePolicy policy_ = RemovePolicy::RefuseIfUsed;
    std::string label_;
};

//! 表示名を変える。名前は表示用なので、重複してよい。
class RenameEntityCommand final : public DocumentCommand {
public:
    RenameEntityCommand(EntityId entityId, std::string newName);
    [[nodiscard]] std::string Label() const override { return "名前を変える"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    EntityId entityId_;
    std::string newName_;
};

class SetVisibilityCommand final : public DocumentCommand {
public:
    SetVisibilityCommand(std::vector<EntityId> entityIds, domain::Visibility visibility);
    [[nodiscard]] std::string Label() const override { return "表示を変える"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    std::vector<EntityId> entityIds_;
    domain::Visibility visibility_ = domain::Visibility::Visible;
};

//! Featureを無効にする(消さずに効かなくする)。下流は SuppressedInput になる。
class SetFeatureEnabledCommand final : public DocumentCommand {
public:
    SetFeatureEnabledCommand(FeatureId featureId, bool enabled);
    [[nodiscard]] std::string Label() const override
    {
        return enabled_ ? "操作を有効にする" : "操作を無効にする";
    }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    FeatureId featureId_;
    bool enabled_ = true;
};

//! Featureの定義を差し替える。IDと出力の対応は保つ(DOC-011)。
class UpdateFeatureDefinitionCommand final : public DocumentCommand {
public:
    UpdateFeatureDefinitionCommand(FeatureId featureId,
        domain::FeatureDefinition definition, std::vector<EntityId> inputEntityIds,
        std::string label);
    [[nodiscard]] std::string Label() const override { return label_; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    FeatureId featureId_;
    domain::FeatureDefinition definition_;
    std::vector<EntityId> inputEntityIds_;
    std::string label_;
};

//! グループ操作。
class AddGroupCommand final : public DocumentCommand {
public:
    AddGroupCommand(Group group);
    [[nodiscard]] std::string Label() const override { return "まとまりを作る"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    Group group_;
};

class MoveEntitiesToGroupCommand final : public DocumentCommand {
public:
    MoveEntitiesToGroupCommand(std::vector<EntityId> entityIds,
        std::optional<GroupId> groupId);
    [[nodiscard]] std::string Label() const override { return "まとまりへ入れる"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    std::vector<EntityId> entityIds_;
    std::optional<GroupId> groupId_;
};

//! まとまりを消す。中の物は親のまとまりへ移す(消さない)。
class RemoveGroupCommand final : public DocumentCommand {
public:
    explicit RemoveGroupCommand(GroupId groupId);
    [[nodiscard]] std::string Label() const override { return "まとまりを消す"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    GroupId groupId_;
};

} // namespace kachakacha::v2::document
