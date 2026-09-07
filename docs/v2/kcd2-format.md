# Wire-first V2 `.kcd2` 符号化仕様

## 1. 適用範囲

移行中の拡張子は `.kcd2`、正式切替後は `.kcd` である。中身は同じZIP+JSON形式とし、
拡張子以外を切替時に変更しない。

## 2. ZIP entry

許可するentry:

```text
document.json
geometry/<lowercase UUID>.brep
preview/<lowercase UUID>.glb
meta/thumbnail.png
```

規則:

- entry pathはUTF-8、`/` 区切り。
- 絶対path、drive letter、`..`、backslash、NULを拒否する。
- 同名entryを拒否する。
- `document.json` は1個だけ。
- 1 entryの展開後上限は512MiB、全entry合計は2GiB、entry数は10000。
- 圧縮率1000倍を超えるentryを拒否する。
- previewとthumbnailは欠けても文書結果へ影響しない。
- geometry assetがmanifestで必須なら欠損を拒否する。

## 3. JSON共通規則

- UTF-8 BOMなし。
- JSON comments禁止。
- keyはlower camelCase。
- enumはlower snake_case string。
- UUIDはlowercase canonical string。
- finite doubleだけ。
- 長さはmm、保存角度はrad。利用者が入力した式の単位表示だけdegを保持できる。
- optional値は原則key省略。意味のある「未設定」だけ明示null。
- 空配列は意味がある場合に保存し、loaderは省略を空配列と同一視しない。
- loaderは必須key欠損、型違い、未知enumを拒否する。
- `uiState.extensions` 以外の未知keyはWarning付きで保持できるが、未知Feature typeは拒否する。
- writerはobject keyをこの文書の記載順、Entity/Feature/Groupをroot orderまたはUUID順で決定論的に出す。

## 4. ルート

```json
{
  "format": "kachakachaCAD",
  "schemaVersion": 2,
  "documentId": "00000000-0000-4000-8000-000000000001",
  "revision": 42,
  "units": {
    "length": "mm",
    "storedAngle": "rad",
    "displayAngle": "deg"
  },
  "tolerances": {
    "numericEpsilon": 1e-12,
    "modelLinearMm": 0.000001,
    "modelAngularRad": 1e-9,
    "interactiveJoinMm": 0.01
  },
  "metadata": {
    "title": "ER2前頭部",
    "author": "",
    "description": ""
  },
  "activeGroupId": null,
  "grid": {
    "visible": false,
    "workPlaneId": null,
    "originUvMm": {"x": 0.0, "y": 0.0},
    "majorSpacingMm": 10.0,
    "subdivision": "half"
  },
  "groups": [],
  "entities": [],
  "features": [],
  "rootOrder": [],
  "uiState": {
    "activeMode": "drawing",
    "activeWorkPlaneId": null,
    "selectionFilter": ["point", "wire", "guide_surface", "part"],
    "theme": "windows95",
    "displaySettings": {
      "displayMode": "outline_shaded",
      "lineStyles": {},
      "partFace": {"red": 192, "green": 208, "blue": 214, "alpha": 255},
      "guideFace": {"red": 150, "green": 210, "blue": 215, "alpha": 96},
      "background": {"red": 238, "green": 238, "blue": 238, "alpha": 255},
      "workPlane": {"red": 120, "green": 170, "blue": 175, "alpha": 72},
      "gridMajor": {"red": 84, "green": 130, "blue": 136, "alpha": 180},
      "gridMinor": {"red": 84, "green": 130, "blue": 136, "alpha": 99},
      "pointSizePx": 4.0,
      "snapRingSizePx": 16.0
    },
    "extensions": {}
  },
  "assets": []
}
```

`revision` は保存時のDocument revisionであり、読込後に0へ戻さない。

grid subdivisionは `none | half | third | quarter`。visible=trueではworkPlaneIdを必須とする。
`originUvMm` は指定WorkPlaneの局所座標であり、
画面では評価済みXYZも併記する。major spacingはfiniteな正値とする。WorkPlaneが動けばUVを保って追従し、
参照切れでは別平面へ移さずBrokenReferenceにする。

