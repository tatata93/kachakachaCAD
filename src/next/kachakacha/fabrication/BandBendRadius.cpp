#include "kachakacha/fabrication/BandBendRadius.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <string>

namespace kachakacha::v2::fabrication {
namespace {

//! 帯 band の「曲げる向きの幅」を、展開図の真ん中の列で測る。
//!
//! 端の列で測ると、扇形に開いた帯では幅が実際より広い。真ん中を使う。
[[nodiscard]] double BandWidthMm(const BandMesh& mesh, int band)
{
    const auto row = static_cast<std::size_t>(band);
    if (row + 1 >= mesh.developed.size()) {
        return 0.0;
    }
    const auto& bottom = mesh.developed[row];
    const auto& top = mesh.developed[row + 1];
    if (bottom.empty() || top.empty()) {
        return 0.0;
    }
    const std::size_t column = std::min(bottom.size(), top.size()) / 2;
    const double du = top[column].u - bottom[column].u;
    const double dv = top[column].v - bottom[column].v;
    return std::sqrt(du * du + dv * dv);
}

//! 帯 band の先にある折り線の角。最後の帯には折り線が無い。
[[nodiscard]] double CreaseAngleAfterBand(const std::vector<double>& creaseAnglesRad,
    int band)
{
    const auto rail = static_cast<std::size_t>(band);
    if (rail >= creaseAnglesRad.size()) {
        return 0.0;
    }
    return std::abs(creaseAnglesRad[rail]);
}

} // namespace

std::vector<BendRadius> MeasureBandBendRadii(const BandMesh& mesh,
    const std::vector<double>& creaseAnglesRad)
{
    std::vector<BendRadius> bends;
    const int bandCount = mesh.BandCount();
    bends.reserve(static_cast<std::size_t>(std::max(bandCount, 0)));
    for (int band = 0; band < bandCount; ++band) {
        BendRadius bend;
        // その折り線が受け持つ長さは、両隣の帯の半分ずつである。
        // 円を多角形で近似したときの `θ = (w_i + w_{i+1}) / (2R)` がこれで、
        // 端の帯だけ幅が違っても正しい半径が出る。
        // 片側の幅だけで割ると、最後の細い帯の手前で半径が跳ね上がる。
        bend.flatLengthMm = 0.5 * (BandWidthMm(mesh, band) + BandWidthMm(mesh, band + 1));
        const double angle = CreaseAngleAfterBand(creaseAnglesRad, band);
        // 角が 0 なら、そこは平らである。半径は「無い」。0 を入れて、
        // 画面には「平ら」と出す。無限大を作らない。
        bend.radiusMm = (angle > 1.0e-9 && bend.flatLengthMm > 0.0)
            ? bend.flatLengthMm / angle
            : 0.0;
        bend.lock = ValueLock::Auto;
        bends.push_back(bend);
    }
    return bends;
}

std::vector<BendRadius> ApplyStoredBendRadii(const std::vector<BendRadius>& measured,
    const std::vector<double>& radiiMm, const std::vector<int>& lockFlags)
{
    std::vector<BendRadius> bends = measured;
    // 数が合わない古い値は使わない。帯の数は近似をやり直すと変わる。
    if (radiiMm.size() != bends.size() || lockFlags.size() != bends.size()) {
        return bends;
    }
    for (std::size_t index = 0; index < bends.size(); ++index) {
        if (lockFlags[index] == 0) {
            continue;
        }
        if (!(radiiMm[index] > 0.0) || !std::isfinite(radiiMm[index])) {
            continue;   // 壊れた値は無視して自動のまま。開けなくしない。
        }
        bends[index].radiusMm = radiiMm[index];
        bends[index].lock = ValueLock::Locked;
    }
    return bends;
}

std::vector<double> BendRadiusCreaseFactors(const BandMesh& mesh,
    const std::vector<double>& creaseAnglesRad, const std::vector<BendRadius>& bends)
{
    const auto creases = static_cast<std::size_t>(std::max(mesh.CreaseCount(), 0));
    std::vector<double> factors(creases, 1.0);
    for (std::size_t rail = 0; rail < creases; ++rail) {
        if (rail >= bends.size()) {
            break;
        }
        const BendRadius& bend = bends[rail];
        if (bend.lock != ValueLock::Locked || !(bend.radiusMm > 0.0)) {
            continue;
        }
        const double measured = CreaseAngleAfterBand(creaseAnglesRad,
            static_cast<int>(rail));
        const double width = bend.flatLengthMm > 0.0
            ? bend.flatLengthMm
            : 0.5
                * (BandWidthMm(mesh, static_cast<int>(rail))
                    + BandWidthMm(mesh, static_cast<int>(rail) + 1));
        if (!(measured > 1.0e-9) || !(width > 0.0)) {
            continue;   // もともと平らなところは、半径を入れても曲げない。
        }
        const double wanted = width / bend.radiusMm;
        factors[rail] = std::clamp(wanted / measured, 0.0, 4.0);
    }
    return factors;
}

std::string DescribeBandBendRadiiJa(const std::vector<BendRadius>& bends, double percent)
{
    if (bends.empty()) {
        return "曲げる部材がありません。";
    }
    std::ostringstream text;
    std::size_t shown = 0;
    for (std::size_t index = 0; index < bends.size(); ++index) {
        if (bends[index].radiusMm <= 0.0) {
            continue;   // 平らな帯は出さない。並べても読めない。
        }
        if (shown > 0) {
            text << " / ";
        }
        text << "部材" << (index + 1) << " "
             << DescribeBendRadiusJa(bends[index], percent);
        ++shown;
        if (shown >= 4) {
            text << " ほか";
            break;
        }
    }
    if (shown == 0) {
        return "どの部材も平らです。";
    }
    return text.str();
}

} // namespace kachakacha::v2::fabrication
