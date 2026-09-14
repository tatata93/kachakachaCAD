//! 曲げた先の半径と、AUTO / LOCK(オーナー指示 2026-09-14 §30・§31)。
//!
//! 円筒として近似した部材は、曲げ具合と半径が同じことの言い換えである。
//! どちらから入れてもよい。判断は core(`fabrication/BendRadius`)がする。
//!
//! **固定した値は、近似をやり直しても戻さない。** 模型工作では、
//! 手元にある丸棒や治具の径へ合わせたいことがある。
//! 計算した 21.63mm より、22.00mm のほうが作れる、ということが起きる。

#include "V2MainWindow.h"

#include "V2FabricationDock.h"

#include "kachakacha/fabrication/BendRadius.h"

#include <QString>

#include <cmath>
#include <string>

namespace {

//! 部材の並びから、曲げる向きの長さと測った半径を見積もる。
//!
//! 型紙の外周の広がりを、曲げる向きの長さとみなす。
//! 半径は、いま出ている 3D の帯の曲がり方から出す。
//! どちらも取れなければ値を返さない。「測れた」と嘘をつかない。
[[nodiscard]] bool MeasureBend(const kachakacha::v2::app::FabricationEvaluation& model,
    double& flatLengthMm, double& radiusMm)
{
    if (model.panels.empty()) {
        return false;
    }
    // 型紙の外周を1周する長さの4分の1を、曲げる向きの長さの目安にする。
    // 帯の型紙は細長いので、周の半分が長辺2本、その半分が片道1本ぶんになる。
    const auto step = [](const kachakacha::v2::geometry::Point2& from,
                          const kachakacha::v2::geometry::Point2& to) {
        const double du = to.u - from.u;
        const double dv = to.v - from.v;
        return std::sqrt(du * du + dv * dv);
    };
    double perimeter = 0.0;
    const auto& outline = model.panels.front().outline;
    for (std::size_t index = 0; index + 1 < outline.size(); ++index) {
        perimeter += step(outline[index], outline[index + 1]);
    }
    if (!(perimeter > 0.0)) {
        return false;
    }
    flatLengthMm = perimeter * 0.25;
    // 100% のときの半径の目安。90度ぶん曲げると見て `R = L / (π/2)` とする。
    // 測り方そのものは近似だが、**固定していない間だけ** 使う値である。
    radiusMm = flatLengthMm / 1.5707963267948966;
    return radiusMm > 0.0;
}

} // namespace

//! いまの近似モデルから、曲げと半径を測り直す。固定してあれば触らない。
void V2MainWindow::RefreshBendRadius()
{
    if (fabricationDock_ == nullptr) {
        return;
    }
    const auto modelId = CurrentFabricationModelId();
    const auto found = fabricationModels_.find(modelId.ToString());
    if (found == fabricationModels_.end()) {
        fabricationDock_->ShowRadius(bendRadius_, AssemblyPercentNow());
        return;
    }
    double flatLengthMm = 0.0;
    double radiusMm = 0.0;
    if (!MeasureBend(found->second, flatLengthMm, radiusMm)) {
        fabricationDock_->ShowRadius(bendRadius_, AssemblyPercentNow());
        return;
    }
    bendRadius_.flatLengthMm = flatLengthMm;
    // 固定してあるなら半径は触らない。自動なら測った値へ更新する。
    bendRadius_ = kachakacha::v2::fabrication::RefitRadius(bendRadius_, radiusMm);
    fabricationDock_->ShowRadius(bendRadius_, AssemblyPercentNow());
}

//! いまの組立率。近似モデルが無ければ 100%。
double V2MainWindow::AssemblyPercentNow() const
{
    const auto modelId = CurrentFabricationModelId();
    const auto* entity = session_->GetDocument().FindEntity(modelId);
    const auto* feature =
        entity == nullptr ? nullptr : session_->GetDocument().FindFeature(entity->createdBy);
    if (feature == nullptr) {
        return 100.0;
    }
    const auto* definition =
        std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
            &feature->definition);
    return definition == nullptr ? 100.0 : definition->masterPercent;
}

//! 「固定」「固定を外す」を押した。判断は core がする。
void V2MainWindow::ApplyBendRadius(double radiusMm, bool locked)
{
    const double percent = AssemblyPercentNow();
    if (!locked) {
        // 固定を外す。測った値へ戻す。測れていなければ、いまの値のまま自動にする。
        bendRadius_ = kachakacha::v2::fabrication::UnlockRadius(bendRadius_, 0.0);
        RefreshBendRadius();
        SetStatus(QStringLiteral("半径: 自動へ戻しました。%1")
                .arg(QString::fromStdString(
                    kachakacha::v2::fabrication::DescribeBendRadiusJa(bendRadius_,
                        percent))));
        return;
    }
    const auto locked_ = kachakacha::v2::fabrication::LockRadiusAtPercent(bendRadius_,
        radiusMm, percent);
    if (!locked_.HasValue()) {
        ReportDiagnostics(locked_.Diagnostics());
        return;
    }
    bendRadius_ = locked_.Value();
    if (fabricationDock_ != nullptr) {
        fabricationDock_->ShowRadius(bendRadius_, percent);
    }
    SetStatus(QStringLiteral("半径: %1。近似をやり直しても戻しません。")
            .arg(QString::fromStdString(
                kachakacha::v2::fabrication::DescribeBendRadiusJa(bendRadius_, percent))));
}
