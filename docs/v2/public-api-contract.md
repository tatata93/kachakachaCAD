# Wire-first V2 公開API契約

## 1. 目的

この文書は、WP間で共有するC++ APIの形を固定する。実装ファイル内のprivate classや関数名は変更できるが、
ここにある型の責務、所有関係、戻り値、依存方向は変更してはならない。

## 2. 名前空間

```cpp
namespace kachakacha::base {}
namespace kachakacha::geometry {}
namespace kachakacha::domain {}
namespace kachakacha::document {}
namespace kachakacha::modeling {}
namespace kachakacha::fabrication {}
namespace kachakacha::io {}
namespace kachakacha::kernel {}
namespace kachakacha::app {}
```

`v2` を名前空間へ恒久的に含めない。移行中の衝突回避に使った場合、WP-13で除去する。

## 3. ResultとDiagnostic

例外はプログラミングエラーにだけ使い、利用者入力やkernel失敗はResultで返す。

```cpp
namespace kachakacha::base {

enum class Severity {
    Info,
    Warning,
    Error,
};

struct RecoveryAction {
    std::string actionId;
    std::string labelJa;
    std::vector<EntityId> targets;
};

struct Diagnostic {
    std::string code;
    Severity severity;
    std::string summaryJa;
    std::string detailsJa;
    std::vector<EntityId> entityIds;
    std::vector<FeatureId> featureIds;
    std::vector<std::string> subshapeKeys;
    std::vector<RecoveryAction> recoveryActions;
};

template<class T>
class Result {
public:
    static Result Success(T value, std::vector<Diagnostic> warnings = {});
    static Result Failure(std::vector<Diagnostic> errors);

    bool HasValue() const noexcept;
    const T& Value() const;
    const std::vector<Diagnostic>& Diagnostics() const noexcept;
};

}
```

- Errorが1件以上ならValueを持たない。
- WarningだけならValueを持てる。
- Failureへ部分値を入れない。
- `void` 用に `Result<Unit>` を使う。
- UIはcodeを分岐に使い、日本語本文を解析しない。

## 4. 強いID

```cpp
template<class Tag>
class StrongUuid {
public:
    static Result<StrongUuid> Parse(std::string_view canonical);
    static StrongUuid New(IdGenerator&);
    std::string ToString() const;
    auto operator<=>(const StrongUuid&) const = default;
};

using DocumentId = StrongUuid<struct DocumentTag>;
using EntityId   = StrongUuid<struct EntityTag>;
using FeatureId  = StrongUuid<struct FeatureTag>;
using GroupId    = StrongUuid<struct GroupTag>;
using SegmentId  = StrongUuid<struct SegmentTag>;
using PanelId    = StrongUuid<struct PanelTag>;
using FoldId     = StrongUuid<struct FoldTag>;
using CutId      = StrongUuid<struct CutTag>;
using AssetId    = StrongUuid<struct AssetTag>;
```

異なるID型の比較、代入、暗黙string変換はcompile errorにする。

## 5. Geometry値型

```cpp
struct Point3 {
    double xMm;
    double yMm;
    double zMm;
};

struct Vector3 {
    double x;
    double y;
    double z;
};

struct Vector2 {
    double x;
    double y;
};

struct UnitVector3 {
    Vector3 value;
};

struct ParameterRange {
    double first;
    double last;
};

struct LineData {
    Point3 start;
    Point3 end;
};

struct CircularArcData {
    Point3 center;
    UnitVector3 normal;
    UnitVector3 xDirection;
    double radiusMm;
    double startAngleRad;
    double sweepAngleRad;
};

struct CircleData {
    Point3 center;
    UnitVector3 normal;
    UnitVector3 xDirection;
    double radiusMm;
};

struct CubicBezierData {
    std::array<Point3, 4> controlPoints;
};

struct BSplineData {
    int degree;
    std::vector<Point3> controlPoints;
    std::vector<double> knots;
    std::vector<int> multiplicities;
    std::optional<std::vector<double>> weights;
    bool periodic;
};

using CurveData = std::variant<
    LineData,
    CircularArcData,
    CircleData,
    CubicBezierData,
    BSplineData>;

struct CurveSegment {
    SegmentId id;
    CurveData curve;
    std::vector<SegmentId> provenance;
};

struct Wire3d {
    std::vector<CurveSegment> segments;
    bool closed;
};
```

