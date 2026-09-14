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

#include "kachakacha/document/Commands.h"
#include "kachakacha/fabrication/BandPartition.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

//! いまの製作モデルの部材の数。無ければ 0。
std::size_t V2MainWindow::FabricationPanelCount() const
{
    const auto found = fabricationModels_.find(CurrentFabricationModelId().ToString());
    return found == fabricationModels_.end() ? 0 : found->second.panels.size();
}

//! いまの帯の境目と、部材ごとの幅。近似がまだなら空。
bool V2MainWindow::CurrentBandPartition(std::vector<double>& railParameters,
    std::vector<double>& widthsMm) const
{
    const auto found = fabricationModels_.find(CurrentFabricationModelId().ToString());
    if (found == fabricationModels_.end() || !found->second.bands.has_value()
        || !found->second.bandMesh.has_value()) {
        return false;
    }
    railParameters = found->second.bands->railParameters;
    widthsMm.clear();
    const auto& mesh = *found->second.bandMesh;
    for (int band = 0; band < mesh.BandCount(); ++band) {
        const auto row = static_cast<std::size_t>(band);
        if (row + 1 >= mesh.developed.size() || mesh.developed[row].empty()
            || mesh.developed[row + 1].empty()) {
            widthsMm.push_back(0.0);
            continue;
        }
        const std::size_t column =
            std::min(mesh.developed[row].size(), mesh.developed[row + 1].size()) / 2;
        const double du = mesh.developed[row + 1][column].u - mesh.developed[row][column].u;
        const double dv = mesh.developed[row + 1][column].v - mesh.developed[row][column].v;
        widthsMm.push_back(std::sqrt(du * du + dv * dv));
    }
    return !railParameters.empty();
}

//! 見せる相手と、決めたあとに変える境目を、**同じ1つの候補から**作る。
//!
//! 別々に作ると「分けられます」と言った相手と実際に変える境目が食い違い、
//! 見せた前後の姿と出来上がりがずれる(Codex Q1-Q5-R2 B2)。
void V2MainWindow::ApplyBandPartition(
    const kachakacha::v2::fabrication::BandPartitionPreview& preview, const QString& what)
{
    const QString text =
        QString::fromStdString(kachakacha::v2::fabrication::DescribeBandPartitionJa(preview));
    if (!preview.possible) {
        SetStatus(QStringLiteral("%1: %2").arg(what, text));
        return;
    }
    // 中の境目だけを渡す。両端の 0 と 1 は作り直す側が付ける。
    std::vector<double> inner;
    for (std::size_t index = 1; index + 1 < preview.railParameters.size(); ++index) {
        inner.push_back(preview.railParameters[index]);
    }
    if (!ApplyBandBoundaries(inner, what, preview.messageJa)) {
        return;
    }
    // 言ったとおりの枚数になったか、その場で突き合わせる。
    // 言うだけ言って違う形になっていた、を通さない。
    const std::size_t actual = FabricationPanelCount();
    if (actual != preview.partsAfter) {
        SetStatus(QStringLiteral("%1: 見せた形と出来た形が違います"
                                 "(%2 枚と言って %3 枚になりました)。")
                .arg(what)
                .arg(static_cast<int>(preview.partsAfter))
                .arg(static_cast<int>(actual)));
    }
}

//! 「部材を1つにする」。棚の「曲げる部材」で挙げた番号と、その次を1枚にする。
void V2MainWindow::MergeFabricationParts()
{
    const auto numbers = SelectedPartNumbers();
    if (numbers.size() != 1) {
        SetStatus(QStringLiteral(
            "部材を1つにする: 棚の「曲げる部材」に、番号を1つ書いてください。"
            "その番号と次の番号を1枚にします。"));
        return;
    }
    std::vector<double> rails;
    std::vector<double> widths;
    if (!CurrentBandPartition(rails, widths)) {
        SetStatus(QStringLiteral(
            "部材を1つにする: 先に「製作モデルを作る」で帯近似の近似モデルを"
            "作ってください。"));
        return;
    }
    ApplyBandPartition(
        kachakacha::v2::fabrication::PreviewBandMerge(rails, widths, numbers.front()),
        QStringLiteral("部材を1つにする"));
}

//! 「部材を分ける」。棚の「曲げる部材」で挙げた1つを、その真ん中で2つに分ける。
void V2MainWindow::SplitFabricationPart()
{
    const auto numbers = SelectedPartNumbers();
    if (numbers.size() != 1) {
        SetStatus(QStringLiteral(
            "部材を分ける: 棚の「曲げる部材」に、分ける部材の番号を1つ書いてください。"));
        return;
    }
    std::vector<double> rails;
    std::vector<double> widths;
    if (!CurrentBandPartition(rails, widths)) {
        SetStatus(QStringLiteral(
            "部材を分ける: 先に「製作モデルを作る」で帯近似の近似モデルを"
            "作ってください。"));
        return;
    }
    // 細くなりすぎる分け方は core が断る。基準は近似の作り方が持つ最小幅。
    const auto* definition = CurrentFabricationDefinition();
    const double minimumMm = definition == nullptr ? 4.0 : definition->minimumPartWidthMm;
    ApplyBandPartition(kachakacha::v2::fabrication::PreviewBandSplit(rails, widths,
                           numbers.front(), minimumMm),
        QStringLiteral("部材を分ける"));
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
