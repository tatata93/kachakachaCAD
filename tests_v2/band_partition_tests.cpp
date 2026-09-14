// 帯の境目を人が決める(§32、Codex Q1-Q5-R2 B2)。
//
// ここで見るのは **見せる形と、決めたあとに使う形が同じものか** である。
// 「分けられます」と言った相手と、実際に変える境目が別物だと、
// 見せた前後の姿と出来上がりが食い違う。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/BandPartition.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using kachakacha::v2::fabrication::BandPartitionPreview;
using kachakacha::v2::fabrication::DescribeBandPartitionJa;
using kachakacha::v2::fabrication::PreviewBandMerge;
using kachakacha::v2::fabrication::PreviewBandSplit;
using kachakacha::v2::fabrication::ValidRailParameters;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireNear;

namespace {

//! 3枚に切ってある帯。境目は 0 / 0.3 / 0.7 / 1。
[[nodiscard]] std::vector<double> ThreeParts()
{
    return {0.0, 0.3, 0.7, 1.0};
}

//! それぞれの幅(mm)。全体で 40mm の帯を上の割合で切ったもの。
[[nodiscard]] std::vector<double> ThreeWidths()
{
    return {12.0, 16.0, 12.0};
}

} // namespace

KACHA_V2_TEST(band_partition, 境目の並びが正しいかを見分ける)
{
    Require(ValidRailParameters(ThreeParts()), "正しい並び");
    Require(!ValidRailParameters({}), "空は正しくない");
    Require(!ValidRailParameters({0.0}), "1本だけは正しくない");
    Require(!ValidRailParameters({0.2, 1.0}), "0 から始まらない");
    Require(!ValidRailParameters({0.0, 0.8}), "1 で終わらない");
    Require(!ValidRailParameters({0.0, 0.5, 0.5, 1.0}), "重なっている");
    Require(!ValidRailParameters({0.0, 0.7, 0.3, 1.0}), "順番が逆");
}

KACHA_V2_TEST(band_partition, 分けると境目が1本増える)
{
    const auto preview = PreviewBandSplit(ThreeParts(), ThreeWidths(), 1, 4.0);
    Require(preview.possible, "分けられる");
    Require(preview.partsBefore == 3, "前は3枚");
    Require(preview.partsAfter == 4, "後は4枚");
    Require(preview.railParameters.size() == 5, "境目は5本");
    // 真ん中に入る。0.3 と 0.7 の間は 0.5。
    RequireNear(preview.railParameters[2], 0.5, 1.0e-12, "新しい境目は真ん中");
    // 両端は動かさない。
    RequireNear(preview.railParameters.front(), 0.0, 1.0e-12, "先頭は 0");
    RequireNear(preview.railParameters.back(), 1.0, 1.0e-12, "末尾は 1");
    Require(ValidRailParameters(preview.railParameters), "出来た並びも正しい");
    // 幅も言う。人はここを見て決める。
    RequireNear(preview.widthBeforeMm, 16.0, 1.0e-12, "前の幅");
    RequireNear(preview.firstWidthMm, 8.0, 1.0e-12, "半分");
    RequireNear(preview.secondWidthMm, 8.0, 1.0e-12, "もう半分");
}

KACHA_V2_TEST(band_partition, 細くなりすぎる分け方は断る)
{
    // 折るところが残らない帯は、作っても形にならない。
    const auto preview = PreviewBandSplit(ThreeParts(), ThreeWidths(), 0, 8.0);
    Require(!preview.possible, "断ること");
    Require(preview.railParameters.empty(), "断ったら境目を出さない");
    Require(preview.messageJa.find("細い") != std::string::npos, "理由を言う");
    // 基準を下げれば通る。禁止ではなく、基準の話である。
    Require(PreviewBandSplit(ThreeParts(), ThreeWidths(), 0, 4.0).possible,
        "基準を下げれば通る");
}

KACHA_V2_TEST(band_partition, 無い部材の番号は断る)
{
    // **丸めない。** 999 と書いたら、黙って最後の部材を分けるのではなく断る。
    for (const std::size_t which : {std::size_t{3}, std::size_t{999}}) {
        const auto preview = PreviewBandSplit(ThreeParts(), ThreeWidths(), which, 4.0);
        Require(!preview.possible, "断ること: " + std::to_string(which));
        Require(preview.railParameters.empty(), "境目を出さない");
        Require(preview.messageJa.find("その部材がありません") != std::string::npos,
            "理由を言う");
    }
}