- Geometry値型はQt/OCCT型を含まない。
- constructor/factoryでfinite、半径、degree、knot、連続端点を検証する。
- `closed` は読込時に再検証し、不一致なら拒否する。
- Segment provenanceはtrim/split/join後の参照修復候補表示に使う。自動修復には使わない。

## 6. 参照

```cpp
struct EntityRef {
    EntityId entityId;
};

struct SegmentRef {
    EntityId wireEntityId;
    SegmentId segmentId;
    geometry::ParameterRange range;
    bool reversed;
};

struct WireChainRef {
    std::vector<SegmentRef> segments;
    bool expectedClosed;
};

struct SubshapeKey {
    std::string value;
};

struct SubshapeRef {
    EntityId partEntityId;
    SubshapeKey key;
};
```

- WireChainRefはFeature作成確定前に順序と方向を固定する。
- `range.first` と `range.last` は0から1で、reversedとは別に保持する。
- 空chain、重複range、範囲外parameterを拒否する。
- SubshapeKeyはgrammar `^[a-z0-9][a-z0-9/_-]*$` に従う。

## 7. EntityRecord

`EntityRecord`、参照、Feature定義、製作設定の純データは `kachakacha::domain` に置く。

```cpp
enum class EntityKind {
    Point,
    WorkPlane,
    Wire,
    GuideSurface,
    Part,
    FabricationModel,
    Pattern,
};

enum class Visibility {
    Visible,
    Reference,
    Hidden,
};

enum class EditPolicy {
    Source,
    Derived,
    Frozen,
};

enum class PartPurpose {
    FinishedModel,
    FabricationPart,
    Fixture,
    Imported,
};

struct ColorRgba8 {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
    std::uint8_t alpha;
};

struct ManufacturingProperties {
    std::optional<std::string> materialName;
    std::optional<ColorRgba8> displayColor;
    std::optional<std::string> processName;
    std::optional<double> nominalThicknessMm;
    std::optional<double> referenceScaleDenominator;
    std::string notes;
};

struct PartProperties {
    PartPurpose purpose;
    std::optional<ManufacturingProperties> manufacturing;
};

struct EntityRecord {
    EntityId id;
    EntityKind kind;
    std::string displayName;
    std::optional<GroupId> groupId;
    Visibility visibility;
    EditPolicy editPolicy;
    FeatureId createdBy;
    std::uint64_t revision;
    std::optional<PartProperties> partProperties;
};
```

重要:

- EntityRecordはidentity、表示metadata、Feature outputとの対応だけを持つ。
- `partProperties` はPartでは必須、他kindではnullを必須とする。これは幾何定義ではなく表示・製作metadataである。
- `nominalThicknessMm` と `referenceScaleDenominator` はfiniteかつ正値だけを許し、未指定はnullとする。
- 幾何定義をEntityRecordとFeatureDefinitionの両方へ重複保存しない。
- すべての幾何Entityは必ず1つのFeature outputであり、`createdBy` は必須。
- SourceはCreate系Featureの設定を編集できる。
- Derivedは上流Featureを編集する。
- FrozenはFreezeFeatureが保存した独立snapshotを編集元にする。

## 8. FeatureDefinition

文字列type+自由形式mapではなく、compile-timeのvariantを正本にする。
この節で使う `FabricationSettings`、`ManualRoleAssignment`、`AssemblyState`、`PatternSettings`、
`PatternPlacement`、`FrozenPayload` は14節の `domain` 型である。実際のheaderではそれらの完全定義を
`FeatureDefinition` より前にincludeし、同名のfabrication層型を新設してはならない。

