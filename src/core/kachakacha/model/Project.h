#pragma once

#include "kachakacha/model/Body.h"
#include "kachakacha/model/PartModel.h"
#include "kachakacha/model/Wire.h"
#include "kachakacha/model/WireConstraints.h"
#include "kachakacha/model/WorkPlane.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::model {

enum class WirePlanePolicy {
    Free3D,
    ReferenceOnly,
    LockedToPlane,
};

enum class WireEndpoint {
    Start,
    End,
};

struct WireEndpointReference {
    std::string wireName;
    WireEndpoint endpoint = WireEndpoint::Start;
};

struct WireCoincidentConstraint {
    WireEndpointReference anchor;
    WireEndpointReference follower;
};

enum class WireContinuity {
    G1Tangent,
    G2Curvature,
};

struct WireTangentConstraint {
    WireEndpointReference anchor;
    WireEndpointReference follower;
    WireContinuity continuity = WireContinuity::G1Tangent;
};

enum class ReferenceDimensionKind {
    PointDistance,
    WireLength,
    WireRadius,
    WireDistance,
    WireAngle,
    PointWireDistance,
    PointPlaneDistance,
    WirePlaneAngle,
    PlaneAngle,
    PlaneDistance,
};

enum class DimensionReferenceKind {
    None,
    FixedPoint,
    Wire,
    WorkPlane,
};

struct DimensionReference {
    DimensionReferenceKind kind = DimensionReferenceKind::None;
    std::string objectName;
    geometry::Vector3 point;
    double wireParameter = 0.0;
};

struct ReferenceDimension {
    std::string name;
    ReferenceDimensionKind kind = ReferenceDimensionKind::PointDistance;
    DimensionReference first;
    DimensionReference second;
    bool visible = true;
};

struct ReferenceDimensionResult {
    geometry::Vector3 firstPoint;
    geometry::Vector3 secondPoint;
    double value = 0.0;
};

struct WireMetadata {
    std::optional<std::string> sourcePlaneName;
    WirePlanePolicy planePolicy = WirePlanePolicy::Free3D;
    WireLineConstraints lineConstraints;
    WireCurveConstraints curveConstraints;
    bool construction = false;
};

struct NamedWorkPlane {
    std::string name;
    WorkPlane plane;
    bool visible = true;
};

struct NamedPoint {
    std::string name;
    geometry::Vector3 point;
    std::optional<std::string> sourcePlaneName;
    bool visible = true;
};

struct NamedWire {
    std::string name;
    Wire wire;
    WireMetadata metadata;
    struct Projection {
        std::string sourceWireName;
        std::string targetSurfaceName;
        geometry::Vector3 direction;
    };
    std::optional<Projection> projection;
    bool visible = true;
    struct PlateOffset {
        std::string sourceWireName;
        std::string plateName;
        double throughThickness = 0.5;
    };
    std::optional<PlateOffset> plateOffset;
    //! 部材近似モデルの派生境界線の場合、そのモデル名。個別編集・保存の対象外。
    std::optional<std::string> partModelSourceName;
};

struct NamedSurface {
    std::string name;
    Surface surface;
    std::vector<std::string> sourceWireNames;
    bool visible = true;
    std::vector<std::string> guideWireNames;
    // Each entry is one logical boundary, guide, or section assembled from
    // endpoint-connected source wires. sourceWireNames remains the flattened
    // dependency list used by existing project operations.
    std::vector<std::vector<std::string>> sourceWireGroups;
    //! 部材近似モデルの派生面(部材面)の場合、そのモデル名。個別編集・保存の対象外。
    std::optional<std::string> partModelSourceName;
    //! 面の開口(窓・ライト等): この面へ投影した閉ワイヤの名前。
    //! 面入力の近似モデル・型紙・実体化に反映され、この面から作る板材にも引き継がれる。
    std::vector<std::string> openingWireNames;
};

