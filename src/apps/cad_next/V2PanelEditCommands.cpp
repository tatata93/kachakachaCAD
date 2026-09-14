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
#include "kachakacha/document/Document.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/document/Document.h"
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

//! 分け方を文書へ書く。以後は自動で切り直さない(人が決めたほうを残す)。
//!
//! 部材ごとに持っていた値(組立率・折り線の進み・半径と固定・展開の基準)は
//! **引き継ぐ。** 変えていない部材の値まで消すのは、利用者の入力を勝手に
//! 捨てることである(Codex Q1-Q5-R3 B3)。引き継ぎの決まりは core が持つ。
bool V2MainWindow::ApplyBandBoundaries(const std::vector<double>& inner,
    const QString& what, const std::string& messageJa,
    const kachakacha::v2::fabrication::BandValueRemap& carried)
{
    using kachakacha::v2::document::UpdateFeatureDefinitionCommand;

    const auto* entity = session_->GetDocument().FindEntity(CurrentFabricationModelId());
    const auto* feature =
        entity == nullptr ? nullptr : session_->GetDocument().FindFeature(entity->createdBy);
    const auto* current = feature == nullptr
        ? nullptr
        : std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
              &feature->definition);
    if (current == nullptr) {
        SetStatus(QStringLiteral("%1: 近似の作り方が見つかりません。").arg(what));
        return false;
    }
    auto definition = *current;
    definition.automaticBoundaries = false;
    definition.manualBoundaries = inner;
    definition.bandProgress = carried.bandProgress;
    definition.creaseProgress = carried.creaseProgress;
    definition.bendRadiusMm = carried.bendRadiusMm;
    definition.bendRadiusLock = carried.bendRadiusLock;
    definition.unfoldBaseRail = carried.unfoldBaseRail;
    const auto changed = session_->GetDocument().Run(UpdateFeatureDefinitionCommand(
        feature->id, definition, feature->inputEntityIds, what.toStdString()));
    if (!changed.committed) {
        ReportDiagnostics(changed.diagnostics);
        return false;
    }
    AdoptCurrentDocument();
    // 境目を変えたら、帯へ切り直さないと枚数が変わらない。
    // 覚えている近似は前の切り方のままである。
    RebuildKernelShapes();
    RefreshFabricationView();
    RefreshBendRadius();
    (void)messageJa;
    return true;
}

//! 見せる相手と、決めたあとに変える境目を、**同じ1つの候補から**作る。
//!
//! 別々に作ると「分けられます」と言った相手と実際に変える境目が食い違い、
//! 見せた前後の姿と出来上がりがずれる(Codex Q1-Q5-R2 B2)。
void V2MainWindow::ApplyBandPartition(
    const kachakacha::v2::fabrication::BandPartitionPreview& preview, const QString& what,
    const kachakacha::v2::fabrication::BandValueRemap& carried)
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
    // まとめて1つの操作にする。**見せた形と出来た形が違ったら、書かずに戻す。**
    // 警告だけ出して違う形を残すと、利用者のモデルが黙って変わる
    // (Codex Q1-Q5-R3 B2)。
    bool ok = false;
    std::size_t actual = 0;
    {
        kachakacha::v2::document::Document::Transaction transaction(
            session_->GetDocument(), what.toStdString());
        if (ApplyBandBoundaries(inner, what, preview.messageJa, carried)) {
            actual = FabricationPanelCount();
            ok = actual == preview.partsAfter;
        }
        if (ok) {
            ok = transaction.Commit();
        }
        // Commit していなければ、ここを抜けるときに書く前へ戻る。
    }
    AdoptCurrentDocument();
    RebuildKernelShapes();
    RefreshFabricationView();
    RefreshBendRadius();
    if (!ok) {
        SetStatus(QStringLiteral("%1: 見せた形と出来た形が違ったので、"
                                 "何も変えずに戻しました(%2 枚と言って %3 枚)。")
                .arg(what)
                .arg(static_cast<int>(preview.partsAfter))
                .arg(static_cast<int>(actual)));
        return;
    }
    QString dropped;
    for (const std::size_t part : carried.droppedParts) {
        if (!dropped.isEmpty()) {
            dropped += QStringLiteral("、");
        }
        dropped += QStringLiteral("部材%1").arg(static_cast<int>(part));
    }
    SetStatus(QStringLiteral("%1: %2 いまは %3 枚です。以後は自動で切り直しません。%4")
            .arg(what)
            .arg(QString::fromStdString(preview.messageJa))
            .arg(static_cast<int>(actual))
            .arg(dropped.isEmpty()
                    ? QString()
                    : QStringLiteral("%1 に入れてあった半径は引き継げないので捨てました。")
                          .arg(dropped)));
}

//! いま部材ごとに持っている値。引き継ぎの元になる。
kachakacha::v2::fabrication::BandValueRemap V2MainWindow::BandValuesNow() const
{
    kachakacha::v2::fabrication::BandValueRemap values;
    const auto* definition = CurrentFabricationDefinition();
    if (definition == nullptr) {
        return values;
    }
    values.bandProgress = definition->bandProgress;
    values.creaseProgress = definition->creaseProgress;
    values.bendRadiusMm = definition->bendRadiusMm;
    values.bendRadiusLock = definition->bendRadiusLock;
    values.unfoldBaseRail = definition->unfoldBaseRail;
    return values;
}