## 5. GroupRecord

```json
{
  "id": "10000000-0000-4000-8000-000000000001",
  "displayName": "前面",
  "parentGroupId": null,
  "state": "visible",
  "childOrder": [
    "20000000-0000-4000-8000-000000000001"
  ]
}
```

`state`:

```text
visible
reference
hidden
```

Group cycle、同じchildの複数parent、存在しないchildOrder IDを拒否する。

## 6. EntityRecord

```json
{
  "id": "20000000-0000-4000-8000-000000000001",
  "kind": "wire",
  "displayName": "窓上輪郭",
  "groupId": "10000000-0000-4000-8000-000000000001",
  "visibility": "visible",
  "editPolicy": "source",
  "createdBy": "30000000-0000-4000-8000-000000000001",
  "revision": 4,
  "partProperties": null
}
```

`kind`:

```text
point
work_plane
wire
guide_surface
part
fabrication_model
pattern
```

`editPolicy`:

```text
source
derived
frozen
```

- Entityに幾何payloadを保存しない。
- `createdBy` のFeatureが同Entity IDをoutputsに1回だけ含むこと。
- `kind` とFeature output kindが一致すること。
- `partProperties` はkind=`part` ではobjectを必須とし、他kindではnullとする。

Partの属性例:

```json
{
  "purpose": "finished_model",
  "manufacturing": {
    "materialName": "ABS",
    "displayColor": {"red": 220, "green": 220, "blue": 220, "alpha": 255},
    "processName": "3D print",
    "nominalThicknessMm": 1.0,
    "referenceScaleDenominator": 87.0,
    "notes": ""
  }
}
```

`purpose` は `finished_model | fabrication_part | fixture | imported`。`manufacturing` はnullを許す。
色成分は0..255、板厚と縮尺分母はfiniteな正値とする。未指定のoptional subfieldは省略する。

## 7. FeatureRecord

共通形:

```json
{
  "id": "30000000-0000-4000-8000-000000000001",
  "type": "create_wire",
  "displayName": "窓上輪郭を作成",
  "enabled": true,
  "revision": 4,
  "definition": {},
  "outputs": [
    {
      "key": "wire",
      "entityId": "20000000-0000-4000-8000-000000000001",
      "kind": "wire"
    }
  ]
}
```

- `outputs.key` は同Feature内で一意。
- 再計算時は同じ意味のkeyへ同じEntityIdを使う。
- indexだけで対応させない。
- typeとdefinitionの組合せが違う場合は拒否する。

type enumは次の12種類だけとする。

```text
create_point
create_work_plane
create_wire
transform_wire
project_wire
create_guide_surface
extrude
create_part_from_wire_cage
boolean
create_fabrication_model
create_pattern
freeze_derived
```

transformの個別method、booleanのadd/cutをtypeへ昇格させない。schemaはtypeごとにdefinitionの必須keyを
`oneOf` で検査し、未知typeと不一致definitionを拒否する。

## 8. Geometry

### 8.1 Point3/Vector3

```json
{"x": 1.0, "y": 2.0, "z": 3.0}
```

Point3の値はmm。単位vectorは同じ形で保存し、loaderが長さ1へ正規化せず、規定角度差内で1でなければ拒否する。

### 8.2 Segment

Line:

```json
{
  "id": "40000000-0000-4000-8000-000000000001",
  "type": "line",
  "start": {"x": 0.0, "y": 0.0, "z": 0.0},
  "end": {"x": 10.0, "y": 0.0, "z": 0.0},
  "provenance": []
}
```

Arc:

```json
{
  "id": "40000000-0000-4000-8000-000000000002",
  "type": "circular_arc",
  "center": {"x": 0.0, "y": 0.0, "z": 0.0},
  "normal": {"x": 0.0, "y": 0.0, "z": 1.0},
  "xDirection": {"x": 1.0, "y": 0.0, "z": 0.0},
  "radiusMm": 10.0,
  "startAngleRad": 0.0,
  "sweepAngleRad": 1.5707963267948966,
  "provenance": []
}
```