```cpp
struct CreatePointDef {
    geometry::Point3 position;
    std::optional<EntityId> sourcePlaneId;
    PlanePolicy planePolicy;
};

struct CreateWorkPlaneDef {
    WorkPlaneMethod method;
    WorkPlaneInputs inputs;
    WorkPlaneParameters parameters;
};

struct CreateWireDef {
    geometry::Wire3d wire;
    std::optional<EntityId> sourcePlaneId;
    PlanePolicy planePolicy;
    bool construction;
    std::map<std::string, std::string> expressions;
};

struct TransformWireDef {
    WireTransformMethod method;
    std::vector<WireChainRef> inputs;
    WireTransformParameters parameters;
};

struct ProjectWireDef {
    WireChainRef source;
    std::variant<EntityRef, SubshapeRef> target;
    geometry::UnitVector3 direction;
    ProjectionHitPolicy hitPolicy;
};

struct CreateGuideSurfaceDef {
    GuideSurfaceMethod method;
    std::vector<RoleChain> chains;
    GuideSurfaceParameters parameters;
};

struct ExtrudeDef {
    std::vector<WireChainRef> profiles;
    ExtrudeDirection direction;
    ExtrudeTermination termination;
    ExtrudeOutputs outputs;
    PartOperation operation;
    std::optional<EntityId> targetPartId;
};

struct CreatePartFromWireCageDef {
    std::vector<WireChainRef> wires;
    std::vector<PatchChoice> acceptedPatches;
};

struct BooleanDef {
    BooleanOperation operation;
    EntityId targetPartId;
    std::vector<EntityId> toolPartIds;
};

struct CreateFabricationModelDef {
    std::vector<FabricationSourceRef> sources;
    FabricationSettings settings;
    std::vector<ManualRoleAssignment> manualRoles;
    AssemblyState assemblyState;
};

struct CreatePatternDef {
    EntityId fabricationModelId;
    std::vector<PanelId> selectedPanels;
    PatternSettings settings;
    std::vector<PatternPlacement> placements;
};

struct FreezeDerivedDef {
    EntityId sourceEntityId;
    std::uint64_t sourceRevision;
    FrozenPayload payload;
};

using FeatureDefinition = std::variant<
    CreatePointDef,
    CreateWorkPlaneDef,
    CreateWireDef,
    TransformWireDef,
    ProjectWireDef,
    CreateGuideSurfaceDef,
    ExtrudeDef,
    CreatePartFromWireCageDef,
    BooleanDef,
    CreateFabricationModelDef,
    CreatePatternDef,
    FreezeDerivedDef>;
```

`TransformWireMethod` は次を含む。

```cpp
Trim
Extend
Split
Join
Coincident
Tangent
CurvatureContinuous
Chamfer
Fillet
Move
Copy
Rotate
Mirror
```

FeatureRecord:

```cpp
struct FeatureOutputRecord {
    std::string key;
    EntityId entityId;
    EntityKind kind;
};

struct FeatureRecord {
    FeatureId id;
    std::string displayName;
    bool enabled;
    FeatureDefinition definition;
    std::vector<FeatureOutputRecord> outputs;
    std::uint64_t revision;
};
```

- output数はpreview評価で確定してからEntity IDを予約する。keyはFeature内で一意かつ再計算間で安定させる。
- 再計算でoutput数が変わるFeatureは、残る意味的outputのIDを維持し、増減をDocumentDeltaへ明示する。
- indexだけで意味的outputを対応させない。output keyを内部評価結果へ持たせる。

## 9. Document

