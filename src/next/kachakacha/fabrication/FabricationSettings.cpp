#include "kachakacha/fabrication/FabricationSettings.h"

#include "kachakacha/geometry/Units.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::fabrication {

using base::Diagnostic;
using base::MakeError;

namespace {

constexpr const char* kBadSetting = "FAB-S001";

} // namespace

double ResolveTargetMaxDeviationMm(const FabricationSettings& settings,
    double modelDiagonalMm)
{
    if (settings.explicitMaxDeviationMm.has_value()
        && *settings.explicitMaxDeviationMm > 0.0) {
        return *settings.explicitMaxDeviationMm;
    }
    const double diagonal = modelDiagonalMm > 0.0 ? modelDiagonalMm : 1.0;
    const int level = std::clamp(settings.fidelityLevel, 1, 10);
    const double q = static_cast<double>(level - 1) / 9.0;
    const double coarse = std::max(0.50, diagonal * 0.010);
    const double fine = std::max(0.03, diagonal * 0.0005);
    return std::exp(std::log(coarse) + (std::log(fine) - std::log(coarse)) * q);
}

std::vector<Diagnostic> ValidateFabricationSettings(const FabricationSettings& settings)
{
    std::vector<Diagnostic> errors;
    if (settings.fidelityLevel < 1 || settings.fidelityLevel > 10) {
        errors.push_back(MakeError(kBadSetting, "再現度は1から10の間です。",
            std::to_string(settings.fidelityLevel)));
    }
    if (settings.explicitMaxDeviationMm.has_value()
        && !(*settings.explicitMaxDeviationMm > 0.0
            && geometry::IsFinite(*settings.explicitMaxDeviationMm))) {
        errors.push_back(MakeError(kBadSetting, "最大偏差は正の値です。", {}));
    }
    if (settings.panelCountLimit < 1 || settings.panelCountLimit > 200) {
        errors.push_back(MakeError(kBadSetting, "部材数の上限は1から200の間です。",
            std::to_string(settings.panelCountLimit)));
    }
    if (!(settings.minimumPanelWidthMm > 0.0)) {
        errors.push_back(MakeError(kBadSetting, "部材の最小幅は正の値です。", {}));
    }
    if (!(settings.maximumReliefDepthRatio >= 0.0
            && settings.maximumReliefDepthRatio <= 0.95)) {
        errors.push_back(MakeError(kBadSetting,
            "切れ目の深さの上限は0から0.95の間です。", {}));
    }
    if (!(settings.minimumLigamentMm > 0.0)) {
        errors.push_back(MakeError(kBadSetting, "残す幅は正の値です。", {}));
    }
    if (!(settings.outputThicknessMm > 0.0)) {
        errors.push_back(MakeError(kBadSetting, "板厚は正の値です。", {}));
    }
    if (!settings.preserveOpenings) {
        // §3「常にtrueであり、UIで無効化してはならない」。
        errors.push_back(MakeError(kBadSetting, "開口は必ず残します。",
            "この設定は切れません。"));
    }
    if (!settings.allowedPanelTypes.planar && !settings.allowedPanelTypes.cylindrical
        && !settings.allowedPanelTypes.conical
        && !settings.allowedPanelTypes.tangentDevelopable) {
        errors.push_back(MakeError(kBadSetting, "使える部材の種類が1つもありません。",
            "少なくとも1つは許可してください。"));
    }
    return errors;
}

} // namespace kachakacha::v2::fabrication
