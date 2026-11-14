#pragma once

#include "kachakacha/document/Document.h"

namespace v2ui {

bool IsTransformTarget(kachakacha::v2::domain::EntityKind kind);

kachakacha::v2::document::CommandResult TransformDrawingPoint(
    kachakacha::v2::document::Document& document, kachakacha::v2::base::IdGenerator& ids,
    kachakacha::v2::base::EntityId id, const kachakacha::v2::domain::TransformWireDefinition& transform,
    bool keepSource, const std::string& label);

}