```cpp
enum class GridSubdivision {
    None,
    Half,
    Third,
    Quarter,
};

struct GridSettings {
    bool visible;
    std::optional<EntityId> workPlaneId;
    geometry::Vector2 originUvMm;
    double majorSpacingMm;
    GridSubdivision subdivision;
};

struct DocumentSettings {
    std::string lengthUnit; // "mm"
    std::string angleUnit;  // "deg" for UI/save expression
    geometry::GeometryTolerance tolerance;
    std::optional<GroupId> activeGroupId;
    GridSettings grid;
};

struct DocumentSnapshot {
    DocumentId id;
    std::uint64_t revision;
    DocumentSettings settings;
    std::vector<GroupRecord> groups;
    std::vector<EntityRecord> entities;
    std::vector<FeatureRecord> features;
    std::vector<EntityId> rootOrder;
};

class Document {
public:
    const DocumentSnapshot& Snapshot() const noexcept;
    Result<DocumentDelta> Apply(
        const DocumentCommand& command,
        IFeatureValidator& validator);
    Result<DocumentDelta> Undo(IFeatureValidator& validator);
    Result<DocumentDelta> Redo(IFeatureValidator& validator);
};
```

- public mutable container getterを置かない。
- Entity/Feature検索はtyped IDだけ。
- Snapshotはworkerへ安全に渡せる不変valueまたはshared immutable storage。
- Apply中にDocument本体を変更せず、candidate検証後に1回swapする。

## 10. DocumentCommand

```cpp
struct AddFeatureCommand {
    FeatureRecord feature;
    std::vector<EntityRecord> outputs;
};

struct UpdateFeatureCommand {
    FeatureId featureId;
    std::uint64_t expectedRevision;
    FeatureDefinition replacement;
};

struct RemoveFeatureCommand {
    FeatureId featureId;
    RemovePolicy policy;
};

struct RenameEntityCommand {
    EntityId entityId;
    std::string displayName;
};

struct SetVisibilityCommand {
    std::vector<EntityId> entityIds;
    Visibility visibility;
};

struct UpdatePartPropertiesCommand {
    EntityId partEntityId;
    PartProperties replacement;
};

struct SetActiveGroupCommand {
    std::optional<GroupId> groupId;
};

struct SetGridSettingsCommand {
    GridSettings replacement;
};

struct MoveEntitiesToGroupCommand {
    std::vector<EntityId> entityIds;
    std::optional<GroupId> groupId;
};

using DocumentCommand = std::variant<
    AddFeatureCommand,
    UpdateFeatureCommand,
    RemoveFeatureCommand,
    RenameEntityCommand,
    SetVisibilityCommand,
    UpdatePartPropertiesCommand,
    SetActiveGroupCommand,
    SetGridSettingsCommand,
    MoveEntitiesToGroupCommand,
    AddGroupCommand,
    UpdateGroupCommand,
    RemoveGroupCommand>;
```

- expectedRevision不一致は `DOC-STALE-WRITE`。
- RemovePolicyは `RejectIfReferenced` または `RemoveDependentsAfterPreview`。
- cascade deleteは件数と対象をpreviewし、暗黙実行しない。

## 11. FeatureGraph

```cpp
class FeatureGraph {
public:
    static Result<FeatureGraph> Build(const DocumentSnapshot&);
    Result<std::vector<FeatureId>> EvaluationOrder(FeatureId root) const;
    std::vector<FeatureId> AffectedBy(std::span<const EntityId>) const;
    std::vector<ReferenceProblem> ValidateReferences() const;
};
```

- Buildはcycle、missing ref、kind mismatch、duplicate output ownerを検査する。
- orderはFeatureIdの安定順をtie breakにする。
- AffectedByは入力Entityから下流だけを返す。
- UI表示順を評価順に使わない。

## 12. Geometry evaluator

