#include "kachakacha/fabrication/BandPartition.h"

#include <cmath>
#include <sstream>

namespace kachakacha::v2::fabrication {
namespace {

//! 小数を「12.24」のように2桁で書く。並べて読めるようにする。
[[nodiscard]] std::string Millimetres(double value)
{
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(2);
    text << value;
    return text.str();
}

} // namespace

bool ValidRailParameters(const std::vector<double>& railParameters)
{
    if (railParameters.size() < 2) {
        return false;
    }
    if (std::abs(railParameters.front()) > 1.0e-9
        || std::abs(railParameters.back() - 1.0) > 1.0e-9) {
        return false;
    }
    for (std::size_t index = 0; index + 1 < railParameters.size(); ++index) {
        if (!std::isfinite(railParameters[index])
            || railParameters[index + 1] <= railParameters[index] + 1.0e-9) {
            return false;
        }
    }
    return true;
}

BandPartitionPreview PreviewBandSplit(const std::vector<double>& railParameters,
    const std::vector<double>& bandWidthsMm, std::size_t which,
    double minimumPartWidthMm)
{
    BandPartitionPreview preview;
    if (!ValidRailParameters(railParameters)) {
        preview.messageJa = "いまの部材の境目が読めません。";
        return preview;
    }
    const std::size_t parts = railParameters.size() - 1;
    preview.partsBefore = parts;
    preview.partsAfter = parts;
    if (which >= parts) {
        preview.messageJa = "その部材がありません。1 から " + std::to_string(parts)
            + " までの番号を書いてください。";
        return preview;
    }
    preview.widthBeforeMm = which < bandWidthsMm.size() ? bandWidthsMm[which] : 0.0;
    preview.firstWidthMm = preview.widthBeforeMm * 0.5;
    preview.secondWidthMm = preview.widthBeforeMm * 0.5;
    // 細くなりすぎる分け方は断る。折るところが残らない帯は、作っても形にならない。
    if (minimumPartWidthMm > 0.0 && preview.widthBeforeMm > 0.0
        && preview.firstWidthMm < minimumPartWidthMm) {
        preview.messageJa = "部材" + std::to_string(which + 1) + " は "
            + Millimetres(preview.widthBeforeMm) + "mm しかないので、分けると "
            + Millimetres(preview.firstWidthMm) + "mm になります。"
            + Millimetres(minimumPartWidthMm) + "mm より細い帯は作れません。";
        return preview;
    }
    std::vector<double> next;
    next.reserve(railParameters.size() + 1);
    for (std::size_t index = 0; index < railParameters.size(); ++index) {
        next.push_back(railParameters[index]);
        if (index == which) {
            next.push_back(0.5 * (railParameters[index] + railParameters[index + 1]));
        }
    }
    preview.railParameters = std::move(next);
    preview.partsAfter = parts + 1;
    preview.possible = true;
    preview.messageJa = "部材" + std::to_string(which + 1) + "("
        + Millimetres(preview.widthBeforeMm) + "mm)を "
        + Millimetres(preview.firstWidthMm) + "mm と "
        + Millimetres(preview.secondWidthMm) + "mm に分けます。";
    return preview;
}

BandPartitionPreview PreviewBandMerge(const std::vector<double>& railParameters,
    const std::vector<double>& bandWidthsMm, std::size_t first)
{
    BandPartitionPreview preview;
    if (!ValidRailParameters(railParameters)) {
        preview.messageJa = "いまの部材の境目が読めません。";
        return preview;
    }
    const std::size_t parts = railParameters.size() - 1;
    preview.partsBefore = parts;
    preview.partsAfter = parts;
    if (parts < 2) {
        preview.messageJa = "部材が1枚しかないので、1つにできません。";
        return preview;
    }
    if (first + 1 >= parts) {
        preview.messageJa = "その2つは隣り合っていません。1 から "
            + std::to_string(parts - 1) + " までの番号を書いてください"
            + "(その番号と次の番号を1つにします)。";
        return preview;
    }
    preview.firstWidthMm = first < bandWidthsMm.size() ? bandWidthsMm[first] : 0.0;
    preview.secondWidthMm =
        first + 1 < bandWidthsMm.size() ? bandWidthsMm[first + 1] : 0.0;
    preview.widthBeforeMm = preview.firstWidthMm + preview.secondWidthMm;
    std::vector<double> next;
    next.reserve(railParameters.size() - 1);
    for (std::size_t index = 0; index < railParameters.size(); ++index) {
        if (index == first + 1) {
            continue;   // ここが2枚の間の境目。抜くと1枚になる。
        }
        next.push_back(railParameters[index]);
    }
    preview.railParameters = std::move(next);
    preview.partsAfter = parts - 1;
    preview.possible = true;
    preview.messageJa = "部材" + std::to_string(first + 1) + "("
        + Millimetres(preview.firstWidthMm) + "mm)と 部材"
        + std::to_string(first + 2) + "(" + Millimetres(preview.secondWidthMm)
        + "mm)を 1枚(" + Millimetres(preview.widthBeforeMm) + "mm)にします。"
        + "接着線が1本減りますが、丸みは粗くなります。";
    return preview;
}

std::string DescribeBandPartitionJa(const BandPartitionPreview& preview)
{
    if (!preview.possible) {
        return preview.messageJa;
    }
    return std::to_string(preview.partsBefore) + "枚 → "
        + std::to_string(preview.partsAfter) + "枚。" + preview.messageJa;
}

} // namespace kachakacha::v2::fabrication