struct NamedPlate {
    std::string name;
    Plate plate;
    std::string sourceSurfaceName;
    std::string material;
    std::vector<std::string> openingWireNames;
    bool visible = true;
    std::vector<std::string> reliefCutWireNames;
    std::vector<std::string> splitWireNames;
    //! 積層(重ね板、合意9): この板が重なる下の板の名前(空=独立)。
    //! 同じ元面の板同士なら幾何も追従し、下の板の外側へ厚みぶんずらして作られる。
    //! 別の面に描いた板同士なら関係の記録のみ(幾何は変えない)。
    std::string laminateBaseName;
};

struct NamedBody {
    std::string name;
    Body body;
    std::string sourceSurfaceName;
    bool visible = true;
};

//! 部材近似モデル(ADR 0019)。板材を1軸曲げ部材の集合へ近似する派生レシピ。
struct NamedPartModel {
    std::string name;
    //! 近似元。sourcePlateName(板材)か sourceSurfaceName(面)のどちらか一方だけが
    //! 非空になる(面入力では厚み未定のまま近似し、板材化の時点で厚みを指定する)。
    //! 注意: 集成体初期化互換のため、新フィールドは末尾へ追加する。
    std::string sourcePlateName;
    PartApproximationOptions options;
    PartApproximationResult result;
    std::vector<std::string> boundaryWireNames; //!< レール(縁+内部境界)の派生ワイヤ名(部材数+1本)
    std::vector<std::string> partSurfaceNames;  //!< 部材ごとの派生ルールド面名(部材数本)
    //! 元板材の開口(窓・ライト等)を部材面へ投影した派生ワイヤ名。
    //! 命名は「<モデル名>_部材<番号>_穴<開口番号>」。部材境界をまたぐ開口は作らない。
    std::vector<std::string> openingWireNames;
    bool visible = true;
    std::string sourceSurfaceName; //!< 面入力のときの元面名(板材入力なら空)
    //! 可動折り線(合意10): 各内部折り線の進行度(1=完成形の折り角、0=平ら)。
    //! サイズは 部材数-1。空なら全て1(完成形)。部材数が変わる再生成でリセットされる。
    std::vector<double> railFoldProgress;
    //! 接続スコープ(合意13): 近似実行時に一緒に選ばれていた近似元以外のワイヤ・面。
    //! 再生成のたび、近似の実形状(角ばったメッシュ)に載る部分をスナップした
    //! 派生コピー「<元名>_接続」を作り直す(元は変更しない)。
    std::vector<std::string> scopeWireNames;
    std::vector<std::string> scopeSurfaceNames;
    std::vector<std::string> adaptedWireNames;    //!< 派生「_接続」ワイヤ(自動)
    std::vector<std::string> adaptedSurfaceNames; //!< 派生「_接続」面(自動)
    //! 部材面への後付け開口(#17b): 部材番号(1始まり)と投影元ワイヤ・方向。
    //! 再生成のたび、その部材面へ投影し直した派生穴が作られる(型紙・実体化にも反映)。
    struct PartOpening {
        int partNumber = 1;
        std::string sourceWireName;
        geometry::Vector3 direction;
    };
    std::vector<PartOpening> partOpenings;
};

//! セットの表示状態。ReferenceOnly はスナップ・測定可、選択・編集不可(UI側で解釈)。
enum class ObjectSetState {
    Visible,
    ReferenceOnly,
    Hidden,
};

enum class ProjectObjectKind {
    WorkPlane,
    Point,
    Wire,
    Surface,
    Plate,
    Body,
    PartModel,
};

struct ObjectSetMember {
    ProjectObjectKind kind = ProjectObjectKind::Wire;
    std::string name;
};

//! セット(グループ)。派生物の整理と一括表示制御に使う(docs/surface-unfolding-spec.md)。
struct ObjectSet {
    std::string name;
    ObjectSetState state = ObjectSetState::Visible;
    bool automatic = false; //!< 部材近似モデル等が生成・管理する自動セット
    //! 「出力対象のみで.kcd書き出し」で書き出すか。通常の保存には影響しない。
    bool exportEnabled = true;
    std::vector<ObjectSetMember> members;
    //! 親グループ名(空=最上位)。エクスプローラ風の入れ子(ADR 0024)。
    //! 集成体初期化の互換のため末尾に置く。
    std::string parentName;
};