```cpp
struct DisplayMesh {
    std::vector<geometry::Point3> vertices;
    std::vector<geometry::UnitVector3> normals;
    std::vector<std::uint32_t> triangleIndices;
};

struct MassProperties {
    double volumeMm3;
    geometry::Point3 centerOfMass;
    geometry::Bounds3 bounds;
};

struct EvaluatedEntity {
    EntityId entityId;
    std::uint64_t sourceRevision;
    std::optional<geometry::Point3> point;
    std::optional<geometry::WorkPlane3d> workPlane;
    std::optional<geometry::Wire3d> wire;
    std::optional<DisplayMesh> displayMesh;
    std::optional<MassProperties> massProperties;
    std::vector<SubshapeKey> subshapeKeys;
};

struct EvaluationBundle {
    std::vector<EvaluatedEntity> entities;
    std::vector<Diagnostic> diagnostics;
};

class IGeometryEvaluator {
public:
    virtual ~IGeometryEvaluator() = default;
    virtual Result<EvaluationBundle> Evaluate(
        const DocumentSnapshot& snapshot,
        std::span<const FeatureId> roots,
        const CancellationToken& cancel) = 0;
};
```

- ResultにTopoDS型を入れない。
- Partの正本B-RepはOCCT evaluatorのprivate cacheに置く。
- export serviceはEntityId+revisionで同じcache shapeを要求する。
- cache missは同じFeature定義から再評価する。

## 13. Modeling preview services

UIはFeatureをcommitする前に次を呼ぶ。

```cpp
class IWireChainAnalyzer {
public:
    virtual Result<WireChainAnalysis> Analyze(
        const DocumentSnapshot&,
        std::span<const SegmentRef>,
        bool expectedClosed) = 0;
};

class IPartCandidateService {
public:
    virtual Result<PartCandidateSet> FindCandidates(
        const DocumentSnapshot&,
        const PartCandidateScope&,
        const CancellationToken&) = 0;
};

class IExtrudePreviewService {
public:
    virtual Result<ExtrudePreview> Preview(
        const DocumentSnapshot&,
        const ExtrudeDef&,
        const CancellationToken&) = 0;
};

class IGuideSurfacePreviewService {
public:
    virtual Result<GuideSurfacePreview> Preview(
        const DocumentSnapshot&,
        const CreateGuideSurfaceDef&,
        const CancellationToken&) = 0;
};
```

Preview resultは一時表示用でDocumentへ自動追加しない。確定時は同じDefinitionをCommandへ渡し、
validatorが再検証する。UI previewだけを信用してcommitしない。

## 14. Fabrication API

製作Featureへ保存する設定値は計算serviceではなく `domain` の純データとする。これにより
`domain/document` が `fabrication` へ逆依存しない。

