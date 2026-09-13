#pragma once

//! 選択(ui-workflows §2、V1同等)。
//!
//! 「いま何を選んでいるか」は、画面の飾りではなく作業の入口である。
//! 出力の対象も、形状ガイドの役割も、正対も、ここから始まる。
//! V1 はこれが Viewport の中にあったので、画面を出さないと確かめられなかった。
//!
//! ここでやるのは4つ。
//!   1. 画面の1点から、いちばん近い線を拾う。
//!   2. 画面の矩形から、引いた向きに応じて包含か交差で対象を集める。
//!   3. 拾ったものを、修飾キーに応じて選択へ入れる・外す。
//!   4. 選んでいるものを種類ごとに数える。
//!
//! 拾えなかったことは失敗ではない。何も無いところを押しただけである。
//! だから診断は出さず、値を持たないことで表す。

#include "kachakacha/base/Ids.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/modeling/SnapEngine.h"
#include "kachakacha/modeling/SubshapeKey.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <optional>
#include <vector>

namespace kachakacha::v2::app {

//! 選択の更新方法。UI の既定は Replace、Ctrl は Toggle。
//! Add / Subtract はコマンドや試験から明示的に使うために残すが、
//! Shift / Alt には割り当てない。Shift は作図拘束、Alt は奥候補の選択に使う。
enum class SelectionMode {
    Replace,   //!< 素で押した。前の選択は捨てる。
    Add,       //!< 明示的に足す。
    Toggle,    //!< Ctrl。入っていれば外し、無ければ足す。
    Subtract,  //!< 明示的に外す。
};

//! 物体の中のどこを選んだか。EntityId だけへ潰してはならない。
enum class SelectionElementKind {
    Object,
    Vertex,
    Edge,
    Face,
    ControlPoint,
    WorkPlane,
};

//! 選択の正本。命中位置などは操作中だけの情報で、.kcd には保存しない。
struct SelectionRef {
    base::EntityId entityId;
    SelectionElementKind kind = SelectionElementKind::Object;
    std::optional<base::SegmentId> segmentId;
    std::optional<modeling::SubshapeKey> subshapeKey;
    std::optional<double> curveParameter;
    geometry::Vector3 hitPoint{};
    double screenDistancePx = 0.0;
};

//! 選んでいるもの。ordered が正本で、押した順と部分要素を保つ。
//! entityIds は V2 移植途中のコマンド向け互換投影で、物体IDを重複なく並べる。
//! 新しい処理は ordered を使い、EntityId だけへ潰してはならない。
struct SelectionSet {
    std::vector<base::EntityId> entityIds;
    std::vector<SelectionRef> ordered;
};

using SelectionSnapshot = SelectionSet;

//! 画面上で拾ったもの。
struct PickCandidate {
    base::EntityId entityId;
    base::SegmentId segmentId;
    SelectionElementKind kind = SelectionElementKind::Object;
    std::optional<modeling::SubshapeKey> subshapeKey;
    std::optional<double> curveParameter;
    geometry::Vector3 hitPoint{};
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

//! 画面の1点にある選択候補を、優先順位と距離の順で全て返す。
//! 点を線より先にし、同順位では近い候補を先にする。同じEntityの別Segmentは潰さない。
[[nodiscard]] std::vector<PickCandidate> CollectPickCandidates(
    const modeling::SnapScene& scene, const geometry::ScreenMapping& mapping,
    const geometry::ScreenPoint& pointer, const geometry::GeometryTolerance& tolerance,
    const PickFocus& focus = {});

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

//! 画面上の矩形(logical px)。左上と右下へ正規化して持つ。
//! 引いた向きは kind が持つので、ここは向きを覚えない。
struct ScreenBox {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;

