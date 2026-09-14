#pragma once

//! 帯の境目を人が決める(§32、Codex Q1-Q5-R2 B2)。
//!
//! 帯近似の分け方は、**分割方向のパラメータ(0〜1)の並び**で決まる。
//! 先頭が 0、末尾が 1 で、間が部材の境目である。
//! 2枚を1枚にするのは、その間の境目を1本抜くこと。
//! 1枚を2枚に分けるのは、その部材の中に1本足すこと。
//!
//! ここが大事な点である。**見せる形と、決めたあとに使う形を、同じものにする。**
//! 「分けられます」と言った相手と、実際に変える境目が別物だと、
//! 見せた前後の姿と出来上がりが食い違う。だからこの関数は
//! 「できるか」と「できたあとの境目の並び」を **一緒に** 返す。

#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::fabrication {

//! 帯の境目をいじる前と後。人が決める前に見せ、決めたらそのまま使う。
struct BandPartitionPreview {
    bool possible = false;
    std::size_t partsBefore = 0;
    std::size_t partsAfter = 0;
    //! 変えたあとの境目の並び(両端の 0 と 1 を含む)。possible のときだけ意味がある。
    std::vector<double> railParameters;
    //! 相手になった部材の、変える前と後の幅(mm)。
    double widthBeforeMm = 0.0;
    double firstWidthMm = 0.0;
    double secondWidthMm = 0.0;
    std::string messageJa;
};

//! その並びが帯の境目として正しいか(昇順・0 で始まり 1 で終わる・重複しない)。
[[nodiscard]] bool ValidRailParameters(const std::vector<double>& railParameters);

//! 部材 `which`(0 起点)を、その真ん中で2枚に分けたらどうなるか。
//!
//! `bandWidthsMm` は部材ごとの、曲げる向きの幅。細くなりすぎる分け方は断る。
//! 細い帯は折るところが残らず、作っても形にならない。
[[nodiscard]] BandPartitionPreview PreviewBandSplit(
    const std::vector<double>& railParameters, const std::vector<double>& bandWidthsMm,
    std::size_t which, double minimumPartWidthMm);

//! 部材 `first` と `first + 1` を1枚にしたらどうなるか。
//!
//! 隣り合っていない2枚は、そもそも番号が隣でないので断る。
//! 離れた紙が1枚として型紙に出ると、切り出しても組み立てられない。
[[nodiscard]] BandPartitionPreview PreviewBandMerge(
    const std::vector<double>& railParameters, const std::vector<double>& bandWidthsMm,
    std::size_t first);

//! 「3枚 → 4枚。部材2(12.24mm)を 6.12mm と 6.12mm に分けます」のような一文。
[[nodiscard]] std::string DescribeBandPartitionJa(const BandPartitionPreview& preview);

} // namespace kachakacha::v2::fabrication
