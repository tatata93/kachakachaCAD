//! 部材の分割と統合(オーナー指示 2026-09-14 §32)。
//!
//! 自動で出した分け方は出発点であって、答えではない。
//! 「この2枚はつないだまま切りたい」「ここは分けたい」は人が決める。
//!
//! **誤差が増えるからという理由だけで禁止しない。** 2枚を1枚にすれば
//! たいてい誤差は増えるが、接着線が1本減るほうがきれいに作れることがある。
//! こちらは「前はこう、後はこう」を見せるところまでにする。
//!
//! 判断は core(`fabrication/PanelEdit`)がする。ここは相手を集めて渡すだけ。

#include "V2MainWindow.h"

#include "V2FabricationDock.h"
#include "V2Viewport.h"

#include "kachakacha/fabrication/PanelEdit.h"

#include <QString>

#include <cstddef>
#include <string>
#include <vector>

namespace {

//! 型紙の部材から、分け方を決めるための面の並びを作る。
//!
//! いまの部材はすでに分かれた形で持っているので、
//! 「面1枚 = いまの部材1枚」として扱う。分け直しはこの上で行う。
[[nodiscard]] std::vector<kachakacha::v2::fabrication::PanelCandidate> PanelsOf(
    const kachakacha::v2::app::FabricationEvaluation& model)
{
    std::vector<kachakacha::v2::fabrication::PanelCandidate> panels;
    for (std::size_t index = 0; index < model.panels.size(); ++index) {
        kachakacha::v2::fabrication::PanelCandidate candidate;
        candidate.panelId = model.panels[index].panelId;
        candidate.flattenDeviationMm = model.maximumDeviationMm;
        candidate.areaMm2 = 1.0;
        panels.push_back(std::move(candidate));
    }
    return panels;
}

//! 帯の並びは、隣どうしが繋がっている。0-1-2-... の鎖にする。
[[nodiscard]] std::vector<kachakacha::v2::fabrication::PanelAdjacency> ChainOf(
    std::size_t count)
{
    std::vector<kachakacha::v2::fabrication::PanelAdjacency> chain;
    for (std::size_t index = 0; index + 1 < count; ++index) {
        kachakacha::v2::fabrication::PanelAdjacency edge;
        edge.firstIndex = index;
        edge.secondIndex = index + 1;
        edge.sharedEdgeLengthMm = 1.0;
        chain.push_back(edge);
    }
    return chain;
}

//! 部材を全部ばらした分け方。いまの型紙と同じ形から始める。
[[nodiscard]] kachakacha::v2::fabrication::PanelPartition PartitionOf(std::size_t count)
{
    kachakacha::v2::fabrication::PanelPartition partition;
    for (std::size_t index = 0; index < count; ++index) {
        kachakacha::v2::fabrication::PanelPiece piece;
        piece.pieceId = "部材" + std::to_string(index + 1);
        piece.panelIndices.push_back(index);
        partition.pieces.push_back(std::move(piece));
    }
    return partition;
}

} // namespace

//! いまの製作モデルの部材の数。無ければ 0。
std::size_t V2MainWindow::FabricationPanelCount() const
{
    const auto found = fabricationModels_.find(CurrentFabricationModelId().ToString());
    return found == fabricationModels_.end() ? 0 : found->second.panels.size();
}

//! 「部材を1つにする」。棚の「曲げる部材」で挙げた2つを1つにする。
void V2MainWindow::MergeFabricationParts()
{
    const auto numbers = SelectedPartNumbers();
    if (numbers.size() != 2) {
        SetStatus(QStringLiteral(
            "部材を1つにする: 棚の「曲げる部材」に、1つにする2つの番号を"
            "「1, 2」のように書いてください。"));
        return;
    }
    const std::size_t count = FabricationPanelCount();
    if (count < 2) {
        SetStatus(QStringLiteral("部材を1つにする: 先に製作モデルを作ってください。"));
        return;
    }
    const auto panels = PanelsOf(
        fabricationModels_.at(CurrentFabricationModelId().ToString()));
    const auto chain = ChainOf(count);
    const auto partition = PartitionOf(count);
    const auto preview = kachakacha::v2::fabrication::PreviewMerge(partition, panels,
        chain, numbers[0], numbers[1]);
    if (!preview.possible) {
        SetStatus(QStringLiteral("部材を1つにする: %1")
                .arg(QString::fromStdString(preview.messageJa)));
        return;
    }
    // 前と後を見せる。実際に作り直すのは、型紙の作り方そのものを持てるようになってから。
    SetStatus(QStringLiteral("部材を1つにする: %1")
            .arg(QString::fromStdString(preview.messageJa)));
}

//! 「部材を分ける」。棚の「曲げる部材」で挙げた1つを2つに分ける。
void V2MainWindow::SplitFabricationPart()
{
    const auto numbers = SelectedPartNumbers();
    if (numbers.size() != 1) {
        SetStatus(QStringLiteral(
            "部材を分ける: 棚の「曲げる部材」に、分ける部材の番号を1つ書いてください。"));
        return;
    }
    const std::size_t count = FabricationPanelCount();
    if (count < 1) {
        SetStatus(QStringLiteral("部材を分ける: 先に製作モデルを作ってください。"));
        return;
    }
    const auto panels = PanelsOf(
        fabricationModels_.at(CurrentFabricationModelId().ToString()));
    // いまの型紙は1部材=1面なので、そのままでは分けられない。
    // まず全部を1枚にまとめた分け方の上で、半分を分ける形にする。
    kachakacha::v2::fabrication::PanelPartition whole;
    kachakacha::v2::fabrication::PanelPiece one;
    one.pieceId = "部材1";
    for (std::size_t index = 0; index < count; ++index) {
        one.panelIndices.push_back(index);
    }
    whole.pieces.push_back(std::move(one));
    std::vector<std::size_t> moved;
    for (std::size_t index = count / 2; index < count; ++index) {
        moved.push_back(index);
    }
    const auto preview = kachakacha::v2::fabrication::PreviewSplit(whole, panels, 0,
        moved);
    SetStatus(QStringLiteral("部材を分ける: %1")
            .arg(QString::fromStdString(preview.messageJa)));
}

//! 棚の「曲げる部材」に書いた番号。0 起点へ直して返す。
std::vector<std::size_t> V2MainWindow::SelectedPartNumbers() const
{
    std::vector<std::size_t> numbers;
    if (fabricationDock_ == nullptr) {
        return numbers;
    }
    const auto parsed = kachakacha::v2::app::ParsePartNumberList(
        fabricationDock_->PartNumbersText().toStdString());
    if (!parsed.HasValue()) {
        return numbers;
    }
    for (const int number : parsed.Value()) {
        if (number >= 1) {
            numbers.push_back(static_cast<std::size_t>(number - 1));
        }
    }
    return numbers;
}