KACHA_V2_TEST(band_partition, 1つにすると境目が1本減る)
{
    const auto preview = PreviewBandMerge(ThreeParts(), ThreeWidths(), 0);
    Require(preview.possible, "1つにできる");
    Require(preview.partsBefore == 3, "前は3枚");
    Require(preview.partsAfter == 2, "後は2枚");
    Require(preview.railParameters.size() == 3, "境目は3本");
    RequireNear(preview.railParameters[1], 0.7, 1.0e-12, "抜けたのは 0.3 のほう");
    Require(ValidRailParameters(preview.railParameters), "出来た並びも正しい");
    RequireNear(preview.widthBeforeMm, 28.0, 1.0e-12, "1枚になった幅");
}

KACHA_V2_TEST(band_partition, 隣が無い番号は断る)
{
    // 最後の部材には次が無い。
    const auto preview = PreviewBandMerge(ThreeParts(), ThreeWidths(), 2);
    Require(!preview.possible, "断ること");
    Require(preview.messageJa.find("隣り合っていません") != std::string::npos,
        "理由を言う");
    Require(!PreviewBandMerge({0.0, 1.0}, {40.0}, 0).possible, "1枚しかなければ断る");
}

KACHA_V2_TEST(band_partition, 分けてから1つにすると元へ戻る)
{
    // 見せた候補をそのまま使えば、往復して元へ戻る。
    const auto split = PreviewBandSplit(ThreeParts(), ThreeWidths(), 1, 4.0);
    Require(split.possible, "分けられる");
    const std::vector<double> widths{12.0, 8.0, 8.0, 12.0};
    const auto merged = PreviewBandMerge(split.railParameters, widths, 1);
    Require(merged.possible, "1つにできる");
    Require(merged.railParameters.size() == ThreeParts().size(), "本数が戻る");
    for (std::size_t index = 0; index < ThreeParts().size(); ++index) {
        RequireNear(merged.railParameters[index], ThreeParts()[index], 1.0e-12,
            "位置も戻る");
    }
}

KACHA_V2_TEST(band_partition, 前と後を一文で言える)
{
    const auto preview = PreviewBandSplit(ThreeParts(), ThreeWidths(), 1, 4.0);
    const std::string text = DescribeBandPartitionJa(preview);
    Require(text.find("3枚 → 4枚") != std::string::npos, "枚数を言う");
    Require(text.find("部材2") != std::string::npos, "相手を言う");
    const auto refused = PreviewBandSplit(ThreeParts(), ThreeWidths(), 9, 4.0);
    Require(DescribeBandPartitionJa(refused) == refused.messageJa,
        "断ったときは理由だけを言う");
}

KACHA_V2_TEST(band_partition, 有限でない値をすべて断る)
{
    // Codex Q1-Q5-R3 B4。NaN との比較はどちらもなりたたないので、
    // 大小を比べるだけだと末尾の NaN が素通りする。先に全部を見る。
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    Require(!ValidRailParameters({0.0, 0.5, nan}), "末尾が NaN");
    Require(!ValidRailParameters({0.0, nan, 1.0}), "途中が NaN");
    Require(!ValidRailParameters({nan, 0.5, 1.0}), "先頭が NaN");
    Require(!ValidRailParameters({0.0, inf, 1.0}), "途中が無限大");
    Require(!ValidRailParameters({0.0, -inf, 1.0}), "途中が負の無限大");

    // 幅と、細すぎる基準も同じように見る。
    Require(!PreviewBandSplit(ThreeParts(), {12.0, nan, 12.0}, 1, 4.0).possible,
        "幅が NaN なら断る");
    Require(!PreviewBandSplit(ThreeParts(), {12.0, inf, 12.0}, 1, 4.0).possible,
        "幅が無限大なら断る");
    Require(!PreviewBandSplit(ThreeParts(), {12.0, -1.0, 12.0}, 1, 4.0).possible,
        "幅が負なら断る");
    Require(!PreviewBandSplit(ThreeParts(), ThreeWidths(), 1, nan).possible,
        "基準が NaN なら断る");
    Require(!PreviewBandSplit(ThreeParts(), ThreeWidths(), 1, -1.0).possible,
        "基準が負なら断る");
    Require(!PreviewBandMerge(ThreeParts(), {12.0, nan, 12.0}, 0).possible,
        "1つにするときも幅を見る");
    // 断ったときは境目を出さない。出すと、それを書き込んでしまう。
    Require(PreviewBandSplit(ThreeParts(), ThreeWidths(), 1, nan).railParameters.empty(),
        "断ったら境目を出さない");
}