class Project {
public:
    void AddWorkPlane(std::string name, WorkPlane plane);
    void AddPoint(
        std::string name,
        geometry::Vector3 point,
        std::optional<std::string> sourcePlaneName = std::nullopt);
    void AddWire(std::string name, Wire wire, WireMetadata metadata = {});
    void AddPlanarSurface(std::string name, std::string boundaryWireName);
    void AddPlanarSurface(std::string name, std::vector<std::string> boundaryWireNames);
    void AddRuledSurface(std::string name, std::string firstSectionName, std::string secondSectionName);
    void AddRuledSurface(
        std::string name,
        std::vector<std::string> firstSectionNames,
        std::vector<std::string> secondSectionNames);
    void AddLoftSurface(std::string name, std::vector<std::string> sectionNames);
    void AddGordonSurface(
        std::string name,
        std::vector<std::string> sectionNames,
        std::vector<std::string> guideNames);
    void AddLoftSurface(
        std::string name,
        std::vector<std::vector<std::string>> sectionWireGroups);
    void AddGuidedLoftSurface(
        std::string name,
        std::string firstGuideName,
        std::string secondGuideName,
        std::vector<std::string> sectionNames);
    void AddGuidedLoftSurface(
        std::string name,
        std::vector<std::string> firstGuideNames,
        std::vector<std::string> secondGuideNames,
        std::vector<std::vector<std::string>> sectionWireGroups);
    void AddPlate(
        std::string name,
        std::string sourceSurfaceName,
        double thickness,
        PlateThicknessDirection direction,
        std::string material);
    void AddPlate(
        std::string name,
        std::string sourceSurfaceName,
        double startThickness,
        double endThickness,
        PlateThicknessDirection direction,
        std::string material);
    void AddSurfaceJig(
        std::string name,
        std::string sourceSurfaceName,
        PlateSurfaceRange range,
        JigSide side,
        double clearanceMillimeters,
        double thicknessMillimeters);
    void AddProjectedWire(
        std::string name,
        std::string sourceWireName,
        std::string targetSurfaceName,
        geometry::Vector3 direction);
    void AddPlateOffsetWire(
        std::string name,
        std::string sourceWireName,
        std::string plateName,
        double throughThickness);
    void UpdateWorkPlane(std::string_view name, WorkPlane plane);
    void UpdateWire(std::string_view name, Wire wire);
    void UpdateWireAndMetadata(std::string_view name, Wire wire, WireMetadata metadata);
    void UpdatePlate(
        std::string_view name,
        std::string sourceSurfaceName,
        double thickness,
        PlateThicknessDirection direction,
        std::string material);
    void UpdatePlate(
        std::string_view name,
        std::string sourceSurfaceName,
        double startThickness,
        double endThickness,
        PlateThicknessDirection direction,
        std::string material);
    void UpdateSurfaceJig(
        std::string_view name,
        std::string sourceSurfaceName,
        PlateSurfaceRange range,
        JigSide side,
        double clearanceMillimeters,
        double thicknessMillimeters);
    void SetWireMetadata(std::string_view name, WireMetadata metadata);
    void AddWireCoincidentConstraint(
        WireEndpointReference anchor,
        WireEndpointReference follower);
    [[nodiscard]] std::size_t RemoveWireCoincidentConstraints(std::string_view wireName);
    void AddWireTangentConstraint(
        WireEndpointReference anchor,
        WireEndpointReference follower,
        WireContinuity continuity = WireContinuity::G1Tangent);
    [[nodiscard]] std::size_t RemoveWireTangentConstraints(std::string_view wireName);
    void AddReferenceDimension(ReferenceDimension dimension);
    [[nodiscard]] bool RemoveReferenceDimension(std::string_view name);
    void SetReferenceDimensionVisible(std::string_view name, bool visible);
    [[nodiscard]] ReferenceDimensionResult EvaluateReferenceDimension(
        std::string_view name) const;
    void SetWorkPlaneVisible(std::string_view name, bool visible);
    void SetPointVisible(std::string_view name, bool visible);
    void SetWireVisible(std::string_view name, bool visible);
    void SetSurfaceVisible(std::string_view name, bool visible);
    void SetPlateVisible(std::string_view name, bool visible);
    void SetBodyVisible(std::string_view name, bool visible);
    void SetPlateRange(std::string_view name, PlateSurfaceRange range);
    void SplitPlate(
        std::string_view name,
        PlateSplitAxis axis,
        double parameter,
        std::string firstName,
        std::string secondName);
    void AddSurfaceOpening(std::string_view surfaceName, std::string wireName);
    void RemoveSurfaceOpening(std::string_view surfaceName, std::string_view wireName);
    void AddPlateOpening(std::string_view plateName, std::string wireName);
    void RemovePlateOpening(std::string_view plateName, std::string_view wireName);
    void AddPlateReliefCut(std::string_view plateName, std::string wireName);
    void RemovePlateReliefCut(std::string_view plateName, std::string_view wireName);
    void AddPlateSplitLine(std::string_view plateName, std::string wireName);
    void RemovePlateSplitLine(std::string_view plateName, std::string_view wireName);
    //! 積層関係の設定/解除(basePlateName が空なら解除)。同じ元面なら幾何も追従する。
    void SetPlateLaminate(std::string_view name, std::string_view basePlateName);
    //! base 板の外側に積層板を1枚追加する(同じ元面・同じ方向・同じ範囲)。
    void AddLaminatedPlate(
        std::string name,
        std::string_view basePlateName,
        double thickness,
        std::string material);
    void AddPartModel(
        std::string name,
        std::string sourcePlateName,
        PartApproximationOptions options);
    //! 面を直接近似する部材近似モデル(厚みは後段の板材化で指定)。
    void AddPartModelFromSurface(
        std::string name,
        std::string sourceSurfaceName,
        PartApproximationOptions options);
    void UpdatePartModelOptions(std::string_view name, PartApproximationOptions options);
    //! 可動折り線の進行度を設定する(サイズは部材数-1、または空=全て1)。
    void SetPartModelRailFoldProgress(std::string_view name, std::vector<double> progress);
    //! 接続スコープを設定し、派生「_接続」オブジェクトを作り直す(空で解除)。
    void SetPartModelConnectionScope(
        std::string_view name,
        std::vector<std::string> wireNames,
        std::vector<std::string> surfaceNames);
    //! 部材面へ下書きワイヤを投影して穴を後付けする(#17b)。閉じたワイヤのみ。
    //! 部材番号付きで記録され、再生成のたび再投影されて追従する。
    void AddPartModelOpening(
        std::string_view name,
        int partNumber,
        std::string sourceWireName,
        geometry::Vector3 direction);
    //! 後付け開口を外す(投影元ワイヤ名で指定)。
    void RemovePartModelOpening(std::string_view name, std::string_view sourceWireName);
    bool RemovePartModel(std::string_view name);
    void SetPartModelVisible(std::string_view name, bool visible);
    //! 部材境界を独立した通常ワイヤとして複製する(抽出)。作成したワイヤ名を返す。
    [[nodiscard]] std::vector<std::string> ExtractPartModelBoundaries(std::string_view name);

