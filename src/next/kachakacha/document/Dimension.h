#pragma once
#include "kachakacha/document/Document.h"
namespace kachakacha::v2::document {
struct DimensionValue {
    double value = 0;
    std::vector<geometry::Vector3> anchors;
};
base::Result<DimensionValue> EvaluateDimension(const DocumentSnapshot&, const ReferenceDimension&);
std::vector<base::Diagnostic> ValidateDrivingDimensions(const DocumentSnapshot&);
//! 新規・編集とも1回のUndo。失敗なら形と寸法を一切変更しない。
class SetDimensionCommand final : public DocumentCommand {
public:
    explicit SetDimensionCommand(ReferenceDimension dimension) : dimension_(std::move(dimension)) {}
    std::string Label() const override { return "寸法を設定"; }
    std::vector<base::Diagnostic> Apply(DocumentSnapshot&) const override;
private:
    ReferenceDimension dimension_;
};
base::Result<bool> SolveDimensions(DocumentSnapshot&, const ReferenceDimension& changed);
} // namespace kachakacha::v2::document