Circleは `startAngleRad` と `sweepAngleRad` を持たない。Bezierは4個の `controlPoints`。
B-spline:

```json
{
  "id": "40000000-0000-4000-8000-000000000003",
  "type": "cubic_b_spline",
  "degree": 3,
  "controlPoints": [],
  "knots": [],
  "multiplicities": [],
  "weights": null,
  "periodic": false,
  "provenance": []
}
```

### 8.3 Wire

```json
{
  "segments": [],
  "closed": false
}
```

Segment順が幾何順である。隣接端点をloaderが検査する。

### 8.4 SegmentRef/WireChainRef

```json
{
  "wireEntityId": "20000000-0000-4000-8000-000000000001",
  "segmentId": "40000000-0000-4000-8000-000000000001",
  "range": {"first": 0.0, "last": 1.0},
  "reversed": false
}
```

```json
{
  "segments": [],
  "expectedClosed": true
}
```

Feature確定後は自動整列前の選択順でなく、解決済み順序を保存する。

## 9. Create feature definitions

### 9.1 create_point

```json
{
  "position": {"x": 0.0, "y": 0.0, "z": 0.0},
  "sourcePlaneId": null,
  "planePolicy": "free_3d"
}
```

### 9.2 create_work_plane

```json
{
  "method": "three_points",
  "inputs": {
    "pointEntityIds": [
      "20000000-0000-4000-8000-000000000010",
      "20000000-0000-4000-8000-000000000011",
      "20000000-0000-4000-8000-000000000012"
    ]
  },
  "parameters": {
    "flipNormal": false,
    "uDirectionHint": null
  }
}
```

method enum:

```text
standard
offset
parallel_through_point
mid_plane
cylinder_axis
angle_about_edge
three_points
two_edges
tangent_through_edge
tangent_through_point
normal_to_curve_at_point
```

各methodで不要なinput keyを拒否する。

### 9.3 create_wire

```json
{
  "wire": {"segments": [], "closed": false},
  "sourcePlaneId": null,
  "planePolicy": "reference_only",
  "construction": false,
  "expressions": {
    "length": "(180/2)*3"
  }
}
```

`expressions` は再編集用で、評価済みgeometryと一致しなければ読込拒否する。

## 10. Transform/projection

```json
{
  "method": "trim",
  "inputs": [],
  "parameters": {
    "pickedParameter": 0.4,
    "boundaryRefs": []
  }
}
```

```json
{
  "source": {"segments": [], "expectedClosed": true},
  "target": {
    "kind": "subshape",
    "partEntityId": "20000000-0000-4000-8000-000000000020",
    "subshapeKey": "extrude/cap/end"
  },
  "direction": {"x": 0.0, "y": 0.0, "z": -1.0},
  "hitPolicy": "nearest_positive"
}
```

target kindは `entity` または `subshape`。

## 11. GuideSurface

```json
{
  "method": "guided_loft",
  "chains": [
    {
      "role": "guide_u",
      "index": 1,
      "chain": {"segments": [], "expectedClosed": false},
      "correspondenceParameter": null
    },
    {
      "role": "section",
      "index": 1,
      "chain": {"segments": [], "expectedClosed": false},
      "correspondenceParameter": 0.25
    }
  ],
  "parameters": {
    "createVirtualEndSections": true,
    "boundaryContinuity": "g0"
  }
}
```

role enum:

```text
outer_boundary
hole_boundary
section
guide_u
guide_v
boundary_side
source_surface
```

indexは1始まりで、同role内一意。配列順はUI表順。

## 12. Extrude

```json
{
  "profiles": [],
  "direction": {
    "method": "profile_normal",
    "vector": null,
    "flip": false
  },
  "termination": {
    "method": "distance",
    "positiveDistanceMm": 20.0,
    "negativeDistanceMm": null,
    "target": null,
    "expression": "40/2"
  },
  "outputs": {
    "endProfileWires": true,
    "sideBoundaryWires": true,
    "parts": true
  },
  "operation": "new_part",
  "targetPartId": null
}
```

