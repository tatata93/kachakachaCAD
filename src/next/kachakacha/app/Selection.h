#pragma once

//! 選択(ui-workflows §2、V1同等)。
//!
//! 「いま何を選んでいるか」は、画面の飾りではなく作業の入口である。
//! 出力の対象も、形状ガイドの役割も、正対も、ここから始まる。
//! V1 はこれが Viewport の中にあったので、画面を出さないと確かめられなかった。
//!
//! ここでやるのは3つ。
//!   1. 画面の1点から、いちばん近い線を拾う。
//!   2. 拾ったものを、修飾キーに応じて選択へ入れる・外す。
//!   3. 選んでいるものを種類ごとに数える。
//!
//! 拾えなかったことは失敗ではない。何も無いところを押しただけである。
//! だから診断は出さず、値を持たないことで表す。

#include "kachakacha/base/Ids.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/modeling/SnapEngine.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <optional>
#include <vector>

namespace kachakacha::v2::app {

//! 修飾キーの意味。V1と同じにする。
enum class SelectionMode {
    Replace,   //!< 素で押した。前の選択は捨てる。
    Add,       //!< Shift。足す。
    Toggle,    //!< Ctrl。入っていれば外し、無ければ足す。
    Subtract,  //!< Alt。外す。
};

//! 選んでいるもの。押した順に並ぶ。同じものは1回しか入らない。
//! 順を保つのは、形状ガイドの役割が「選んだ順」で決まるためである。
struct SelectionSet {
    std::vector<base::EntityId> entityIds;
};

//! 画面上で拾ったもの。
struct PickCandidate {
    base::EntityId entityId;
    base::SegmentId segmentId;
    double distancePx = 0.0;
};

//! 拾う相手を絞る印。作図中は作業平面の上の線だけを相手にする(app/PlaneFocus)。
struct PickFocus {
    //! 作図の道具を持っているか。
    bool drawing = false;
    //! 「作図面以外の線を常に薄く」の印。外れていれば絞らない。
    bool dimOffPlane = false;
    //! いまの作業平面。drawing かつ dimOffPlane のときだけ見る。
    modeling::WorkPlaneFrame plane{};
};

//! 画面の1点から、いちばん近い線を拾う。
//! 拾う範囲は tolerance.displayPickPx。範囲の外なら何も返さない。
//! 同じ距離のものが並んだときは、場面に入っている順で先のものを返す。毎回同じ結果になる。
//!
//! focus を渡すと、薄くしている線は拾わない。薄いのに掴めると、見た目と手が食い違う。
//! 省くと全部拾う(いままでと同じ)。
[[nodiscard]] std::optional<PickCandidate> PickCurve(const modeling::SnapScene& scene,
    const geometry::ScreenMapping& mapping, const geometry::ScreenPoint& pointer,
    const geometry::GeometryTolerance& tolerance, const PickFocus& focus = {});

//! 画面の1点から、いちばん近い **作図点** を拾う(棚卸し追加、オーナー指摘 2026-09-11)。
//!
//! 点は線と同じように文書のものなのに、拾う道が無かった。
//! 「交点に点」で作った点も、「作図点」も、**選ぶことができなかった** ので、
//! 消すことも、名前を変えることも、次の操作の相手にすることもできなかった。
//!
//! 拾う範囲は線と同じ tolerance.displayPickPx。
[[nodiscard]] std::optional<PickCandidate> PickPoint(const modeling::SnapScene& scene,
    const geometry::ScreenMapping& mapping, const geometry::ScreenPoint& pointer,
    const geometry::GeometryTolerance& tolerance);

//! 点 → 線 の順で拾う。
//!
//! **点を先に見る。** 点は線の上に載っていることが多く(端点に置いた作図点、
//! 交点に置いた点)、線を先に見ると点が永久に拾えない。
[[nodiscard]] std::optional<PickCandidate> PickEntity(const modeling::SnapScene& scene,
    const geometry::ScreenMapping& mapping, const geometry::ScreenPoint& pointer,
    const geometry::GeometryTolerance& tolerance, const PickFocus& focus = {});

//! 拾ったものを選択へ入れる。拾えていなければ、Replace のときだけ空にする。
//! 足す・外すの途中で、何も無いところを押しても選択は消えない(V1と同じ)。
[[nodiscard]] SelectionSet ApplySelection(const SelectionSet& current,
    const std::optional<PickCandidate>& picked, SelectionMode mode);

[[nodiscard]] bool IsSelected(const SelectionSet& selection, base::EntityId entityId);

//! 選んでいるもののうち、その種類の数。
[[nodiscard]] int SelectedCountOfKind(const SelectionSet& selection,
    const document::DocumentSnapshot& snapshot, domain::EntityKind kind);

//! その種類を全部選ぶ。並びは文書の並びと同じ。
[[nodiscard]] SelectionSet SelectAllOfKind(const document::DocumentSnapshot& snapshot,
    domain::EntityKind kind);

//! 文書から消えたものを選択から外す。消したあとに必ず呼ぶ。
//! 外さないと、無いものを選んでいることになる。
[[nodiscard]] SelectionSet PruneSelection(const SelectionSet& selection,
    const document::DocumentSnapshot& snapshot);

//! 選んでいるものの線を集める。書き出しと測定で使う。順は選んだ順。
[[nodiscard]] std::vector<geometry::CurveSegment> SelectedCurves(
    const SelectionSet& selection, const modeling::SnapScene& scene);

} // namespace kachakacha::v2::app