```cpp
namespace kachakacha::domain {

enum class FabricationStrategy {
    OnePiece,
    FewPieces,
    SeparatePanels,
    Hybrid,
};

enum class FabricationSourceRole {
    Approximate,
    PreserveShape,
    ConnectionReference,
    Ignore,
};

struct PartFabricationSource {
    EntityId partEntityId;
    std::vector<SubshapeKey> subshapeKeys;
    FabricationSourceRole role;
};

struct GuideFabricationSource {
    EntityId guideSurfaceEntityId;
    FabricationSourceRole role;
};

struct WireFabricationSource {
    WireChainRef wire;
    FabricationSourceRole role;
};

using FabricationSourceRef = std::variant<
    PartFabricationSource,
    GuideFabricationSource,
    WireFabricationSource>;

enum class BendDirection {
    Auto,
    U,
    V,
    Both,
};

enum class ReliefShape {
    Auto,
    StraightSlit,
    VNotch,
    CurvedVNotch,
};

enum class PanelType {
    Planar,
    Cylindrical,
    Conical,
    TangentDevelopable,
};

enum class ThicknessPlacement {
    Inside,
    Centered,
    Outside,
};

enum class MaterialKind {
    Paper,
    Styrene,
    Brass,
    Other,
};

struct MaterialSpec {
    MaterialKind kind;
    std::string displayName;
};

struct FabricationSettings {
    FabricationStrategy strategy;
    int fidelityLevel;
    std::optional<double> explicitMaxDeviationMm;
    int panelCountLimit;
    double minimumPanelWidthMm;
    BendDirection preferredBendDirection;
    std::vector<PanelType> allowedPanelTypes;
    bool reliefCutsEnabled;
    BendDirection reliefDirection;
    ReliefShape reliefShape;
    double maximumReliefDepthRatio;
    double minimumLigamentMm;
    bool preserveOpenings;
    double outputThicknessMm;
    ThicknessPlacement thicknessPlacement;
    MaterialSpec material;
};

enum class ManualRole {
    PanelBoundary,
    FoldLine,
    ReliefCut,
    Opening,
    KeepTogether,
    NoCutZone,
    BendDirection,
};

struct ManualRoleAssignment {
    ManualRole role;
    WireChainRef wire;
    std::optional<FeatureId> sourceProjectionFeatureId;
};

struct AssemblyState {
    double masterPercent;
    std::map<FoldId, double> foldProgressOverrides;
};

enum class FrozenWireRole {
    General,
    Boundary,
    Fold,
    Cut,
    Opening,
};

struct FrozenWirePayload {
    std::string outputKey;
    FrozenWireRole role;
    std::optional<PanelId> panelId;
    geometry::Wire3d wire;
};

struct FrozenPartPayload {
    std::string outputKey;
    AssetId assetId;
};

struct FrozenPayload {
    std::optional<AssemblyState> assemblyState;
    std::vector<PanelId> selectedPanelIds;
    std::vector<FrozenWirePayload> wires;
    std::vector<FrozenPartPayload> parts;
};

enum class PaperSize {
    A4,
    A3,
    Custom,
};

enum class PatternOrientation {
    Auto,
    Portrait,
    Landscape,
};

struct PatternSettings {
    PaperSize paper;
    PatternOrientation orientation;
    std::optional<geometry::Vector2> customPaperSizeMm;
    double pageOverlapMm;
    bool showPartNumbers;
    bool showFoldDirections;
    bool showMatePairs;
    bool showReferenceScale;
};

struct PatternPlacement {
    PanelId panelId;
    geometry::Vector2 translationMm;
    double rotationRad;
};

}

namespace kachakacha::fabrication {

using domain::AssemblyState;
using domain::FabricationSettings;
using domain::ThicknessPlacement;

struct SurfaceSample {
    geometry::Vector2 uv;
    geometry::Point3 point;
    geometry::Vector3 derivativeU;
    geometry::Vector3 derivativeV;
    geometry::UnitVector3 normal;
    double principalCurvature1PerMm;
    double principalCurvature2PerMm;
    geometry::UnitVector3 principalDirection1;
    geometry::UnitVector3 principalDirection2;
};

struct SurfaceBoundary {
    std::string key;
    geometry::Wire3d curve3d;
    std::vector<geometry::Vector2> uvSamples;
    bool opening;
};

struct FabricationSamplingRequest {
    double targetChordDeviationMm;
    double maximumSampleSpacingMm;
    int maximumSubdivisionDepth;
};

struct FabricationSurfacePatch {
    std::string sourceKey;
    domain::FabricationSourceRole role;
    std::vector<SurfaceSample> samples;
    std::vector<SurfaceBoundary> boundaries;
    std::vector<std::string> adjacentSourceKeys;
    double measuredSamplingDeviationMm;
};

struct FabricationInputGeometry {
    std::uint64_t documentRevision;
    std::vector<FabricationSurfacePatch> patches;
    std::vector<Diagnostic> warnings;
};

class IFabricationGeometryProvider {
public:
    virtual ~IFabricationGeometryProvider() = default;
    virtual Result<FabricationInputGeometry> Extract(
        const DocumentSnapshot&,
        std::span<const domain::FabricationSourceRef>,
        const FabricationSamplingRequest&,
        const geometry::GeometryTolerance&,
        const CancellationToken&) = 0;
};

class IFabricationAlgorithm {
public:
    virtual ~IFabricationAlgorithm() = default;
    virtual Result<FabricationPreview> Generate(
        const FabricationInputGeometry&,
        const domain::FabricationSettings&,
        std::span<const domain::ManualRoleAssignment>,
        const CancellationToken&) = 0;
};

enum class FabricationWireRole {
    Boundary,
    Fold,
    Cut,
    Opening,
};

struct MaterializeOptions {
    bool includeBoundaryWires;
    bool includeFoldWires;
    bool includeCutWires;
    bool includeOpeningWires;
    bool includeParts;
    bool joinMatedPanels;
};

struct ErrorMetrics {
    double maxDeviationMm;
    double rmsDeviationMm;
    double maxClosureGapMm;
    double maxMetricDistortionRelative;
};

struct MaterializedWire {
    std::string outputKey;
    FabricationWireRole role;
    std::optional<PanelId> panelId;
    geometry::Wire3d wire;
};

struct FabricationMaterialization {
    std::vector<MaterializedWire> wires;
    std::vector<modeling::PartBuildRequest> partRequests;
    ErrorMetrics metrics;
};

class IFabricationService {
public:
    virtual Result<FabricationPreview> Generate(
        const DocumentSnapshot&,
        const CreateFabricationModelDef&,
        const CancellationToken&) = 0;

    virtual Result<AssemblyPreview> Assemble(
        const FabricationModelData&,
        const AssemblyState&,
        const CancellationToken&) = 0;

    virtual Result<FabricationMaterialization> Materialize(
        const FabricationModelData&,
        const AssemblyState&,
        std::span<const PanelId>,
        const MaterializeOptions&,
        ThicknessPlacement,
        const CancellationToken&) = 0;
};

}
```

