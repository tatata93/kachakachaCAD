#pragma once

//! Feature DAG。文書の正本はこちらで、Entityは識別と表示だけを持つ。
//! methodごとにFeature typeを増やさない(architecture-and-data.md §7)。

#include "kachakacha/base/Ids.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Expression.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kachakacha::v2::domain {

enum class FeatureType {
    CreatePoint,
    CreateWorkPlane,
    CreateWire,
    TransformWire,
    ProjectWire,
    CreateGuideSurface,
    Extrude,
    CreatePartFromWireCage,
    Boolean,
    CreateFabricationModel,
    CreatePattern,
    FreezeDerived,
    //! 面に厚みを付けて立体にする。押し出しとは入力が違う(面であって輪郭ではない)。
    ThickenSurface,
};

[[nodiscard]] constexpr std::string_view FeatureTypeName(FeatureType type) noexcept
{
    switch (type) {
    case FeatureType::CreatePoint:            return "CreatePoint";
    case FeatureType::CreateWorkPlane:        return "CreateWorkPlane";
    case FeatureType::CreateWire:             return "CreateWire";
    case FeatureType::TransformWire:          return "TransformWire";
    case FeatureType::ProjectWire:            return "ProjectWire";
    case FeatureType::CreateGuideSurface:     return "CreateGuideSurface";
    case FeatureType::Extrude:                return "Extrude";
    case FeatureType::CreatePartFromWireCage: return "CreatePartFromWireCage";
    case FeatureType::Boolean:                return "Boolean";
    case FeatureType::CreateFabricationModel: return "CreateFabricationModel";
    case FeatureType::CreatePattern:          return "CreatePattern";
    case FeatureType::FreezeDerived:          return "FreezeDerived";
    case FeatureType::ThickenSurface:         return "ThickenSurface";
    }
    return "Unknown";
}

//! ワイヤー編集の方法。V1の編集アクションをここへ集める。
enum class WireTransformMethod {
    Move,
    Copy,
    Rotate,
    Mirror,
    Trim,
    Extend,
    Split,
    Join,
    Fillet,
    Chamfer,
    Offset,
    MeetLines,
    Coincident,
    Tangent,
    Curvature,
    //! ポリラインの角を全部落とす / 丸める(V1 の「角の加工」)。線1本の中で完結する。
    CornerChamfer,
    CornerFillet,
};

enum class BooleanOperation {
    New,
    Add,
    Cut,
};

// ---- 参照(architecture-and-data.md §6) ----

struct EntityRef {
    EntityId entityId;
};

struct SegmentRef {
    EntityId entityId;
    SegmentId segmentId;
    double startParameter = 0.0;
    double endParameter = 1.0;
};

struct SubshapeRef {
    EntityId partId;
    //! Feature由来の意味的キー。OCCTの一時Face番号を保存してはならない。
    std::string subshapeKey;
};

struct WireChainRef {
    std::vector<SegmentRef> segments;
    std::vector<bool> reversed;
};

// ---- Feature定義 ----

struct CreatePointDefinition {
    geometry::Vector3 positionMm;
    std::optional<EntityId> sourcePlaneId;
    geometry::EvaluatedValue xExpression;
    geometry::EvaluatedValue yExpression;
    geometry::EvaluatedValue zExpression;
};

struct CreateWireDefinition {
    std::vector<geometry::CurveSegment> segments;
    std::vector<SegmentId> segmentIds;
    std::optional<EntityId> sourcePlaneId;
    bool construction = false;
};

struct TransformWireDefinition {
    WireTransformMethod method = WireTransformMethod::Move;
    std::vector<SegmentRef> inputs;
    geometry::Vector3 vectorArgument;   //!< 移動量、軸方向、面法線など
    geometry::Vector3 pointArgument;    //!< 軸上の点、面上の点など
    geometry::EvaluatedValue scalarArgument; //!< 距離、半径、角度など
    //! C面取りの B 側の切戻し(mm)。0 なら scalarArgument と同じ(対称)。V1 の「B の切戻し」。
    double secondScalarMm = 0.0;
    //! 残す側(V1 の「A の残す側」「B の残す側」)。0 自動(角から遠い端)/ 1 始点側 / 2 終点側。
    int firstKeepSide = 0;
    int secondKeepSide = 0;
    //! 角の加工で 1 つの角だけにするときの頂点番号(0 始まり、点の番号)。-1 なら全部。
    int cornerIndex = -1;
};

struct FreezeDerivedDefinition {
    std::vector<EntityId> sources;
};

//! 作業平面。作り方と、それに要る入力を持つ。
//! 出来上がりの枠(原点と3軸)は再計算で出せるので、ここには持たない。
//! 持つと、入力を変えたのに枠が古いまま、という食い違いが起きる。
struct CreateWorkPlaneDefinition {
    //! modeling::WorkPlaneMethod と同じ並び。core への依存を増やさないため数で持つ。
    int method = 0;
    //! 原点の基準平面(top_XY / front_XZ / side_YZ)。消せず、名前も変えられない。
    //! V1 の一覧の「原点」ノードに当たる。
    bool isOriginPlane = false;
    std::vector<EntityId> inputs;
    geometry::Vector3 origin{};
    geometry::Vector3 normal{0.0, 0.0, 1.0};
    geometry::Vector3 uDirection{1.0, 0.0, 0.0};
    geometry::EvaluatedValue offset;   //!< 平行移動の距離など
};

