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

//! 材料・積層などの製作の属性を付ける(V1 の板材の「材料」「積層」)。
//! 部品・形状ガイド・近似モデルにだけ付く。線や点には意味が無いので断る(DOC-C010)。
class SetManufacturingCommand final : public DocumentCommand {
public:
    SetManufacturingCommand(std::vector<EntityId> entityIds,
        domain::ManufacturingProperties properties);
    [[nodiscard]] std::string Label() const override { return "材料を決める"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    std::vector<EntityId> entityIds_;
    domain::ManufacturingProperties properties_;
};

//! 補助線にする / 戻す(V1の「補助線として作図」「補助線化」)。
//! 幾何は変えない。作図の下敷きとして扱うかどうかだけを変える。
class SetConstructionCommand final : public DocumentCommand {
public:
    SetConstructionCommand(std::vector<EntityId> entityIds, bool construction);
    [[nodiscard]] std::string Label() const override
    {
        return construction_ ? "補助線にする" : "補助線をやめる";
    }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    std::vector<EntityId> entityIds_;
    bool construction_ = true;
};

//! 作業中グループを決める(AT-UIX-006)。
//! これから作る利用者作成Entityは、このグループへ入る。
//! 派生物(部品・形状ガイド・型紙)は Feature の派生グループへ入るので、
//! ここを切り替えても居場所が変わらない。
class SetActiveGroupCommand final : public DocumentCommand {
public:
    //! 値を渡さなければ「どのグループにも入れない」に戻す。
    explicit SetActiveGroupCommand(std::optional<GroupId> groupId);
    [[nodiscard]] std::string Label() const override { return "作業中グループを決める"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    std::optional<GroupId> groupId_;
};

//! 基準線にする / 解除する(V1の「基準線に設定」「基準解除」)。
class SetDatumCommand final : public DocumentCommand {
public:
    SetDatumCommand(std::vector<EntityId> entityIds, bool datum);
    [[nodiscard]] std::string Label() const override
    {
        return datum_ ? "基準線にする" : "基準線をやめる";
    }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    std::vector<EntityId> entityIds_;
    bool datum_ = true;
};

//! 製作の「生成」で作ったものに、どの近似モデルから作ったかを付ける(F-15)。
//! 由来は一覧の並べ方のためのもので、依存ではない(Entity::generatedFrom)。
//! 相手は近似モデルだけ。近似モデル自身や作業平面には付けない。
class SetGeneratedFromCommand final : public DocumentCommand {
public:
    SetGeneratedFromCommand(std::vector<EntityId> entityIds, EntityId sourceModelId);
    [[nodiscard]] std::string Label() const override { return "生成物の由来を付ける"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    std::vector<EntityId> entityIds_;
    EntityId sourceModelId_;
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

//! 測った結果を文書へ残す(V1の「残した参照寸法」)。
class AddReferenceDimensionCommand final : public DocumentCommand {
public:
    explicit AddReferenceDimensionCommand(ReferenceDimension dimension);
    [[nodiscard]] std::string Label() const override { return "寸法を残す"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    ReferenceDimension dimension_;
};

class RemoveReferenceDimensionCommand final : public DocumentCommand {
public:
    explicit RemoveReferenceDimensionCommand(base::DimensionId id);
    [[nodiscard]] std::string Label() const override { return "残した寸法を消す"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    base::DimensionId id_;
};

//! グループ操作。
class AddGroupCommand final : public DocumentCommand {
public:
    AddGroupCommand(Group group);
    [[nodiscard]] std::string Label() const override { return "グループを作る"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    Group group_;
};

class MoveEntitiesToGroupCommand final : public DocumentCommand {
public:
    MoveEntitiesToGroupCommand(std::vector<EntityId> entityIds,
        std::optional<GroupId> groupId);
    [[nodiscard]] std::string Label() const override { return "グループへ入れる"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    std::vector<EntityId> entityIds_;
    std::optional<GroupId> groupId_;
};

//! グループの名前を変える。中身には触らない。
class RenameGroupCommand final : public DocumentCommand {
public:
    RenameGroupCommand(GroupId groupId, std::string displayName);
    [[nodiscard]] std::string Label() const override { return "グループの名前を変える"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    GroupId groupId_;
    std::string displayName_;
};

//! グループを別のグループの下へ移す(入れ子)。中身は一緒に付いていく。
//!
//! 輪(自分の子孫の下へ移す)は断る。輪ができると、木を辿るときに終わらない。
class SetGroupParentCommand final : public DocumentCommand {
public:
    SetGroupParentCommand(GroupId groupId, std::optional<GroupId> parentId);
    [[nodiscard]] std::string Label() const override { return "グループを移す"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    GroupId groupId_;
    std::optional<GroupId> parentId_;
};

//! グループごと出す/隠す。**中身の visibility は書き換えない。**
class SetGroupVisibilityCommand final : public DocumentCommand {
public:
    SetGroupVisibilityCommand(GroupId groupId, bool visible);
    [[nodiscard]] std::string Label() const override
    {
        return visible_ ? "グループを出す" : "グループを隠す";
    }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    GroupId groupId_;
    bool visible_ = true;
};

//! グループを消す。中の物は親のグループへ移す(消さない)。
class RemoveGroupCommand final : public DocumentCommand {
public:
    explicit RemoveGroupCommand(GroupId groupId);
    [[nodiscard]] std::string Label() const override { return "グループを消す"; }
    [[nodiscard]] std::vector<Diagnostic> Apply(DocumentSnapshot& candidate) const override;

private:
    GroupId groupId_;
};

} // namespace kachakacha::v2::document