    void CreateObjectSet(std::string name, ObjectSetState state = ObjectSetState::Visible);
    bool RemoveObjectSet(std::string_view name);
    void SetObjectSetState(std::string_view name, ObjectSetState state);
    void SetObjectSetExport(std::string_view name, bool enabled);
    //! 親グループを設定する(空文字で最上位へ)。自分自身・子孫は循環として拒否。
    void SetObjectSetParent(std::string_view child, std::string_view parent);
    void AssignObjectToSet(ProjectObjectKind kind, std::string objectName, std::string_view setName);
    void RemoveObjectFromSets(ProjectObjectKind kind, std::string_view objectName);
    //! オブジェクトが属するセットの状態。どのセットにも属さなければ Visible。
    [[nodiscard]] ObjectSetState ObjectStateInSets(
        ProjectObjectKind kind, std::string_view objectName) const;

    bool RemoveWorkPlane(std::string_view name);
    bool RemovePoint(std::string_view name);
    bool RemoveWire(std::string_view name);
    bool RemoveSurface(std::string_view name);
    bool RemovePlate(std::string_view name);
    bool RemoveBody(std::string_view name);

    //! 名前を変更し、参照する全フィールド(投影元・先、面の元ワイヤ、板の元面、
    //! 開口、近似モデルの元・スコープ、積層、拘束、寸法、セット所属)を一括更新する。
    //! 近似モデルの派生物(partModelSourceName 付き)は単独リネーム不可。
    //! 近似モデル自体のリネームは派生物(<名前>_境界N 等)と自動セットも連動して付け替える。
    void RenameObject(ProjectObjectKind kind, std::string_view oldName, std::string newName);
    //! セット(グループ)名の変更。自動セットは不可。子の親参照も更新する。
    void RenameObjectSet(std::string_view oldName, std::string newName);