//! 線を面へ落とす。
struct ProjectWireDefinition {
    std::vector<EntityId> inputs;
    EntityId targetPlaneId;
    geometry::Vector3 direction{0.0, 0.0, -1.0};
};

//! 形状ガイドの面。役割ごとの線の並びを持つ。
struct CreateGuideSurfaceDefinition {
    //! modeling::GuideSurfaceMethod と同じ並び。
    int method = 0;
    //! 役割ごとの鎖。並びは modeling::ChainRole の順。
    std::vector<WireChainRef> chains;
    //! 各鎖の役割。chains と同じ長さ。
    std::vector<int> roles;
    //! OffsetGuide の離す距離(mm)。他の作り方では 0。
    double offsetDistanceMm = 0.0;
    //! Revolve(回転体)の軸と角度。他の作り方では使わない。
    geometry::Vector3 revolveAxisPoint{};
    geometry::Vector3 revolveAxisDirection{0.0, 0.0, 1.0};
    double revolveAngleRad = 0.0;
    //! 断面を chains の順のまま使う(手動固定)。古い文書には無い鍵なので、読むときは偽。
    bool lockSectionOrder = false;
    //! 四辺面の張り方(modeling::FourEdgeStyle の番号)。古い文書には無いので 0(標準)。
    int fourEdgeStyle = 0;
    //! 鎖ごとの連続条件(modeling::SurfaceContinuity の番号)と支持面。chains と同じ長さか空。
    //! 古い文書には無い鍵なので、空で読む(全部 G0、支持面なし = これまでと同じ意味)。
    std::vector<int> continuity;
    std::vector<EntityId> supportSurfaces;
};

//! 押し出し。
struct ExtrudeDefinition {
    std::vector<EntityId> profiles;
    geometry::Vector3 direction{0.0, 0.0, 1.0};
    geometry::EvaluatedValue distance;
    //! modeling::ExtrudeExtentMode / ExtrudeBooleanMode と同じ並び。
    int extentMode = 0;
    int booleanMode = 0;
    std::vector<EntityId> targets;
};

//! 閉じたかごから部品を作る。
struct CreatePartFromWireCageDefinition {
    std::vector<EntityId> wires;
    geometry::EvaluatedValue thickness;
    //! 板厚をどちらへ付けるか。0=外側 1=中央 2=内側。
    int placement = 1;
};

//! 足す・引く。
struct BooleanDefinition {
    //! 0=足す 1=引く。
    int mode = 0;
    std::vector<EntityId> targets;
    std::vector<EntityId> tools;
};

//! 面に厚みを付けて立体にする(工程2の「面をソリッド化する」)。
struct ThickenSurfaceDefinition {
    EntityId surface;
    geometry::EvaluatedValue thickness;
    //! fabrication::ThicknessPlacement と同じ並び。0=外側 1=中央 2=内側。
    int placement = 1;
    //! 「任意の面まで」のときの相手の作業平面。あれば thickness と placement は使わない。
    std::optional<EntityId> targetPlane;
};

//! 製作モデル(近似モデル)。
//!
//! 近似の結果そのものは持たない。持つのは **作り方と曲げ状態** で、
//! 開いたときに作り方から作り直す(立体と同じ考え)。
//! 曲げ状態を文書に持つのは、任意の曲げ具合でワイヤ・面・展開図を出し、
//! それを保存して開き直しても同じ状態から続けられるようにするため。
//! V1 は part_model_fold / part_model_assembly / part_model_part_assembly で
//! 同じことを保存していた。
struct CreateFabricationModelDefinition {
    //! 元になるもの。部品か形状ガイド。
    std::vector<EntityId> parts;
    geometry::EvaluatedValue materialThickness;
    geometry::EvaluatedValue targetMaxDeviation;
    int fidelity = 6;

    //! 近似の方式。0 = V2 方式(面を分類して展開できなければ断る)、
    //! 1 = V1 方式(帯へ近似し直す。二重曲面も切る)。
    int method = 0;
    //! 帯近似の決め方(method = 1 のとき)。fabrication::BandApproximationOptions と同じ。
    int splitAxis = 2; //!< 0 = U、1 = V、2 = 自動(曲がっている方向を横切る)
    bool automaticBoundaries = true;
    int maximumPartCount = 12;
    double minimumPartWidthMm = 4.0;
    std::vector<double> manualBoundaries;

    //! 開口(窓など)にする線と、折り線にする線。どの部材のものかは、
    //! 外周と同じ平面に載っているかで決まる(人に選ばせない)。
    std::vector<EntityId> openingWires;
    std::vector<EntityId> foldWires;
    //! 切れ目にする線(V1 の plate_relief_cut)。開いた線。切るが、部材は分かれない。
    std::vector<EntityId> reliefCutWires;
    //! 接続スコープ。近似の実形状へ寄せた「_接続」の線を作る元(V1 の合意13)。
    //! 元の線は変えない。近似したことで隣の部品と合わなくなるのを、派生の線で埋める。
    std::vector<EntityId> connectionWires;