KACHA_V2_TEST(band_partition, 分けても変えていない部材の値は残る)
{
    // Codex Q1-Q5-R3 B3。3枚目に半径を固定してあるのに、
    // 1枚目を分けたせいでそれが消えるのは、利用者の入力を勝手に捨てることである。
    kachakacha::v2::fabrication::BandValueRemap before;
    before.bandProgress = {0.3, 0.5, 0.7};
    before.bendRadiusMm = {0.0, 0.0, 8.5};
    before.bendRadiusLock = {0, 0, 1};
    before.creaseProgress = {0.4, 0.6};
    before.unfoldBaseRail = 2;

    const auto after = kachakacha::v2::fabrication::RemapForSplit(before, 3, 0);
    Require(after.bandProgress.size() == 4, "部材が4枚になる");
    // 分けた2枚は元の値を引き継ぐ。
    RequireNear(after.bandProgress[0], 0.3, 1.0e-12, "分けた片方");
    RequireNear(after.bandProgress[1], 0.3, 1.0e-12, "分けたもう片方");
    // **後ろの部材の値はずれるだけで消えない。**
    RequireNear(after.bandProgress[2], 0.5, 1.0e-12, "2枚目だったもの");
    RequireNear(after.bandProgress[3], 0.7, 1.0e-12, "3枚目だったもの");
    Require(after.bendRadiusLock.size() == 4, "固定の数も合う");
    Require(after.bendRadiusLock[3] == 1, "3枚目の固定が残る");
    RequireNear(after.bendRadiusMm[3], 8.5, 1.0e-12, "3枚目の半径が残る");
    // 展開の基準は、分けた場所より後ろなので1つずれる。
    Require(after.unfoldBaseRail == 3, "基準の辺がずれる");
}

KACHA_V2_TEST(band_partition, 1つにすると後ろの1枚の値だけを捨てる)
{
    kachakacha::v2::fabrication::BandValueRemap before;
    before.bandProgress = {0.3, 0.5, 0.7};
    before.bendRadiusMm = {22.0, 9.0, 8.5};
    before.bendRadiusLock = {1, 1, 1};
    before.creaseProgress = {0.4, 0.6};
    before.unfoldBaseRail = 2;

    const auto after = kachakacha::v2::fabrication::RemapForMerge(before, 3, 0);
    Require(after.bandProgress.size() == 2, "部材が2枚になる");
    RequireNear(after.bendRadiusMm[0], 22.0, 1.0e-12, "先の1枚の値が残る");
    RequireNear(after.bendRadiusMm[1], 8.5, 1.0e-12, "3枚目の値も残る");
    // 捨てたものは黙っていない。
    Require(after.droppedParts.size() == 1, "捨てた部材を1つ言う");
    Require(after.droppedParts.front() == 2, "捨てたのは部材2");
    // 消えるのは境目 first+1 = 1。基準は 2 なので、番号が1つ前へずれる。
    Require(after.unfoldBaseRail == 1, "基準の辺は番号がずれるだけ");

    // 消える辺そのものが基準だったときは、先頭へ戻す。無い辺は基準にできない。
    kachakacha::v2::fabrication::BandValueRemap onTheSeam = before;
    onTheSeam.unfoldBaseRail = 1;
    Require(kachakacha::v2::fabrication::RemapForMerge(onTheSeam, 3, 0).unfoldBaseRail == 0,
        "無くなる辺が基準なら先頭へ戻す");
}

KACHA_V2_TEST(band_partition, 数が合わない古い値は引き継がない)
{
    // 前に捨てられた並びを無理に当てると、別の部材の値が当たる。
    kachakacha::v2::fabrication::BandValueRemap before;
    before.bendRadiusMm = {22.0};      // 部材は3枚あるのに1つしか無い
    before.bendRadiusLock = {1};
    const auto after = kachakacha::v2::fabrication::RemapForSplit(before, 3, 0);
    Require(after.bendRadiusMm.empty(), "半径は引き継がない");
    Require(after.bendRadiusLock.empty(), "固定も引き継がない");
}

KACHA_V2_TEST_MAIN("band_partition_tests")
