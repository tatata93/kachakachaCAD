//! 曲げた先の半径と、AUTO / LOCK(オーナー指示 2026-09-14 §30・§31)。
//!
//! 円筒として近似した部材は、曲げ具合と半径が同じことの言い換えである。
//! どちらから入れてもよい。判断は core(`fabrication/BandBendRadius`)がする。
//!
//! **固定した値は、近似をやり直しても戻さない。** 模型工作では、
//! 手元にある丸棒や治具の径へ合わせたいことがある。
//! 計算した 21.63mm より、22.00mm のほうが作れる、ということが起きる。
//!
//! 半径は **文書が持つ**。画面が覚えていると、保存で消え、取り消しで戻らず、
//! 開き直すと別の形になる。ここは近似の作り方(Feature の定義)へ書き込み、
//! 取り消し・やり直し・保存・再計算のすべてに乗せる(Codex Q1-Q5 B2)。
//!
//! 測り方も推測しない。帯 i の幅 w とその先の折り線の角 θ から `R = w / θ`。
//! これは多角形で円を内接近似したときの弦と中心角の関係そのものである。
//! 外周の 1/4 を 90 度と決めつけるような当て推量はしない。

#include "V2MainWindow.h"

#include "V2FabricationDock.h"

#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/fabrication/BandBendRadius.h"
#include "kachakacha/fabrication/BandFold.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace {

using kachakacha::v2::domain::CreateFabricationModelDefinition;

//! いま画面が相手にしている部材の番号。棚に書いていなければ 0 番。
[[nodiscard]] std::size_t FirstPartOr(const std::vector<std::size_t>& numbers,
    std::size_t count)
{
    if (numbers.empty() || count == 0) {
        return 0;
    }
    return std::min(numbers.front(), count - 1);
}

} // namespace

//! いまの近似モデルの、部材ごとの曲げと半径。無ければ空。
std::vector<kachakacha::v2::fabrication::BendRadius> V2MainWindow::BendRadiiNow() const
{
    const auto* definition = CurrentFabricationDefinition();
    const auto found = fabricationModels_.find(CurrentFabricationModelId().ToString());
    if (definition == nullptr || found == fabricationModels_.end()
        || !found->second.bandMesh.has_value()) {
        return {};
    }
    return kachakacha::v2::app::ResolveFoldState(*definition, *found->second.bandMesh).bends;
}

//! いま棚に出ている部材の曲げと半径。無ければ空の既定値。
kachakacha::v2::fabrication::BendRadius V2MainWindow::BendRadiusNow() const
{
    const auto bends = BendRadiiNow();
    if (bends.empty()) {
        return {};
    }
    return bends[FirstPartOr(SelectedPartNumbers(), bends.size())];
}

//! いまの近似モデルの作り方。無ければ空。
const kachakacha::v2::domain::CreateFabricationModelDefinition*
V2MainWindow::CurrentFabricationDefinition() const
{
    const auto* entity = session_->GetDocument().FindEntity(CurrentFabricationModelId());
    const auto* feature =
        entity == nullptr ? nullptr : session_->GetDocument().FindFeature(entity->createdBy);
    if (feature == nullptr) {
        return nullptr;
    }
    return std::get_if<CreateFabricationModelDefinition>(&feature->definition);
}

//! いまの曲げ状態での形の指紋。座標をまるめて並べた文字列。
//!
//! 試験が「表示だけ変わって形は同じ」を見つけるために使う。
//! 数字を見比べるのではなく、形そのものが動いたかを見る。
std::string V2MainWindow::FabricationShapeSignature() const
{
    const auto* definition = CurrentFabricationDefinition();
    const auto found = fabricationModels_.find(CurrentFabricationModelId().ToString());
    if (definition == nullptr || found == fabricationModels_.end()
        || !found->second.bandMesh.has_value()) {
        return {};
    }
    const auto state = kachakacha::v2::app::ResolveFoldState(*definition,
        *found->second.bandMesh);
    const auto folded = kachakacha::v2::fabrication::FoldBandMesh(*found->second.bandMesh,
        state.masterProgress, state.creaseFactors, state.unfoldBaseRail);
    std::string text;
    for (const auto& row : folded) {
        // 全部の点を並べると長い。行の端と真ん中だけで足りる。
        for (const std::size_t column : {std::size_t{0}, row.size() / 2,
                 row.empty() ? std::size_t{0} : row.size() - 1}) {
            if (column >= row.size()) {
                continue;
            }
            const auto& point = row[column];
            for (const double value : {point.x, point.y, point.z}) {
                // 1/1000 mm でまるめる。計算の揺らぎで指紋が変わらないようにする。
                text += std::to_string(static_cast<long long>(std::llround(value * 1000.0)));
                text += ',';
            }
        }
        text += ';';
    }
    return text;
}