    //! 面の範囲(V1 の板材の「範囲」)。u は列方向、v は行方向の 0〜1。全体なら 0〜1。
    double rangeUMin = 0.0;
    double rangeUMax = 1.0;
    double rangeVMin = 0.0;
    double rangeVMax = 1.0;

    //! 切れ目の上限(fabrication-contract §7.5)。
    //!
    //! 紙とプラ板と真鍮で、残してよい幅は違う。既定は 0.3mm 厚のプラ板の値で、
    //! 薄い紙ならもっと狭くてよいし、真鍮なら足りない。
    //! 深さは部材の幅に対する比、残りは先端から向こう側の縁までの最小の幅。
    //! 立体を面ごとに分けるか(EX-02 のあと、2026-09-14)。
    //!
    //! 偽なら、いままでどおり立体の「平らな1枚」だけを部材にする。
    //! 真なら、立体の面を1枚ずつ部材にする。箱を6枚の型紙にできる。
    //! 古い文書は偽で読む。開いたときに部材の数が変わらないようにするためである。
    bool splitSolidFaces = false;
    double maximumReliefDepthRatio = 0.55;
    double minimumReliefLigamentMm = 0.5;

    //! 曲げ状態。0 = 平ら(型紙)、100 = 近似完成形。
    double masterPercent = 100.0;
    //! 折り線ごとの進行度(0..1)。空なら全部 master に従う。
    std::vector<double> creaseProgress;
    //! 帯ごとの進行度(0..1)。空なら全部 master に従う。「選んだ部材だけが曲がる」。
    std::vector<double> bandProgress;

    //! 部材(帯)ごとに固定した曲げ半径(mm)と、その別(0 = 自動、1 = 固定)。
    //!
    //! 自動なら、いま出来ている形から測った半径を使う。近似をやり直すと更新される。
    //! 固定なら、人が入れた半径を使い、**近似をやり直しても戻さない**(§31)。
    //! 模型工作では、手元にある丸棒や治具の径へ合わせたいことがあるからである。
    //!
    //! ここに置くのは、これが「作り方」だからである。画面が覚えていると、
    //! 保存で消え、取り消しで戻らず、開き直すと別の形になる。
    //! 数が帯の数と合わなければ、合わない分は自動として扱う。
    //! 帯の数は近似をやり直すと変わるので、古い値を理由に開けなくしない。
    std::vector<double> bendRadiusMm;
    std::vector<int> bendRadiusLock;

    //! 展開の基準にする辺(§33)。帯の境目(レール)の番号。0 が先頭。
    //!
    //! 展開すると、既定では先頭の辺が動かない。しかし人が作るときは
    //! 「この辺は動かしたくない」がある。床板の縁を基準にすれば、
    //! 展開しても床板がその場に残り、まわりの板だけが開く。
    //! 範囲の外なら既定(0)に戻す。基準が消えたことを理由に開けなくしない。
    int unfoldBaseRail = 0;
};

//! 型紙。
struct CreatePatternDefinition {
    std::vector<EntityId> fabricationModels;
    geometry::EvaluatedValue pageWidth;
    geometry::EvaluatedValue pageHeight;
    geometry::EvaluatedValue marginMm;
};

//! 種類ごとの定義。まだ実装していないFeatureは空の定義を持つ。
using FeatureDefinition = std::variant<std::monostate, CreatePointDefinition,
    CreateWireDefinition, TransformWireDefinition, FreezeDerivedDefinition,
    CreateWorkPlaneDefinition, ProjectWireDefinition, CreateGuideSurfaceDefinition,
    ExtrudeDefinition, CreatePartFromWireCageDefinition, BooleanDefinition,
    CreateFabricationModelDefinition, CreatePatternDefinition, ThickenSurfaceDefinition>;

struct FeatureOutput {
    std::string key;   //!< 再計算で同じ出力を指し続けるための安定キー
    EntityId entityId;
    EntityKind kind = EntityKind::Point;
};

struct Feature {
    FeatureId id;
    FeatureType type = FeatureType::CreatePoint;
    std::string displayName;
    bool enabled = true;
    FeatureDefinition definition;
    std::vector<FeatureOutput> outputs;
    std::uint64_t revision = 0;

    //! このFeatureが入力として参照しているEntity。DAGの辺はここから作る。
    std::vector<EntityId> inputEntityIds;

    //! 派生物を置くグループ(architecture-and-data.md §11)。
    //! 部品・形状ガイド・近似ワイヤー・型紙は派生物であり、
    //! 作業中グループではなく、ここが指すグループへ入る。
    //! 指定が無ければ作業中グループへ入る(利用者が自分で作ったもの)。
    std::optional<GroupId> derivedGroupId;
};

} // namespace kachakacha::v2::domain