`std::map` はFoldId順の決定性のために使う。別containerを使う場合もserializeと評価順を安定ソートする。
`IFabricationService::Generate` はfacadeであり、内部でproviderの `Extract` とalgorithmの `Generate` を順に呼ぶ。
sampling requestはFabricationSettingsの目標偏差から作り、`targetChordDeviationMm` を目標偏差の1/5以下とする。
providerがその精度を達成できないpatchはWarningではなく生成失敗にする。
`DisplayMesh` を `FabricationInputGeometry` へ変換するadapterを作ってはならない。
`includeParts=true` のとき、`partRequests` は同じ戻り値の `wires` とpanel面を入力にして作る。
境界を別評価してはならず、commit前にPart境界との一致を検査する。全include flagがfalseなら失敗する。

## 15. Export API

```cpp
struct ExportTarget {
    std::vector<EntityId> partIds;
    std::vector<EntityId> wireIds;
    std::optional<EntityId> patternId;
    std::optional<EntityId> fabricationModelId;
    std::vector<PanelId> panelIds;
    std::optional<fabrication::AssemblyState> assemblyState;
};

struct ExportOptions {
    ExportFormat format;
    std::filesystem::path destination;
    double meshDeflectionMm;
    PdfPaperSettings paper;
};

class IExportService {
public:
    virtual Result<ExportReport> Validate(
        const DocumentSnapshot&,
        const ExportTarget&,
        const ExportOptions&) = 0;

    virtual Result<ExportReport> Write(
        const DocumentSnapshot&,
        const ExportTarget&,
        const ExportOptions&,
        const CancellationToken&) = 0;
};
```

- Writeは内部でValidateを再実行する。
- destinationへの直接書込ではなくtemp+validate+replace。
- ExportReportに対象ID、出力component数、bbox、volume、warning、file sizeを含める。
- selected targetをvisibilityから推測しない。

## 16. UI境界