    [[nodiscard]] const std::vector<NamedWorkPlane>& WorkPlanes() const noexcept { return workPlanes_; }
    [[nodiscard]] const std::vector<NamedPoint>& Points() const noexcept { return points_; }
    [[nodiscard]] const std::vector<NamedWire>& Wires() const noexcept { return wires_; }
    [[nodiscard]] const std::vector<NamedSurface>& Surfaces() const noexcept { return surfaces_; }
    [[nodiscard]] const std::vector<NamedPlate>& Plates() const noexcept { return plates_; }
    [[nodiscard]] const std::vector<NamedBody>& Bodies() const noexcept { return bodies_; }
    [[nodiscard]] const std::vector<NamedPartModel>& PartModels() const noexcept { return partModels_; }
    [[nodiscard]] const std::vector<ObjectSet>& ObjectSets() const noexcept { return objectSets_; }
    [[nodiscard]] const std::vector<WireCoincidentConstraint>& CoincidentConstraints() const noexcept
    {
        return coincidentConstraints_;
    }
    [[nodiscard]] const std::vector<WireTangentConstraint>& TangentConstraints() const noexcept
    {
        return tangentConstraints_;
    }
    [[nodiscard]] const std::vector<ReferenceDimension>& ReferenceDimensions() const noexcept
    {
        return referenceDimensions_;
    }

    [[nodiscard]] std::optional<WorkPlane> FindWorkPlane(std::string_view name) const;
    [[nodiscard]] std::optional<Surface> FindSurface(std::string_view name) const;
    [[nodiscard]] std::optional<Plate> FindPlate(std::string_view name) const;
    [[nodiscard]] std::optional<Body> FindBody(std::string_view name) const;

private:
    [[nodiscard]] const NamedWire& RequireWire(std::string_view name) const;
    [[nodiscard]] Wire BuildSurfaceWireGroup(
        const std::vector<std::string>& wireNames,
        bool rejectProjected,
        bool rejectPlateOffset,
        std::string_view role) const;
    void ApplyCoincidentConstraints();
    void ApplyTangentConstraints();
    void ApplyWireConstraints();
    void RebuildDependentGeometry();
    void RecomputeLaminateOffsets();
    void RebuildPartModels();
    void RegeneratePartModelDerivedObjects(NamedPartModel& model);
    //! 近似元(面 or 板材の厚み中央面)のサンプラを返す。見つからなければ投げる。
    [[nodiscard]] PartSource RequirePartModelSource(const NamedPartModel& model) const;
    [[nodiscard]] ObjectSet* FindObjectSetMutable(std::string_view name);
    //! 参照フィールドの付け替え(kind の名前 old → new)。ガード無しの内部処理。
    void RenameReferences(ProjectObjectKind kind, std::string_view oldName, const std::string& newName);

    std::vector<NamedWorkPlane> workPlanes_;
    std::vector<NamedPoint> points_;
    std::vector<NamedWire> wires_;
    std::vector<NamedSurface> surfaces_;
    std::vector<NamedPlate> plates_;
    std::vector<NamedBody> bodies_;
    std::vector<NamedPartModel> partModels_;
    std::vector<ObjectSet> objectSets_;
    std::vector<WireCoincidentConstraint> coincidentConstraints_;
    std::vector<WireTangentConstraint> tangentConstraints_;
    std::vector<ReferenceDimension> referenceDimensions_;
};

} // namespace kachakacha::model
