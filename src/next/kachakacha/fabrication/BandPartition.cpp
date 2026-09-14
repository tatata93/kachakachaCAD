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
    // **先に全部の値が有限かを見る。** あとで大小を比べるだけだと、
    // NaN との比較はどちらもなりたたないので、末尾が NaN の並びが
    // 「正しい」ことになって通ってしまう(Codex Q1-Q5-R3 B4)。
    for (const double value : railParameters) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    if (std::abs(railParameters.front()) > 1.0e-9
        || std::abs(railParameters.back() - 1.0) > 1.0e-9) {
        return false;
    }
    for (std::size_t index = 0; index + 1 < railParameters.size(); ++index) {
        if (railParameters[index + 1] <= railParameters[index] + 1.0e-9) {
            return false;
        }
    }
    return true;
}

namespace {

//! 幅の並びが読めるか。有限でない幅が混じっていたら、最小幅の判定ができない。
[[nodiscard]] bool ValidWidths(const std::vector<double>& widthsMm)
{
    for (const double value : widthsMm) {
        if (!std::isfinite(value) || value < 0.0) {
            return false;
        }
    }
    return true;
}

} // namespace

BandPartitionPreview PreviewBandSplit(const std::vector<double>& railParameters,
    const std::vector<double>& bandWidthsMm, std::size_t which,
    double minimumPartWidthMm)
{
    BandPartitionPreview preview;
    if (!ValidRailParameters(railParameters)) {
        preview.messageJa = "いまの部材の境目が読めません。";
        return preview;
    }
    if (!ValidWidths(bandWidthsMm)) {
        preview.messageJa = "部材の幅が読めません。";
        return preview;
    }
    if (!std::isfinite(minimumPartWidthMm) || minimumPartWidthMm < 0.0) {
        preview.messageJa = "細すぎる部材の基準が読めません。0 以上の数にしてください。";
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
    if (!ValidWidths(bandWidthsMm)) {
        preview.messageJa = "部材の幅が読めません。";
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



namespace {

//! 並びの長さが合っているときだけ引き継ぐ。合わなければ空にする。
//! 前に捨てられた並びを無理に当てると、別の部材の値が当たる。
template<class T>
[[nodiscard]] bool Usable(const std::vector<T>& values, std::size_t expected)
{
    return values.size() == expected;
}

//! 部材の並びを、分けたぶんだけ伸ばす。分けた部材は2枚とも同じ値を引き継ぐ。
template<class T>
[[nodiscard]] std::vector<T> SplitList(const std::vector<T>& values, std::size_t parts,
    std::size_t which)
{
    if (!Usable(values, parts)) {
        return {};
    }
    std::vector<T> next;
    next.reserve(parts + 1);
    for (std::size_t index = 0; index < parts; ++index) {
        next.push_back(values[index]);
        if (index == which) {
            next.push_back(values[index]);
        }
    }
    return next;
}

//! 部材の並びを、1つにしたぶんだけ縮める。先の1枚の値を残す。
template<class T>
[[nodiscard]] std::vector<T> MergeList(const std::vector<T>& values, std::size_t parts,
    std::size_t first)
{
    if (!Usable(values, parts)) {
        return {};
    }
    std::vector<T> next;
    next.reserve(parts - 1);
    for (std::size_t index = 0; index < parts; ++index) {
        if (index == first + 1) {
            continue;   // 後ろの1枚は捨てる。2つの値は両立しない。
        }
        next.push_back(values[index]);
    }
    return next;
}

} // namespace

BandValueRemap RemapForSplit(const BandValueRemap& before, std::size_t partsBefore,
    std::size_t which)
{
    BandValueRemap after;
    if (partsBefore == 0 || which >= partsBefore) {
        return after;
    }
    after.bandProgress = SplitList(before.bandProgress, partsBefore, which);
    after.bendRadiusMm = SplitList(before.bendRadiusMm, partsBefore, which);
    after.bendRadiusLock = SplitList(before.bendRadiusLock, partsBefore, which);
    // 折り線は部材どうしの境目なので、部材より1本少ない。
    // 分けると、分けた場所に1本増える。増えた折り線は、まだ曲げていない扱いにする。
    if (partsBefore >= 1 && before.creaseProgress.size() + 1 == partsBefore) {
        std::vector<double> creases;
        creases.reserve(before.creaseProgress.size() + 1);
        for (std::size_t index = 0; index < before.creaseProgress.size(); ++index) {
            if (index == which) {
                // 新しい折り線は、両隣と同じ進み方にしておく。急に折れない。
                creases.push_back(before.creaseProgress[index]);
            }
            creases.push_back(before.creaseProgress[index]);
        }
        if (which + 1 == partsBefore && !before.creaseProgress.empty()) {
            creases.push_back(before.creaseProgress.back());
        }
        after.creaseProgress = std::move(creases);
    }
    // 展開の基準にする辺は、分けた場所より後ろなら1つずれる。
    after.unfoldBaseRail = before.unfoldBaseRail > static_cast<int>(which)
        ? before.unfoldBaseRail + 1
        : before.unfoldBaseRail;
    return after;
}

BandValueRemap RemapForMerge(const BandValueRemap& before, std::size_t partsBefore,
    std::size_t first)
{
    BandValueRemap after;
    if (partsBefore < 2 || first + 1 >= partsBefore) {
        return after;
    }
    after.bandProgress = MergeList(before.bandProgress, partsBefore, first);
    after.bendRadiusMm = MergeList(before.bendRadiusMm, partsBefore, first);
    after.bendRadiusLock = MergeList(before.bendRadiusLock, partsBefore, first);
    // 捨てた部材を言う。黙って消さない。
    //
    // **その部材に人が入れたものを全部見る。**半径の固定だけを見ていたので、
    // 組立率だけを入れてある部材は黙って消えていた(Codex Q1-Q5-R4 B2)。
    const std::size_t dropped = first + 1;
    const bool hadRadius = Usable(before.bendRadiusLock, partsBefore)
        && before.bendRadiusLock[dropped] != 0;
    // 組立率は「全体と違う値が入っている」ときだけ人が入れたものとみなす。
    // 全部同じなら、1つにしても失われるものは無い。
    const bool hadProgress = Usable(before.bandProgress, partsBefore)
        && std::abs(before.bandProgress[dropped] - before.bandProgress[first]) > 1.0e-9;
    if (hadRadius) {
        after.droppedValues.push_back({dropped + 1, "曲げ半径"});
    }
    if (hadProgress) {
        after.droppedValues.push_back({dropped + 1, "組立率"});
    }
    if (hadRadius || hadProgress) {
        after.droppedParts.push_back(dropped + 1);   // 1 起点で言う
    }
    if (before.creaseProgress.size() + 1 == partsBefore) {
        std::vector<double> creases;
        creases.reserve(before.creaseProgress.size() - 1);
        for (std::size_t index = 0; index < before.creaseProgress.size(); ++index) {
            if (index == first) {
                continue;   // 消える折り線
            }
            creases.push_back(before.creaseProgress[index]);
        }
        after.creaseProgress = std::move(creases);
    }
    // 消える辺が基準だったら、先頭へ戻す。無い辺は基準にできない。
    if (before.unfoldBaseRail == static_cast<int>(first) + 1) {
        after.unfoldBaseRail = 0;
    } else {
        after.unfoldBaseRail = before.unfoldBaseRail > static_cast<int>(first)
            ? before.unfoldBaseRail - 1
            : before.unfoldBaseRail;
    }
    return after;
}

} // namespace kachakacha::v2::fabrication