```cpp
enum class SelectionElementKind {
    Object,
    Vertex,
    Edge,
    Face,
    ControlPoint,
    WorkPlane,
};

struct SelectionRef {
    EntityId entityId;
    SelectionElementKind kind = SelectionElementKind::Object;
    std::optional<SegmentId> segmentId;
    std::optional<SubshapeKey> subshapeKey;
    std::optional<double> curveParameter;
    geometry::Point3 hitPoint;
    double screenDistancePx = 0.0;
};

struct SelectionSnapshot {
    std::vector<SelectionRef> ordered;
};

enum class ToolSessionPhase {
    Idle,
    AwaitingInput,
    EditingParameters,
    PreviewReady,
    InvalidPreview,
};

struct ToolInputRequirement {
    std::string roleId;
    std::vector<SelectionElementKind> acceptedKinds;
    std::size_t minimumCount = 0;
    std::optional<std::size_t> maximumCount;
    bool ordered = true;
};

using ToolParameterValue = std::variant<
    bool,
    std::int64_t,
    double,
    std::string,
    EntityId,
    geometry::Point3,
    geometry::Vector3>;

using ToolParameterMap = std::map<std::string, ToolParameterValue>;

struct ToolPreviewState {
    std::uint64_t generation = 0;
    bool visible = false;
};

struct ToolSessionSnapshot {
    ToolId toolId;
    ToolSessionPhase phase = ToolSessionPhase::Idle;
    std::vector<ToolInputRequirement> requirements;
    std::map<std::string, std::vector<SelectionRef>> capturedInputs;
    ToolParameterMap parameters;
    std::optional<ToolPreviewState> preview;
    std::vector<Diagnostic> diagnostics;
    bool canCommit = false;
    bool canStepBack = false;
};

class IToolController {
public:
    virtual ~IToolController() = default;
    virtual ToolId Id() const noexcept = 0;
    virtual ToolSessionSnapshot Snapshot() const = 0;
    virtual OperationGuideModel Guide() const = 0;
    virtual void Begin(const SelectionSnapshot&) = 0;
    virtual void SelectionChanged(const SelectionSnapshot&) = 0;
    virtual void PointerMove(const PointerEvent&) = 0;
    virtual void PointerPress(const PointerEvent&) = 0;
    virtual void KeyPress(const KeyEvent&) = 0;
    virtual bool StepBack() = 0;
    virtual void Cancel() = 0;
    virtual Result<DocumentCommand> BuildCommitCommand() const = 0;
};
```

- `SelectionRef` はEntityIdだけへ縮退させない。SegmentId/SubshapeKeyを失う変換を禁止する。
- `hitPoint` と `screenDistancePx` はpick時の情報であり、保存形式へ書かない。
- `SelectionSnapshot::ordered` は利用者が選んだ順を保つ。同一Entityの異なるサブ要素を重複除去しない。
- ViewportはPointerEventをactive toolへ渡すだけ。
- Tool controllerはDocumentを直接mutateしない。
- ControllerはPreview serviceを呼び、commit時にDocumentCommandを返す。
- ToolSessionSnapshotを右コンテキストプロパティとカーソル近傍入力の唯一の状態源にする。
- OperationGuideModelは各toolが必ず提供し、空stepを禁止する。ビューポート表示は2行以内に要約する。
- QAction、toolbar、right panel buttonは同じToolIdとcommand registryへbindする。
- 事前選択とツール開始後の選択は同じ `SelectionChanged` 経路で役割へ取り込む。
- `StepBack` がfalseを返したときだけEscでToolSessionを終了する。

## 17. 変更手続き

この公開契約を変える必要がある場合:

1. 影響WPと試験IDを列挙する。
2. 旧signature、新signature、移行手順を示す。
3. `public-api-contract.md` とcompile testを先に変更する。
4. 統合担当の承認後に各WPを更新する。
5. 一時的なoverloadには削除WPと期限を付ける。

担当AIが自分のWPだけで似た型を新設して回避してはならない。