termination method:

```text
distance
symmetric_distance
two_distances
to_target
through_all
```

operation:

```text
new_part
add
cut
```

add/cutではtargetPartId必須、newではnull必須。

## 13. Part from cage/Boolean

```json
{
  "wires": [],
  "acceptedPatches": [
    {
      "key": "patch/front",
      "method": "planar",
      "boundary": {"segments": [], "expectedClosed": true},
      "guideSurfaceId": null
    }
  ]
}
```

```json
{
  "operation": "cut",
  "targetPartId": "20000000-0000-4000-8000-000000000020",
  "toolPartIds": [
    "20000000-0000-4000-8000-000000000021"
  ]
}
```

## 14. FabricationModel

```json
{
  "sources": [
    {
      "kind": "part_subshape",
      "partEntityId": "20000000-0000-4000-8000-000000000020",
      "subshapeKeys": ["loft/span/a/b"],
      "role": "approximate"
    }
  ],
  "settings": {
    "strategy": "few_pieces",
    "fidelityLevel": 6,
    "explicitMaxDeviationMm": null,
    "panelCountLimit": 24,
    "minimumPanelWidthMm": 1.0,
    "preferredBendDirection": "auto",
    "allowedPanelTypes": ["planar", "cylindrical", "conical", "tangent_developable"],
    "reliefCutsEnabled": true,
    "reliefDirection": "both",
    "reliefShape": "auto",
    "maximumReliefDepthRatio": 0.55,
    "minimumLigamentMm": 0.5,
    "preserveOpenings": true,
    "outputThicknessMm": 0.2,
    "thicknessPlacement": "centered",
    "material": {"kind": "paper", "displayName": "紙"}
  },
  "manualRoles": [],
  "assemblyState": {
    "masterPercent": 30.0,
    "foldOverrides": []
  }
}
```

source kindと必須field:

```json
{
  "kind": "part_subshape",
  "partEntityId": "20000000-0000-4000-8000-000000000020",
  "subshapeKeys": ["loft/span/a/b"],
  "role": "approximate"
}
```

```json
{
  "kind": "guide_surface",
  "guideSurfaceEntityId": "20000000-0000-4000-8000-000000000021",
  "role": "preserve_shape"
}
```

```json
{
  "kind": "wire_chain",
  "wire": {"segments": [], "expectedClosed": false},
  "role": "connection_reference"
}
```

roleは `approximate | preserve_shape | connection_reference | ignore`。
`wire_chain` はconnection_referenceだけを許す。空のsubshapeKeysと空WireChainを拒否する。
同じsource範囲へ矛盾するroleを重ねた場合は読込およびcommitを拒否する。

manual role:

```json
{
  "role": "relief_cut",
  "wire": {"segments": [], "expectedClosed": false},
  "sourceProjectionFeatureId": null
}
```

role enum:

```text
panel_boundary
fold_line
relief_cut
opening
keep_together
no_cut_zone
bend_direction
```

fold override:

```json
{
  "foldId": "50000000-0000-4000-8000-000000000001",
  "progressPercent": 25.0
}
```

Panel/Fold/Cutの生成結果はFeature definitionへ保存しない。入力とsettingsから再生成する。
安定IDはsource keysとdeterministic generation keyから対応し、ランダム再発行しない。

## 15. Pattern

```json
{
  "fabricationModelId": "20000000-0000-4000-8000-000000000030",
  "selectedPanelIds": [],
  "settings": {
    "paper": "a4",
    "orientation": "auto",
    "pageOverlapMm": 5.0,
    "showPartNumbers": true,
    "showFoldDirections": true,
    "showMatePairs": true,
    "showReferenceScale": true
  },
  "placements": []
}
```

`paper="custom"` の場合だけ `customPaperSizeMm={"x": ..., "y": ...}` を必須とし、他では省略する。
placementは次を持つ。scaleとmirror fieldを持たない。