//! いまの近似モデルから、曲げと半径を測り直して棚へ出す。固定してあれば触らない。
void V2MainWindow::RefreshBendRadius()
{
    if (fabricationDock_ == nullptr) {
        return;
    }
    const auto bends = BendRadiiNow();
    const double percent = AssemblyPercentNow();
    if (bends.empty()) {
        fabricationDock_->ShowRadius(kachakacha::v2::fabrication::BendRadius{}, percent);
        return;
    }
    const std::size_t part = FirstPartOr(SelectedPartNumbers(), bends.size());
    fabricationDock_->ShowRadius(bends[part], percent);
}

//! いまの組立率。近似モデルが無ければ 100%。
double V2MainWindow::AssemblyPercentNow() const
{
    const auto* definition = CurrentFabricationDefinition();
    return definition == nullptr ? 100.0 : definition->masterPercent;
}

//! 「固定」「固定を外す」を押した。判断は core がする。
//! 結果は文書の作り方へ書き込む。保存・取り消し・再計算に乗せるためである。
void V2MainWindow::ApplyBendRadius(double radiusMm, bool locked)
{
    using kachakacha::v2::document::UpdateFeatureDefinitionCommand;

    const auto* entity = session_->GetDocument().FindEntity(CurrentFabricationModelId());
    const auto* feature =
        entity == nullptr ? nullptr : session_->GetDocument().FindFeature(entity->createdBy);
    const auto* current = feature == nullptr
        ? nullptr
        : std::get_if<CreateFabricationModelDefinition>(&feature->definition);
    const auto bends = BendRadiiNow();
    if (current == nullptr || bends.empty()) {
        SetStatus(QStringLiteral(
            "半径: 先に「製作モデルを作る」で帯近似の近似モデルを作ってください。"));
        return;
    }
    const std::size_t part = FirstPartOr(SelectedPartNumbers(), bends.size());
    const double percent = AssemblyPercentNow();

    auto definition = *current;
    definition.bendRadiusMm.assign(bends.size(), 0.0);
    definition.bendRadiusLock.assign(bends.size(), 0);
    for (std::size_t index = 0; index < bends.size(); ++index) {
        const bool wasLocked =
            bends[index].lock == kachakacha::v2::fabrication::ValueLock::Locked;
        // 自動の部材は 0 を入れる。測った値を書き込むと、
        // 「人が決めた値」と「たまたま測れた値」の区別がつかなくなる。
        definition.bendRadiusMm[index] = wasLocked ? bends[index].radiusMm : 0.0;
        definition.bendRadiusLock[index] = wasLocked ? 1 : 0;
    }

    QString message;
    if (!locked) {
        // 固定を外す。次に測った値へ戻る。
        definition.bendRadiusLock[part] = 0;
        definition.bendRadiusMm[part] = 0.0;
        message = QStringLiteral("半径: 部材%1 を自動へ戻しました。").arg(static_cast<int>(part) + 1);
    } else {
        // 70% のときに R=22mm と入れたら、100% の半径をそこから決める(§30)。
        const auto decided = kachakacha::v2::fabrication::LockRadiusAtPercent(bends[part],
            radiusMm, percent);
        if (!decided.HasValue()) {
            ReportDiagnostics(decided.Diagnostics());
            return;
        }
        definition.bendRadiusLock[part] = 1;
        definition.bendRadiusMm[part] = decided.Value().radiusMm;
        message = QStringLiteral("半径: 部材%1 は %2。近似をやり直しても戻しません。")
                      .arg(static_cast<int>(part) + 1)
                      .arg(QString::fromStdString(
                          kachakacha::v2::fabrication::DescribeBendRadiusJa(
                              decided.Value(), percent)));
    }

    const auto changed = session_->GetDocument().Run(UpdateFeatureDefinitionCommand(
        feature->id, definition, feature->inputEntityIds, "曲げ半径を決める"));
    if (!changed.committed) {
        ReportDiagnostics(changed.diagnostics);
        return;
    }
    AdoptCurrentDocument();
    RefreshFabricationView();
    RefreshBendRadius();
    SetStatus(message);
}

