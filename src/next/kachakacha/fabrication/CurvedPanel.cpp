#include "kachakacha/fabrication/CurvedPanel.h"

#include "kachakacha/fabrication/Unfold.h"

#include <cmath>
#include <string>
#include <vector>

namespace kachakacha::v2::fabrication {
namespace {

using base::MakeError;

[[nodiscard]] std::string Millimeters(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.4f", value);
    return std::string(buffer) + " mm";
}

//! 帯の両縁をつないで、閉じた外周にする。
//! 片方は行き、もう片方は帰り。帰りを逆に並べないと、8の字になる。
[[nodiscard]] std::vector<geometry::Point2> LoopOf(const UnfoldedStrip& strip)
{
    std::vector<geometry::Point2> loop = strip.firstRail;
    for (std::size_t index = strip.secondRail.size(); index > 0; --index) {
        loop.push_back(strip.secondRail[index - 1]);
    }
    return loop;
}

} // namespace

base::Result<CurvedPanelResult> BuildCurvedPanel(const std::string& panelId,
    const SurfacePatchSamples& samples, double targetMaxDeviationMm)
{
    using Out = base::Result<CurvedPanelResult>;
    if (!samples.Valid()) {
        return Out::Failure(MakeError(kCurvedPanelBadSamples,
            "面の標本が足りません。",
            "行と列がそれぞれ2つ以上、点の数が行×列でなければなりません。"));
    }
    if (!(targetMaxDeviationMm > 0.0)) {
        return Out::Failure(MakeError(kCurvedPanelBadSamples,
            "許すずれが正の数ではありません。",
            "どこまでのずれなら許すかを決めてください。"));
    }

    // 縁だけを見ても分からない。中がふくらんでいても縁は一致してしまう。
    // 角欠損(離散版の Gauss-Bonnet)で、中まで見る。
    const DevelopabilityCheck check = CheckDevelopability(samples);
    if (!check.valid || check.estimatedDistortionMm > targetMaxDeviationMm) {
        return Out::Failure(MakeError(kCurvedPanelNotDevelopable,
            "この面は、伸ばさずには平らにできません。",
            "平らにすると " + Millimeters(check.estimatedDistortionMm)
                + " ずれます(許すのは " + Millimeters(targetMaxDeviationMm)
                + " まで)。球のように二重に曲がった面は、切れ目を入れるか"
                  "分けるしかありません。"));
    }

    const auto unfolded = UnfoldSamples(samples, targetMaxDeviationMm);
    if (!unfolded.HasValue()) {
        return Out::Failure(unfolded.Diagnostics());
    }
    const UnfoldedStrip& strip = unfolded.Value();
    if (strip.maximumPlanarityErrorMm > targetMaxDeviationMm) {
        // 展開の途中で残ったずれも見る。ここを飛ばすと、
        // 「展開できた」と言いながら合わない型紙が出る。
        return Out::Failure(MakeError(kCurvedPanelNotDevelopable,
            "この面は、伸ばさずには平らにできません。",
            "帯を倒したあとに " + Millimeters(strip.maximumPlanarityErrorMm)
                + " のずれが残ります(許すのは "
                + Millimeters(targetMaxDeviationMm) + " まで)。"));
    }

    CurvedPanelResult made;
    made.panel.panelId = panelId;
    made.panel.outline = LoopOf(strip);
    made.lengthErrorRelative = strip.maximumLengthErrorRelative;
    made.distortionMm = std::max(check.estimatedDistortionMm,
        strip.maximumPlanarityErrorMm);
    if (made.panel.outline.size() < 3) {
        return Out::Failure(MakeError(kCurvedPanelBadSamples,
            "展開した形が外周になりません。", "点が3つ未満です。"));
    }
    return Out::Success(std::move(made));
}

} // namespace kachakacha::v2::fabrication
