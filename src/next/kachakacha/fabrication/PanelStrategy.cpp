#include "kachakacha/fabrication/PanelStrategy.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <string>

namespace kachakacha::v2::fabrication {

using base::Diagnostic;
using base::MakeError;
using base::MakeInformation;
using base::MakeWarning;
using base::Result;

namespace {

constexpr const char* kNotConnected = "FAB-P010";
constexpr const char* kCannotBeOnePiece = "FAB-P011";
constexpr const char* kBadInput = "FAB-P012";
constexpr const char* kSplitForCurvature = "FAB-P013";

//! 1枚のまま展開してよい面か。
[[nodiscard]] bool IsDevelopable(const PanelCandidate& panel,
    const FabricationSettings& settings)
{
    switch (panel.classification) {
    case PanelGeometryClass::Planar:
        return settings.allowedPanelTypes.planar;
    case PanelGeometryClass::Cylindrical:
        return settings.allowedPanelTypes.cylindrical;
    case PanelGeometryClass::Conical:
        return settings.allowedPanelTypes.conical;
    case PanelGeometryClass::TangentDevelopable:
        return settings.allowedPanelTypes.tangentDevelopable;
    case PanelGeometryClass::DoubleCurved:
        return false;
    }
    return false;
}

//! 切れ目で吸収できる程度の二重曲率か。
[[nodiscard]] bool CanAbsorbWithRelief(const PanelCandidate& panel,
    const FabricationSettings& settings, double targetMaxDeviationMm)
{
    if (panel.classification != PanelGeometryClass::DoubleCurved) {
        return true;
    }
    if (!settings.reliefCutsEnabled) {
        return false;
    }
    // 二重曲率の面のうち、外れているのが一部で、外れ方も目標の数倍までなら、
    // 切れ目で逃がせる。全面が強く曲がっているものは逃がせない。
    return panel.doubleCurvedRatio <= 0.35
        && panel.flattenDeviationMm <= targetMaxDeviationMm * 4.0;
}

//! 素集合。どの面がどの部材に入るかを決めるのに使う。
class DisjointSets {
public:
    explicit DisjointSets(std::size_t count) : parent_(count)
    {
        std::iota(parent_.begin(), parent_.end(), std::size_t{0});
    }

    [[nodiscard]] std::size_t Find(std::size_t index)
    {
        while (parent_[index] != index) {
            parent_[index] = parent_[parent_[index]];
            index = parent_[index];
        }
        return index;
    }