//! いま決まっている、展開の基準にする辺(§33)。
int V2MainWindow::UnfoldBaseRailNow() const
{
    const auto* definition = CurrentFabricationDefinition();
    const auto found = fabricationModels_.find(CurrentFabricationModelId().ToString());
    if (definition == nullptr || found == fabricationModels_.end()
        || !found->second.bandMesh.has_value()) {
        return 0;
    }
    return kachakacha::v2::app::ResolveFoldState(*definition, *found->second.bandMesh)
        .unfoldBaseRail;
}

//! 展開の基準にする辺を決める(§33)。
//!
//! 展開すると、既定では先頭の辺が動かない。しかし人が作るときは
//! 「この辺は動かしたくない」がある。床板の縁を基準にすれば、
//! 展開しても床板がその場に残り、まわりの板だけが開く。
//!
//! 相手は棚の「曲げる部材」に書いた番号で選ぶ。番号 n の **手前の境目** が基準になる。
//! 1 を書けば先頭の辺(既定に戻る)、2 を書けば1枚目と2枚目の境目である。
void V2MainWindow::SetUnfoldBaseRail()
{
    using kachakacha::v2::document::UpdateFeatureDefinitionCommand;

    const auto* entity = session_->GetDocument().FindEntity(CurrentFabricationModelId());
    const auto* feature =
        entity == nullptr ? nullptr : session_->GetDocument().FindFeature(entity->createdBy);
    const auto* current = feature == nullptr
        ? nullptr
        : std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
              &feature->definition);
    const auto found = fabricationModels_.find(CurrentFabricationModelId().ToString());
    if (current == nullptr || found == fabricationModels_.end()
        || !found->second.bandMesh.has_value()) {
        SetStatus(QStringLiteral(
            "展開の基準にする辺: 先に「製作モデルを作る」で帯近似の近似モデルを"
            "作ってください。"));
        return;
    }
    const auto numbers = SelectedPartNumbers();
    if (numbers.size() != 1) {
        SetStatus(QStringLiteral(
            "展開の基準にする辺: 棚の「曲げる部材」に、基準にする部材の番号を"
            "1つ書いてください。その部材の手前の境目が基準になります。"));
        return;
    }
    const int rails = found->second.bandMesh->rows;
    const int wanted = static_cast<int>(numbers.front());
    if (wanted < 0 || wanted >= rails) {
        SetStatus(QStringLiteral("展開の基準にする辺: 1 から %1 までの番号を"
                                 "書いてください。")
                .arg(rails - 1));
        return;
    }
    auto definition = *current;
    definition.unfoldBaseRail = wanted;
    const auto changed = session_->GetDocument().Run(UpdateFeatureDefinitionCommand(
        feature->id, definition, feature->inputEntityIds, "展開の基準にする辺を決める"));
    if (!changed.committed) {
        ReportDiagnostics(changed.diagnostics);
        return;
    }
    AdoptCurrentDocument();
    RefreshFabricationView();
    SetStatus(wanted == 0
            ? QStringLiteral("展開の基準にする辺: 先頭の辺(既定)に戻しました。")
            : QStringLiteral("展開の基準にする辺: 部材%1 の手前の境目にしました。"
                             "展開してもこの辺は動きません。")
                  .arg(wanted + 1));
}
