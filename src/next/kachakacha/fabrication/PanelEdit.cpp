#include "kachakacha/fabrication/PanelEdit.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace kachakacha::v2::fabrication {
namespace {

using base::MakeError;
using base::Result;

constexpr const char* kNoSuchPiece = "FAB-G001";
constexpr const char* kNotAdjacent = "FAB-G002";
constexpr const char* kNothingToSplit = "FAB-G003";

//! その部材の中でいちばん大きい展開偏差。
[[nodiscard]] double WorstOf(const PanelPiece& piece,
    const std::vector<PanelCandidate>& panels)
{
    double worst = 0.0;
    for (const std::size_t index : piece.panelIndices) {
        if (index < panels.size()) {
            worst = std::max(worst, panels[index].flattenDeviationMm);
        }
    }
    return worst;
}

[[nodiscard]] double WorstOf(const PanelPartition& partition,
    const std::vector<PanelCandidate>& panels)
{
    double worst = 0.0;
    for (const auto& piece : partition.pieces) {
        worst = std::max(worst, WorstOf(piece, panels));
    }
    return worst;
}

[[nodiscard]] double AreaOf(const PanelPiece& piece,
    const std::vector<PanelCandidate>& panels)
{
    double area = 0.0;
    for (const std::size_t index : piece.panelIndices) {
        if (index < panels.size()) {
            area += panels[index].areaMm2;
        }
    }
    return area;
}

//! 2つの部材が、面のどこかで隣り合っているか。
[[nodiscard]] bool PiecesTouch(const PanelPiece& first, const PanelPiece& second,
    const std::vector<PanelAdjacency>& adjacencies)
{
    for (const auto& edge : adjacencies) {
        const bool a = std::find(first.panelIndices.begin(), first.panelIndices.end(),
                           edge.firstIndex)
                != first.panelIndices.end()
            && std::find(second.panelIndices.begin(), second.panelIndices.end(),
                   edge.secondIndex)
                != second.panelIndices.end();
        const bool b = std::find(first.panelIndices.begin(), first.panelIndices.end(),
                           edge.secondIndex)
                != first.panelIndices.end()
            && std::find(second.panelIndices.begin(), second.panelIndices.end(),
                   edge.firstIndex)
                != second.panelIndices.end();
        if (a || b) {
            return true;
        }
    }
    return false;
}

//! 部材の中身を数え直す。面積と偏差は面から出す。
void Recount(PanelPiece& piece, const std::vector<PanelCandidate>& panels)
{
    std::sort(piece.panelIndices.begin(), piece.panelIndices.end());
    piece.areaMm2 = AreaOf(piece, panels);
    piece.maximumDeviationMm = WorstOf(piece, panels);
}

//! 部材の番号を振り直す。並びは決定的にする。
void Renumber(PanelPartition& partition)
{
    for (std::size_t index = 0; index < partition.pieces.size(); ++index) {
        partition.pieces[index].pieceId = "部材" + std::to_string(index + 1);
    }
}

} // namespace

Result<PanelPartition> MergePieces(const PanelPartition& partition,
    const std::vector<PanelCandidate>& panels,
    const std::vector<PanelAdjacency>& adjacencies, std::size_t firstPiece,
    std::size_t secondPiece)
{
    using Out = Result<PanelPartition>;
    if (firstPiece >= partition.pieces.size() || secondPiece >= partition.pieces.size()
        || firstPiece == secondPiece) {
        return Out::Failure(MakeError(kNoSuchPiece, "その部材がありません。",
            "1つにする2つの部材を選んでください。"));
    }
    const std::size_t low = std::min(firstPiece, secondPiece);
    const std::size_t high = std::max(firstPiece, secondPiece);
    if (!PiecesTouch(partition.pieces[low], partition.pieces[high], adjacencies)) {
        return Out::Failure(MakeError(kNotAdjacent, "その2つは隣り合っていません。",
            "離れた2枚を1枚にすると、切り出しても組み立てられません。"));
    }
    PanelPartition made = partition;
    for (const std::size_t index : made.pieces[high].panelIndices) {
        made.pieces[low].panelIndices.push_back(index);
    }
    Recount(made.pieces[low], panels);
    made.pieces.erase(made.pieces.begin() + static_cast<std::ptrdiff_t>(high));
    // 1つになったところの境目は消える。**切れ目は消さない。** 別のものである。
    std::vector<PanelJoint> joints;
    for (const auto& joint : made.joints) {
        const bool insideNow = std::find(made.pieces[low].panelIndices.begin(),
                                   made.pieces[low].panelIndices.end(), joint.firstIndex)
                != made.pieces[low].panelIndices.end()
            && std::find(made.pieces[low].panelIndices.begin(),
                   made.pieces[low].panelIndices.end(), joint.secondIndex)
                != made.pieces[low].panelIndices.end();
        if (insideNow && joint.kind == JointKind::Separate) {
            PanelJoint folded = joint;
            folded.kind = JointKind::Fold;   // 1枚になったので、切り離さず折る。
            folded.matePairId.clear();
            joints.push_back(folded);
            continue;
        }
        joints.push_back(joint);
    }
    made.joints = std::move(joints);
    Renumber(made);
    return Out::Success(std::move(made));
}