//! 案を見せた相手の指紋。**どの模型の、どの値に対して見せた案か。**
//!
//! 案を見せたあとで別の製作模型を選んだり、組立率や半径を変えたりしても、
//! 番号と指示名しか見ていなかったので「2度目」と判定して当ててしまえた。
//! 見せていない形が、見せた形として確定し得た(Codex Q1-Q5-R4 B1)。
//! ここで、案がどの状態に対するものだったかを一緒に覚える。
std::string V2MainWindow::FabricationInputSignature() const
{
    const auto* definition = CurrentFabricationDefinition();
    if (definition == nullptr) {
        return {};
    }
    std::string text = CurrentFabricationModelId().ToString();
    text += '|';
    // 分け方そのもの。
    for (const double rail : definition->manualBoundaries) {
        text += std::to_string(static_cast<long long>(std::llround(rail * 1000000.0)));
        text += ',';
    }
    text += '|';
    // 引き継ぐ値。案はこれらの上に組み立てられている。
    for (const double value : definition->bandProgress) {
        text += std::to_string(static_cast<long long>(std::llround(value * 1000000.0)));
        text += ',';
    }
    text += '|';
    for (const double value : definition->creaseProgress) {
        text += std::to_string(static_cast<long long>(std::llround(value * 1000000.0)));
        text += ',';
    }
    text += '|';
    for (const double value : definition->bendRadiusMm) {
        text += std::to_string(static_cast<long long>(std::llround(value * 1000000.0)));
        text += ',';
    }
    text += '|';
    for (const int lock : definition->bendRadiusLock) {
        text += std::to_string(lock);
        text += ',';
    }
    text += '|';
    text += std::to_string(definition->unfoldBaseRail);
    return text;
}

//! 見せている案を捨てる。文書は触らない(そもそも触っていない)。
void V2MainWindow::ForgetPendingPartition()
{
    pendingPartition_.reset();
}

//! 1度目は見せるだけ。2度目で当てる。
//!
//! 「前と後を見せてから決める」と説明しているのに、押した瞬間に変わっていた
//! (Codex Q1-Q5-R3 B1)。1度目は文書を一切触らず、前と後を帯に出す。
//! 同じ指示をもう一度出したら当てる。番号を変えたら、その番号で見せ直す。
void V2MainWindow::ProposeOrApplyPartition(const QString& what,
    const std::vector<std::size_t>& numbers,
    const kachakacha::v2::fabrication::BandPartitionPreview& preview,
    const kachakacha::v2::fabrication::BandValueRemap& carried)
{
    // 「2度目」は、**同じ指示・同じ番号・同じ相手・同じ値**のときだけ。
    // どれか1つでも変わっていれば、見せた案はもう今の形の案ではない。
    const std::string signature = FabricationInputSignature();
    const bool sameAsShown = pendingPartition_.has_value()
        && pendingPartition_->what == what && pendingPartition_->numbers == numbers
        && pendingPartition_->signature == signature && !signature.empty();
    if (pendingPartition_.has_value() && !sameAsShown
        && pendingPartition_->what == what && pendingPartition_->numbers == numbers) {
        // 同じ指示・同じ番号なのに相手か値が変わった。黙って当てない。
        SetStatus(QStringLiteral(
            "%1: 見せてからモデルか値が変わりました。いまの形で出し直します。").arg(what));
    }
    if (!preview.possible) {
        ForgetPendingPartition();
        SetStatus(QStringLiteral("%1: %2").arg(what,
            QString::fromStdString(
                kachakacha::v2::fabrication::DescribeBandPartitionJa(preview))));
        return;
    }
    if (!sameAsShown) {
        PendingPartition pending;
        pending.what = what;
        pending.numbers = numbers;
        pending.signature = signature;
        pending.preview = preview;
        pending.carried = carried;
        pendingPartition_ = std::move(pending);
        // 何が消えるのかを、番号だけでなく名前で言う。
        QString dropped;
        for (const auto& lost : carried.droppedValues) {
            if (!dropped.isEmpty()) {
                dropped += QStringLiteral("、");
            }
            dropped += QStringLiteral("部材%1の%2")
                           .arg(static_cast<int>(lost.part))
                           .arg(QString::fromStdString(lost.what));
        }
        SetStatus(QStringLiteral("%1(まだ変えていません): %2%3 "
                                 "もう一度同じ指示を出すと、この形にします。"
                                 "やめるときは Esc か道具を替えてください。")
                .arg(what)
                .arg(QString::fromStdString(
                    kachakacha::v2::fabrication::DescribeBandPartitionJa(preview)))
                .arg(dropped.isEmpty()
                        ? QString()
                        : QStringLiteral(" %1 は引き継げません。").arg(dropped)));
        return;
    }
    const PendingPartition decided = *pendingPartition_;
    ForgetPendingPartition();
    ApplyBandPartition(decided.preview, decided.what, decided.carried);
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
    const std::size_t parts = rails.empty() ? 0 : rails.size() - 1;
    ProposeOrApplyPartition(QStringLiteral("部材を1つにする"), numbers,
        kachakacha::v2::fabrication::PreviewBandMerge(rails, widths, numbers.front()),
        kachakacha::v2::fabrication::RemapForMerge(BandValuesNow(), parts,
            numbers.front()));
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
    const std::size_t parts = rails.empty() ? 0 : rails.size() - 1;
    ProposeOrApplyPartition(QStringLiteral("部材を分ける"), numbers,
        kachakacha::v2::fabrication::PreviewBandSplit(rails, widths, numbers.front(),
            minimumMm),
        kachakacha::v2::fabrication::RemapForSplit(BandValuesNow(), parts,
            numbers.front()));
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
