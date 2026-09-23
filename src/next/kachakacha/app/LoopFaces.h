#pragma once

//! 線から面(オーナー要望 2026-09-22): **線を選ぶだけ**で、閉じた輪を全部見つけて面にする。
//!
//! V1 で楽だったのは「外形と断面を決めるやつ」だが、V2 では役割を人が決めていた。
//! ここは役割を **端点のつながり** から決める。
//!   1. 選んだ線を辺、端点(許容差でまとめる)を節にしたグラフを作る(app/LoopGraph)。
//!   2. 閉じた輪(単純閉路)を全部数える。平面かどうかは問わない(3D の線でよい)。
//!   3. 面になる輪を選ぶ: 弦(輪の隣り合わない 2 節を結ぶ選んだ線)を持つ輪は、
//!      小さい輪の合わさりなので面にしない。残りを辺の少ない順・小さい順に採り、
//!      1 本の線が 3 つ以上の面に使われる輪は採らない(1 本の線に面は表裏の 2 枚まで)。
//!   4. 輪ごとに作り方を決める: 同じ平面に載る → 平面。4 辺で平らでない → 四辺面(Coons)。
//!      それ以外 → 境界面(形は一通りに決まらない。近似)。
//!   5. 端が離れている線(行き止まりの端どうしが近い)は **ずれ** として全部挙げる。
//!      黙って寄せない。寄せるか断るかは人が決める(呼び手が LoopGapFix で寄せる)。
//!   6. **T 字**: 線の端が別の線の途中に乗っていれば、その線をそこで分けた「片」で輪を探す
//!      (Inventor/SketchUp では拘束や自動分割が担うところ。本 CAD は拘束を持たないのでここで補う)。
//!      分ける相手と位置は splits に挙げ、呼び手が Enter のときに実際の線を分ける。
//!   7. 輪が 1 つも無く、開いた線が互いに触れずに並んでいれば **ロフト**(断面の並びは重心の主軸)。
//! 元の線は消さない(T 字で分けた線は 2 本になるが、形は変わらない)。
//! ここは OCCT も Qt も呼ばない。面そのものは呼び手が kernel で作る。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

//! 輪の作り方。
enum class LoopFaceMethod {
    Planar,        //!< 平面(輪が同じ平面に載る)
    FourEdge,      //!< 四辺面(4 辺、平らでない)
    BoundaryFill,  //!< 境界面(それ以外。形は一通りに決まらない)
    Loft,          //!< ロフト(輪が無く、開いた断面が並んでいる)
};

//! その輪に使える作り方(表で人が変えるとき用)。
[[nodiscard]] std::vector<LoopFaceMethod> LoopFaceMethodChoices(std::size_t edgeCount,
    bool planar, bool loft);

//! T 字で分けたあとの「片」。faces / gaps はこの番号を指す。
//! 分けていない線は selections と同じ番号(piece i == selection i)。分けた線の片は後ろに足す。
struct LoopPiece {
    std::size_t source = 0;                          //!< 元の選択番号
    std::vector<geometry::CurveSegment> segments;    //!< 片の形
    bool split = false;                              //!< 元の線が T 字で分けられ、この番号は使わない
    bool part = false;                               //!< 分けた片(source の一部)
};

//! T 字: source の線を parameters(0..1、線に沿った位置。複数可)で分ける。
struct LoopSplit {
    std::size_t source = 0;
    std::vector<double> parameters;               //!< 昇順
    std::vector<geometry::Vector3> points;        //!< その位置
    std::vector<std::size_t> bySelections;        //!< 乗っている端を持つ線
};

[[nodiscard]] std::string LoopFaceMethodLabelJa(LoopFaceMethod method);