PanelEditPreview PreviewMerge(const PanelPartition& partition,
    const std::vector<PanelCandidate>& panels,
    const std::vector<PanelAdjacency>& adjacencies, std::size_t firstPiece,
    std::size_t secondPiece)
{
    PanelEditPreview preview;
    preview.pieceCountBefore = partition.pieces.size();
    preview.maximumDeviationBeforeMm = WorstOf(partition, panels);
    const auto merged = MergePieces(partition, panels, adjacencies, firstPiece,
        secondPiece);
    if (!merged.HasValue()) {
        preview.pieceCountAfter = preview.pieceCountBefore;
        preview.maximumDeviationAfterMm = preview.maximumDeviationBeforeMm;
        preview.messageJa = merged.Diagnostics().empty()
            ? std::string("1つにできません。")
            : merged.Diagnostics().front().summaryJa;
        return preview;
    }
    preview.possible = true;
    preview.pieceCountAfter = merged.Value().pieces.size();
    preview.maximumDeviationAfterMm = WorstOf(merged.Value(), panels);
    preview.messageJa = DescribePanelEditJa(preview);
    return preview;
}

Result<PanelPartition> SplitPiece(const PanelPartition& partition,
    const std::vector<PanelCandidate>& panels, std::size_t pieceIndex,
    const std::vector<std::size_t>& movedPanelIndices)
{
    using Out = Result<PanelPartition>;
    if (pieceIndex >= partition.pieces.size()) {
        return Out::Failure(MakeError(kNoSuchPiece, "その部材がありません。", {}));
    }
    const auto& source = partition.pieces[pieceIndex];
    std::vector<std::size_t> moved;
    for (const std::size_t index : movedPanelIndices) {
        if (std::find(source.panelIndices.begin(), source.panelIndices.end(), index)
            != source.panelIndices.end()) {
            moved.push_back(index);
        }
    }
    std::sort(moved.begin(), moved.end());
    moved.erase(std::unique(moved.begin(), moved.end()), moved.end());
    if (moved.empty() || moved.size() == source.panelIndices.size()) {
        return Out::Failure(MakeError(kNothingToSplit, "そこでは分けられません。",
            "分けたい面を、その部材の中から1枚以上、全部より少なく選んでください。"));
    }
    PanelPartition made = partition;
    PanelPiece kept;
    kept.pieceId = source.pieceId;
    for (const std::size_t index : source.panelIndices) {
        if (std::find(moved.begin(), moved.end(), index) == moved.end()) {
            kept.panelIndices.push_back(index);
        }
    }
    PanelPiece split;
    split.panelIndices = moved;
    Recount(kept, panels);
    Recount(split, panels);
    made.pieces[pieceIndex] = std::move(kept);
    made.pieces.insert(made.pieces.begin() + static_cast<std::ptrdiff_t>(pieceIndex) + 1,
        std::move(split));
    // 分けたところは、折るのではなく貼り合わせになる。
    for (auto& joint : made.joints) {
        const bool across = (std::find(moved.begin(), moved.end(), joint.firstIndex)
                                != moved.end())
            != (std::find(moved.begin(), moved.end(), joint.secondIndex) != moved.end());
        if (across && joint.kind == JointKind::Fold) {
            joint.kind = JointKind::Separate;
            joint.matePairId = "分割" + std::to_string(joint.firstIndex) + "-"
                + std::to_string(joint.secondIndex);
        }
    }
    Renumber(made);
    return Out::Success(std::move(made));
}

PanelEditPreview PreviewSplit(const PanelPartition& partition,
    const std::vector<PanelCandidate>& panels, std::size_t pieceIndex,
    const std::vector<std::size_t>& movedPanelIndices)
{
    PanelEditPreview preview;
    preview.pieceCountBefore = partition.pieces.size();
    preview.maximumDeviationBeforeMm = WorstOf(partition, panels);
    const auto split = SplitPiece(partition, panels, pieceIndex, movedPanelIndices);
    if (!split.HasValue()) {
        preview.pieceCountAfter = preview.pieceCountBefore;
        preview.maximumDeviationAfterMm = preview.maximumDeviationBeforeMm;
        preview.messageJa = split.Diagnostics().empty()
            ? std::string("分けられません。")
            : split.Diagnostics().front().summaryJa;
        return preview;
    }
    preview.possible = true;
    preview.pieceCountAfter = split.Value().pieces.size();
    preview.maximumDeviationAfterMm = WorstOf(split.Value(), panels);
    preview.messageJa = DescribePanelEditJa(preview);
    return preview;
}

std::string DescribePanelEditJa(const PanelEditPreview& preview)
{
    if (!preview.possible) {
        return preview.messageJa;
    }
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(2);
    text << preview.pieceCountBefore << " 部材 最大 " << preview.maximumDeviationBeforeMm
         << " mm → " << preview.pieceCountAfter << " 部材 最大 "
         << preview.maximumDeviationAfterMm << " mm";
    // 誤差が増えても禁止しない。決めるのは作る人である。
    if (preview.maximumDeviationAfterMm > preview.maximumDeviationBeforeMm) {
        text << "。ずれは増えますが、接着線は減ります。";
    } else if (preview.pieceCountAfter > preview.pieceCountBefore) {
        text << "。ずれは減りますが、貼り合わせが増えます。";
    }
    return text.str();
}

} // namespace kachakacha::v2::fabrication