```json
{
  "panelId": "50000000-0000-4000-8000-000000000001",
  "translationMm": {"x": 10.0, "y": 15.0},
  "rotationRad": 0.0
}
```

## 16. Frozen/imported payload

```json
{
  "sourceEntityId": "20000000-0000-4000-8000-000000000030",
  "sourceRevision": 9,
  "payload": {
    "assemblyState": {
      "masterPercent": 30.0,
      "foldOverrides": []
    },
    "selectedPanelIds": [
      "50000000-0000-4000-8000-000000000001"
    ],
    "wires": [
      {
        "outputKey": "wire/boundary/1",
        "role": "boundary",
        "panelId": "50000000-0000-4000-8000-000000000001",
        "wire": {"segments": [], "closed": true}
      }
    ],
    "parts": [
      {
        "outputKey": "part/1",
        "assetId": "60000000-0000-4000-8000-000000000001"
      }
    ]
  }
}
```

- `assemblyState` はFabricationModelから固定した場合に必須、その他のDerived Wire/Part固定では省略する。
- wire roleは `general | boundary | fold | cut | opening`。Fabrication由来ではgeneralを使わない。
- `panelId` はpanelに属するWireだけに付け、全体Foldなどは省略できる。
- `wires` と `parts` の合計は1以上とする。
- `outputKey` はFeatureRecord.outputsのkeyと一致し、Wire payloadはkind=`wire`、Part payloadはkind=`part` とする。
- `ワイヤーのみ / 部品のみ / 両方` は空でない配列の組合せとして表し、別Feature typeを増やさない。
- 両方を持つFabrication snapshotは保存前にPart境界とWireの一致検査を通す。

asset manifest:

```json
{
  "id": "60000000-0000-4000-8000-000000000001",
  "kind": "occt_brep",
  "path": "geometry/20000000-0000-4000-8000-000000000040.brep",
  "sha256": "<lowercase hex>",
  "uncompressedSize": 12345
}
```

- sha256、size、BREP validityを読込時に検査する。
- asset pathをIDから推測せずmanifestを正本にする。
- 通常のderived PartへBREP assetを保存しない。

## 17. UI state

UI stateは幾何結果へ影響させない。

保存可:

- active mode/tab。
- active work plane。
- selection filter。
- camera quaternion、target、distance、projection mode。
- dock size/visibility。
- themeとdisplay settings。
- last export directoryの相対または空値。

保存禁止:

- absolute path to source PC。
- pointer/handle。
- current hover。
- incomplete command preview。
- worker state。
- cached diagnostic textだけから復元する状態。

themeは `windows95 | standard`。display modeは
`outline | shaded | outline_shaded | wireframe | curvature | fabrication_error`。
`lineStyles` のkeyは次から必要なものを保存する。

```text
wire.default
wire.construction
selection
hover
reference
error
part.outline
guide.iso
fabrication.fold
fabrication.cut
fabrication.opening
fabrication.boundary
```

line style value:

```json
{
  "color": {"red": 20, "green": 45, "blue": 55, "alpha": 255},
  "widthPx": 1.0,
  "pattern": "solid"
}
```

patternは `solid | dashed | dash_dot`。色成分は0..255、幅・点サイズはfiniteな正値、alphaは表示用であり
幾何や出力線種へ暗黙適用しない。未知line style keyは`uiState.extensions`以外ではWarning付き保持できるが、
標準keyと同名の型違いは拒否する。

## 18. JSON Schema成果物

WP-05はこの文書から `src/core/kachakacha/io/schema/kcd-v2.schema.json` を作り、次を行う。

1. schema自身をJSON Schema validatorで検証する。
2. 全fixtureをschema validationする。
3. invalid fixtureを各規則につき最低1つ持つ。
4. C++ loader独自検証とschema結果が矛盾しない。
5. schemaを配布zipの `docs/` へ含める。

この文書とschemaが矛盾する場合は、この文書を先に修正し、同じcommitでschemaを更新する。