//! 面になる輪 1 つ。
struct LoopFace {
    std::vector<std::size_t> selections;   //!< 輪をたどる順の選択番号
    std::vector<bool> forward;             //!< その線を始点→終点の向きにたどるか
    LoopFaceMethod method = LoopFaceMethod::Planar;
    double planeDeviationMm = 0.0;         //!< 最小二乗平面からのずれ(平面判定に使った値)
    double areaMm2 = 0.0;                  //!< 囲む面積(Newell)
    std::vector<geometry::Vector3> ring;   //!< 輪を 1 周する点列(下見用。ロフトは空)
    //! 下見に描く線(輪は閉じた 1 本、ロフトは断面と端を結ぶ線)。
    std::vector<std::vector<geometry::Vector3>> previewLines;
    //! 内部用(輪を選ぶ間だけ)。外には空で出る。
    std::vector<std::size_t> edgeIndices;
};

//! 端が離れているところ。行き止まりの端どうしで、近いもの。
struct LoopGap {
    std::size_t firstSelection = 0;
    bool firstAtEnd = false;    //!< 1 本目の終点側か(偽なら始点側)
    std::size_t secondSelection = 0;
    bool secondAtEnd = false;
    double distanceMm = 0.0;
    //! 寄せられるか(少なくとも片方が 1 本の直線)。円弧やスプラインの端は動かせない。
    bool movable = false;
};

struct LoopFacePlan {
    std::vector<LoopPiece> pieces;     //!< 片。faces / gaps の番号はこれ
    std::vector<LoopSplit> splits;     //!< T 字で分ける線
    std::vector<LoopFace> faces;
    std::vector<LoopGap> gaps;
    std::vector<std::size_t> unused;   //!< どの面にも入らなかった選択番号(小さい順)
    std::string summaryJa;             //!< 「平面 3・四辺面 1・境界面 0」
};

//! 片の名前(「円弧 1(片 2)」)。
[[nodiscard]] std::string LoopPieceLabelJa(
    const std::vector<modeling::GuideTableSelection>& selections, const LoopFacePlan& plan,
    std::size_t piece);
//! T 字の一文。「直線 5 の端が 円弧 1 の途中に乗っています(円弧 1 を 1 か所で分けます)」。
[[nodiscard]] std::string LoopSplitTextJa(
    const std::vector<modeling::GuideTableSelection>& selections, const LoopSplit& split);

//! 面になる輪と、ずれを挙げる。線が 1 本も無ければ断る。線が多すぎれば UI-R012。
//! 輪が無くてずれも無ければ UI-R011。
[[nodiscard]] base::Result<LoopFacePlan> PlanLoopFaces(
    const std::vector<modeling::GuideTableSelection>& selections,
    const geometry::GeometryTolerance& tolerance);

//! 輪 1 つを、面を作る表にする(平面は外形 1 行、四辺面・境界面は辺ごとに 1 行、ロフトは断面ごと)。
//! T 字で分けた片が残っている計画(splits が空でない)では作れない(先に線を分けて計画し直す)。
[[nodiscard]] base::Result<modeling::GuideTable> LoopFaceTable(
    const std::vector<modeling::GuideTableSelection>& selections, const LoopFace& face,
    const geometry::GeometryTolerance& tolerance);

//! T 字で線を実際に分けた形(片ごとの線)。呼び手はこれを新しい線として文書へ入れる。
[[nodiscard]] base::Result<std::vector<std::vector<geometry::CurveSegment>>> SplitLoopSource(
    const std::vector<modeling::GuideTableSelection>& selections, const LoopSplit& split);

//! ずれを寄せた線。直線どうしなら両方を中点へ、片方が直線ならその端を相手の端へ動かす。
//! 動かせない(どちらも直線でない)なら断る(GEO-E011)。
struct LoopGapFix {
    std::optional<geometry::CurveSegment> first;    //!< 動かした 1 本目(動かさなければ無し)
    std::optional<geometry::CurveSegment> second;
    double movedMm = 0.0;
};

[[nodiscard]] base::Result<LoopGapFix> CloseLoopGap(
    const std::vector<modeling::GuideTableSelection>& selections, const LoopGap& gap);

//! ずれの一文。「直線 と 円弧 の端が 3.082 mm 離れています」。
[[nodiscard]] std::string LoopGapTextJa(
    const std::vector<modeling::GuideTableSelection>& selections, const LoopGap& gap);

} // namespace kachakacha::v2::app