    [[nodiscard]] bool Contains(const geometry::ScreenPoint& point) const noexcept;
};

//! 矩形の取り方。引いた向きだけで決まる(ui-ux-integrated-spec §4.2)。
//!
//! 向きに意味を持たせるのは、同じ手つきで「囲って選ぶ」と「触って選ぶ」を
//! 使い分けるためである。修飾キーへ割り当てると、Ctrl の追加・解除と衝突する。
enum class BoxSelectionKind {
    Contained,  //!< 左から右。矩形に完全に入ったものだけ。
    Crossing,   //!< 右から左。矩形に触れたものも。
};

//! 引いた2点から決まる矩形選択。
struct BoxSelection {
    ScreenBox box;
    BoxSelectionKind kind = BoxSelectionKind::Contained;
};

//! これ未満の移動は矩形ではなく「押しただけ」(logical px)。
//! 掴んで動かす門(GrabToMove の 4px)とは別の値である。混ぜない。
[[nodiscard]] double BoxSelectionMinimumDragPx() noexcept;

//! 引いた量が矩形選択として扱える大きさか。
//! 小さな手ぶれで矩形選択が始まると、ただのクリックが選択の入れ替えになる。
[[nodiscard]] bool BoxSelectionIsMeaningful(const geometry::ScreenPoint& start,
    const geometry::ScreenPoint& end) noexcept;

//! 引いた2点から矩形と取り方を決める。start.x <= end.x なら完全包含。
[[nodiscard]] BoxSelection MakeBoxSelection(const geometry::ScreenPoint& start,
    const geometry::ScreenPoint& end) noexcept;

//! 線分が矩形に触れるか。端が中にあるか、枠を横切れば触れている。
[[nodiscard]] bool BoxTouchesSegment(const ScreenBox& box,
    const geometry::ScreenPoint& start, const geometry::ScreenPoint& end) noexcept;

//! 物体が矩形にどう入っているか。
//!
//! 「触れたか」と「全部入ったか」を分けて持つ。片方だけでは、
//! 左から右(包含)と右から左(交差)を同じ集計から出せない。
//! 曲線の弦でも、三角形網の稜線でも、同じ規則で数える。
struct BoxReach {
    //! 1か所でも矩形に触れた。
    bool touched = false;
    //! 画面へ写せた点がすべて矩形の中にあり、写せない点も無かった。
    bool fullyInside = true;
    //! 画面へ写せた点が1つでもあった。
    bool projectable = false;
    //! 最初に矩形へ入った点。触れていなければ値を持たない。
    std::optional<geometry::Vector3> hitPoint;

    //! その取り方で選ばれるか。
    [[nodiscard]] bool Taken(BoxSelectionKind kind) const noexcept;
};

//! 折れ線を1本足す。点は画面へ写してから比べる。
//! 同じ物体の次の折れ線へそのまま重ねられるので、線が何本あっても1つの答えになる。
void AccumulateBoxReach(BoxReach& reach, const ScreenBox& box,
    const geometry::ScreenMapping& mapping, const std::vector<geometry::Vector3>& polyline);

//! 曲線を1本足す。弦へ落とす細かさは許容差が決める。
void AccumulateBoxCurve(BoxReach& reach, const ScreenBox& box,
    const geometry::ScreenMapping& mapping, const geometry::CurveSegment& segment,
    const geometry::GeometryTolerance& tolerance);

//! 点を1つ足す。作図点に使う。
void AccumulateBoxPoint(BoxReach& reach, const ScreenBox& box,
    const geometry::ScreenMapping& mapping, const geometry::Vector3& point);

//! 矩形に入るものを **物体単位** で集める。
//!
//! 物体単位にするのは、矩形で選ぶのが「対象」だからである(§4.2)。
//! 線分単位にすると、折れ線の一部だけが選ばれて、次の操作の相手が読めなくなる。
//!
//! 完全包含は、その物体の線と点が **すべて** 矩形の中にあるときだけ返す。
//! 画面へ写せない点(カメラの後ろ)を持つ物体は「完全に入った」とは言えないので返さない。
//! 交差は、1か所でも触れていれば返す。並びは場面の並び。
[[nodiscard]] std::vector<PickCandidate> CollectBoxPickCandidates(
    const modeling::SnapScene& scene, const geometry::ScreenMapping& mapping,
    const BoxSelection& request, const geometry::GeometryTolerance& tolerance,
    const PickFocus& focus = {});

//! 矩形で集めた候補を選択へ入れる。
//!
//! Replace は集めたものだけにする。1件ずつ ApplySelection へ渡すと、
//! Replace が重なって最後の1件しか残らない。
//! Add / Toggle / Subtract は1件ずつ当てるが、照合は **物体単位** で行う。
//! 部分要素ごとに見ると、線分を1つ選んでいるワイヤーを Ctrl+矩形で囲ったときに
//! 物体と線分が二重に入り、選択件数が物の数と合わなくなる。
//! 同じ物体が候補に2度出ていても1度だけ当てる。
[[nodiscard]] SelectionSet ApplyBoxSelection(const SelectionSet& current,
    const std::vector<PickCandidate>& candidates, SelectionMode mode);

[[nodiscard]] bool IsSelected(const SelectionSet& selection, base::EntityId entityId);
[[nodiscard]] bool IsSelected(const SelectionSet& selection, const SelectionRef& target);

//! その線分が選択に入っているか。
//!
//! 物体ごと選んだときはその物体の全線分、線分を選んだときは **その線分だけ**。
//! EntityId だけで見ると、ワイヤーの1本を選んだだけで折れ線ぜんぶが
//! 選択色になり、いま何を相手にしているのか読めなくなる(§4.1)。
//!
//! 規則は SelectedCurves と同じ1か所から出す。別に書くと、
//! 「書き出し・測定の対象」と「画面で強調される線」が食い違う。
[[nodiscard]] bool IsCurveSelected(const SelectionSet& selection, base::EntityId entityId,
    base::SegmentId segmentId);

//! 部分要素を含む選択件数。画面の「選択中 n 件」はこちらを使う。
[[nodiscard]] std::size_t SelectionItemCount(const SelectionSet& selection);

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
