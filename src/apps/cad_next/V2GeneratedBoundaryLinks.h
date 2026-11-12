#pragma once

#include "kachakacha/document/Document.h"
#include <algorithm>
#include <utility>
#include <vector>

namespace v2ui {
// Provenance only. A generated wire remains editable and independent of its
// solid; ordinary history dependency rules do not change.
class BoundaryLinks final : public kachakacha::v2::document::DocumentCommand {
    using EntityId=kachakacha::v2::base::EntityId;
public:
    bool Add(EntityId wire) {
        if(wire.IsNil())return false;
        wires_.push_back(wire);return true;
    }
    [[nodiscard]] bool Attach(kachakacha::v2::document::Document& document,EntityId solid) {
        if(solid.IsNil()||wires_.empty())return true;
        solid_=solid;return document.Run(*this).committed;
    }
    std::string Label() const override { return "生成した境界ワイヤーの由来"; }
    std::vector<kachakacha::v2::base::Diagnostic> Apply(
        kachakacha::v2::document::DocumentSnapshot& snapshot) const override {
        using namespace kachakacha::v2;
        const auto find=[&](EntityId id){return std::find_if(snapshot.entities.begin(),snapshot.entities.end(),
            [&](const auto& entity){return entity.id==id;});};
        const auto part=find(solid_);
        if(part==snapshot.entities.end()||part->kind!=domain::EntityKind::Part)
            return {base::MakeError("DOC-C001","境界ワイヤーの生成元の立体が見つかりません。")};
        for(const auto id:wires_) {
            const auto wire=find(id);
            if(wire==snapshot.entities.end()||wire->kind!=domain::EntityKind::Wire||wire->generatedFrom)
                return {base::MakeError("DOC-C001","同時に生成した境界ワイヤーを確認できません。")};
            wire->generatedFrom=solid_;++wire->revision;
        }
        return {};
    }
private:
    EntityId solid_;
    std::vector<EntityId> wires_;
};
} // namespace v2ui