    void Merge(std::size_t first, std::size_t second)
    {
        const std::size_t a = Find(first);
        const std::size_t b = Find(second);
        if (a == b) {
            return;
        }
        // 小さい添字を代表にする。並びを決定的にするため。
        if (a < b) {
            parent_[b] = a;
        } else {
            parent_[a] = b;
        }
    }

private:
    std::vector<std::size_t> parent_;
};

//! 素集合の結果から部材を組み立てる。並びは決定的。
[[nodiscard]] std::vector<PanelPiece> BuildPieces(const std::vector<PanelCandidate>& panels,
    DisjointSets& sets)
{
    std::map<std::size_t, PanelPiece> byRoot;
    for (std::size_t index = 0; index < panels.size(); ++index) {
        const std::size_t root = sets.Find(index);
        PanelPiece& piece = byRoot[root];
        piece.panelIndices.push_back(index);
        piece.areaMm2 += panels[index].areaMm2;
        piece.maximumDeviationMm =
            std::max(piece.maximumDeviationMm, panels[index].flattenDeviationMm);
    }
    std::vector<PanelPiece> pieces;
    pieces.reserve(byRoot.size());
    int number = 1;
    for (auto& entry : byRoot) {
        entry.second.pieceId = "piece/" + std::to_string(number++);
        pieces.push_back(std::move(entry.second));
    }
    return pieces;
}

//! 隣り合わせを、部材の分かれ方から Fold / Relief / Separate へ振り分ける。
[[nodiscard]] std::vector<PanelJoint> BuildJoints(
    const std::vector<PanelCandidate>& panels,
    const std::vector<PanelAdjacency>& adjacencies, DisjointSets& sets,
    const FabricationSettings& settings, double targetMaxDeviationMm)
{
    std::vector<PanelJoint> joints;
    joints.reserve(adjacencies.size());
    int mateNumber = 1;
    for (const PanelAdjacency& adjacency : adjacencies) {
        PanelJoint joint;
        joint.firstIndex = adjacency.firstIndex;
        joint.secondIndex = adjacency.secondIndex;
        const bool sameePiece =
            sets.Find(adjacency.firstIndex) == sets.Find(adjacency.secondIndex);
        if (!sameePiece) {
            joint.kind = JointKind::Separate;
            joint.matePairId = "mate/" + std::to_string(mateNumber++);
        } else if (settings.reliefCutsEnabled
            && (!CanAbsorbWithRelief(panels[adjacency.firstIndex], settings,
                    targetMaxDeviationMm)
                || !CanAbsorbWithRelief(panels[adjacency.secondIndex], settings,
                    targetMaxDeviationMm))) {
            joint.kind = JointKind::Relief;
        } else {
            joint.kind = JointKind::Fold;
        }
        joints.push_back(std::move(joint));
    }
    return joints;
}

//! 入力が使えるかどうか。
[[nodiscard]] std::vector<Diagnostic> CheckInput(const std::vector<PanelCandidate>& panels,
    const std::vector<PanelAdjacency>& adjacencies)
{
    std::vector<Diagnostic> errors;
    if (panels.empty()) {
        errors.push_back(MakeError(kBadInput, "分ける面がありません。", {}));
        return errors;
    }
    std::set<std::string> names;
    for (const PanelCandidate& panel : panels) {
        if (panel.panelId.empty()) {
            errors.push_back(MakeError(kBadInput, "名前の無い面があります。", {}));
        } else if (!names.insert(panel.panelId).second) {
            errors.push_back(MakeError(kBadInput, "同じ名前の面が2つあります。",
                panel.panelId));
        }
        if (!(panel.areaMm2 >= 0.0) || !(panel.flattenDeviationMm >= 0.0)) {
            errors.push_back(MakeError(kBadInput, "面の値が数になっていません。",
                panel.panelId));
        }
    }
    for (const PanelAdjacency& adjacency : adjacencies) {
        if (adjacency.firstIndex >= panels.size()
            || adjacency.secondIndex >= panels.size()) {
            errors.push_back(MakeError(kBadInput, "隣り合わせが面を指していません。", {}));
        } else if (adjacency.firstIndex == adjacency.secondIndex) {
            errors.push_back(MakeError(kBadInput, "同じ面どうしの隣り合わせがあります。",
                panels[adjacency.firstIndex].panelId));
        }
        if (adjacency.forcedBoundary && adjacency.keepTogether) {
            errors.push_back(MakeError(kBadInput,
                "同じ場所へ「必ず分ける」と「つないだまま」が両方付いています。", {}));
        }
    }
    return errors;
}

//! 全部つながっているか。OnePiece が作れるかの前提。
[[nodiscard]] bool IsAllConnected(std::size_t count,
    const std::vector<PanelAdjacency>& adjacencies)
{
    if (count <= 1) {
        return true;
    }
    DisjointSets sets(count);
    for (const PanelAdjacency& adjacency : adjacencies) {
        sets.Merge(adjacency.firstIndex, adjacency.secondIndex);
    }
    const std::size_t root = sets.Find(0);
    for (std::size_t index = 1; index < count; ++index) {
        if (sets.Find(index) != root) {
            return false;
        }
    }
    return true;
}

//! 隣り合わせを「つないだままにする値打ちの高い順」に並べる。
//! 同じ値打ちなら添字の順。並びが呼ぶたびに変わってはならない。
[[nodiscard]] std::vector<std::size_t> OrderByMergeValue(
    const std::vector<PanelAdjacency>& adjacencies)
{
    std::vector<std::size_t> order(adjacencies.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
        [&adjacencies](std::size_t left, std::size_t right) {
            const PanelAdjacency& a = adjacencies[left];
            const PanelAdjacency& b = adjacencies[right];
            if (a.keepTogether != b.keepTogether) {
                return a.keepTogether;
            }
            // 折り角の小さい、長い辺から繋ぐ。
            const double scoreA = a.sharedEdgeLengthMm / (1.0 + std::abs(a.dihedralAngleRad));
            const double scoreB = b.sharedEdgeLengthMm / (1.0 + std::abs(b.dihedralAngleRad));
            if (std::abs(scoreA - scoreB) > 1.0e-12) {
                return scoreA > scoreB;
            }
            if (a.firstIndex != b.firstIndex) {
                return a.firstIndex < b.firstIndex;
            }
            return a.secondIndex < b.secondIndex;
        });
    return order;
}

} // namespace

std::string_view JointKindNameJa(JointKind value) noexcept
{
    switch (value) {
    case JointKind::Fold:     return "折る";
    case JointKind::Relief:   return "切れ目を入れて曲げる";
    case JointKind::Separate: return "別部材にして貼る";
    }
    return "不明";
}

Result<PanelPartition> BuildPanelPartition(const std::vector<PanelCandidate>& panels,
    const std::vector<PanelAdjacency>& adjacencies, const FabricationSettings& settings,
    double targetMaxDeviationMm)
{
    std::vector<Diagnostic> errors = CheckInput(panels, adjacencies);
    if (!errors.empty()) {
        return Result<PanelPartition>::Failure(std::move(errors));
    }

    PanelPartition partition;
    partition.strategy = settings.strategy;
    DisjointSets sets(panels.size());

    const std::vector<std::size_t> order = OrderByMergeValue(adjacencies);

    switch (settings.strategy) {
    case FabricationStrategy::SeparatePanels:
        // 何も繋がない。1面が1部材。
        break;
    case FabricationStrategy::OnePiece: {
        if (!IsAllConnected(panels.size(), adjacencies)) {
            return Result<PanelPartition>::Failure(MakeError(kNotConnected,
                "面がつながっていないので、1枚にはできません。",
                "離れている島があります。少数分割か別部材で作ってください。"));
        }
        for (const std::size_t index : order) {
            const PanelAdjacency& adjacency = adjacencies[index];
            if (adjacency.forcedBoundary) {
                return Result<PanelPartition>::Failure(MakeError(kCannotBeOnePiece,
                    "「必ず分ける」と指定した線があるので、1枚にはできません。",
                    panels[adjacency.firstIndex].panelId + " と "
                        + panels[adjacency.secondIndex].panelId + " のあいだ。"));
            }
            sets.Merge(adjacency.firstIndex, adjacency.secondIndex);
        }
        // 1枚にした結果、切れ目でも逃がせない面が残っていないか。
        for (const PanelCandidate& panel : panels) {
            if (!IsDevelopable(panel, settings)
                && !CanAbsorbWithRelief(panel, settings, targetMaxDeviationMm)) {
                return Result<PanelPartition>::Failure(MakeError(kCannotBeOnePiece,
                    "1枚では展開できない面があります。",
                    panel.panelId + " は "
                        + std::string(PanelGeometryClassNameJa(panel.classification))
                        + " で、切れ目でも逃がせません。"));
            }
        }
        break;
    }
    case FabricationStrategy::FewPieces: {
        // 目標偏差を守れる範囲で、繋げるだけ繋ぐ。
        for (const std::size_t index : order) {
            const PanelAdjacency& adjacency = adjacencies[index];
            if (adjacency.forcedBoundary) {
                continue;
            }
            const PanelCandidate& first = panels[adjacency.firstIndex];
            const PanelCandidate& second = panels[adjacency.secondIndex];
            const bool bothUsable =
                (IsDevelopable(first, settings)
                    || CanAbsorbWithRelief(first, settings, targetMaxDeviationMm))
                && (IsDevelopable(second, settings)
                    || CanAbsorbWithRelief(second, settings, targetMaxDeviationMm));
            if (!bothUsable && !adjacency.keepTogether) {
                continue;
            }
            sets.Merge(adjacency.firstIndex, adjacency.secondIndex);
        }
        break;
    }
    case FabricationStrategy::Hybrid: {
        // 強く二重に曲がっている面だけ切り離し、残りは繋ぐ。
        for (const std::size_t index : order) {
            const PanelAdjacency& adjacency = adjacencies[index];
            if (adjacency.forcedBoundary) {
                continue;
            }
            const PanelCandidate& first = panels[adjacency.firstIndex];
            const PanelCandidate& second = panels[adjacency.secondIndex];
            const bool stronglyCurved =
                (first.classification == PanelGeometryClass::DoubleCurved
                    && !CanAbsorbWithRelief(first, settings, targetMaxDeviationMm))
                || (second.classification == PanelGeometryClass::DoubleCurved
                    && !CanAbsorbWithRelief(second, settings, targetMaxDeviationMm));
            if (stronglyCurved && !adjacency.keepTogether) {
                partition.notes.push_back(MakeInformation(kSplitForCurvature,
                    "強く二重に曲がっているので、ここで分けます。",
                    first.panelId + " と " + second.panelId + " のあいだ。"));
                continue;
            }
            sets.Merge(adjacency.firstIndex, adjacency.secondIndex);
        }
        break;
    }
    }

    partition.pieces = BuildPieces(panels, sets);
    partition.joints =
        BuildJoints(panels, adjacencies, sets, settings, targetMaxDeviationMm);

    if (settings.strategy == FabricationStrategy::OnePiece
        && partition.pieces.size() != 1) {
        return Result<PanelPartition>::Failure(MakeError(kCannotBeOnePiece,
            "1枚にまとめられませんでした。",
            std::to_string(partition.pieces.size()) + " 枚に分かれます。"));
    }
    if (partition.pieces.size()
        > static_cast<std::size_t>(std::max(settings.panelCountLimit, 1))) {
        partition.notes.push_back(MakeWarning("FAB-P100",
            "部材の数が上限を超えています。",
            std::to_string(partition.pieces.size()) + " 枚(上限 "
                + std::to_string(settings.panelCountLimit) + " 枚)。"));
    }

    std::vector<Diagnostic> warnings = partition.notes;
    return Result<PanelPartition>::Success(std::move(partition), std::move(warnings));
}

PanelPartitionComparison CompareAllStrategies(const std::vector<PanelCandidate>& panels,
    const std::vector<PanelAdjacency>& adjacencies, const FabricationSettings& settings,
    double targetMaxDeviationMm)
{
    PanelPartitionComparison comparison;
    for (const FabricationStrategy strategy :
        {FabricationStrategy::OnePiece, FabricationStrategy::FewPieces,
            FabricationStrategy::SeparatePanels, FabricationStrategy::Hybrid}) {
        FabricationSettings local = settings;
        local.strategy = strategy;
        auto built = BuildPanelPartition(panels, adjacencies, local, targetMaxDeviationMm);
        PanelPartitionComparison::Entry entry;
        entry.strategy = strategy;
        entry.available = built.HasValue();
        if (built.HasValue()) {
            entry.partition = built.Value();
        } else {
            entry.refusal = built.Diagnostics();
        }
        comparison.entries.push_back(std::move(entry));
    }
    return comparison;
}

} // namespace kachakacha::v2::fabrication
