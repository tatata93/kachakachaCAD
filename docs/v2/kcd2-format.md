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

## 19. WP-05実装で確定した点

この節はWP-05の実装(`src/next/kachakacha/io/DocumentFile.*`)で確定した内容であり、
上の各節と食い違う場合はこの節を正とする。次の実装WPで上の節へ吸収する。

### 19.1 ZIP診断コード

```text
KCD2-Z001  entry名が安全でない(絶対path、drive letter、`..`、backslash、制御文字、空区間)
KCD2-Z002  同名entry
KCD2-Z003  構造が途切れている(目録、局所ヘッダ、実データ)
KCD2-Z004  対応していない圧縮方式(このソフトはstoredだけを書く)
KCD2-Z005  CRC-32が合わない
KCD2-Z006  上限超過(entry数、1entryの大きさ、合計、展開倍率)
KCD2-Z007  ZIPではない
KCD2-Z008  目録と局所ヘッダでentry名が食い違う
```

Z008は本仕様の追加である。ZIPはentry名に検査値を持たないため、名前が1バイト化けても
別名として通ってしまう。局所ヘッダと中央ディレクトリの2箇所に同じ名前が書かれるので、
読込時に突き合わせる。書き出しは日時を固定値(time=0, date=0x21)にし、
同じ入力から必ず同じバイト列を出す。

### 19.2 document.json診断コード

```text
KCD2-D001  format / schemaVersion が違う(このソフトの文書ではない)
KCD2-D002  型違い、範囲外、UUIDとして不正、有限でない数
KCD2-D003  知らないenum
KCD2-D004  参照切れ、重複ID、構造の矛盾
KCD2-D005  書庫に document.json が無い
KCD2-D100  警告(この版がまだ扱えない指示のdefinitionを保持した、など)
```

JSON層の診断(`KCD2-J001`〜`J004`)はそのまま透過する。

### 19.3 §4ルートへの追加key

- `tolerances` に `displayPickPx` と `candidateMenuPx` を含める(GeometryToleranceの全項目)。
  この2つは省略可能で、欠けた場合は既定値を使う。他の4つは必須とし、正でなければ拒否する。
- `revision` は0以上の整数でなければならない。負・小数は拒否する。

### 19.4 §6 EntityRecordへの追加key

- `construction`(bool、必須ではない。既定false)。V1の補助線と同等の意味を持つ。
- `partProperties.manufacturing` に `colorName`(string)と `layerCount`(1以上の整数)を含める。
  `layerCount` はD-3の積層を専用オブジェクトなしで表すためのもの。
- `partProperties` はkind=`part` でobject必須、他kindではnull必須。どちらの違反も拒否する。

### 19.5 §7 FeatureRecordへの追加key

- `inputEntityIds`(配列、必須)。DAGの辺をここから作る。
  §7の例には無かったが、`definition` から辺を再構成すると未実装Featureの辺が消えるため、
  明示的に持つ。

### 19.6 実装済みdefinitionの形

`create_point`:

```json
{
  "position": {"x": 0.0, "y": 0.0, "z": 0.0},
  "sourcePlaneId": null,
  "expressions": {
    "x": {"expression": "10*2", "value": 20.0, "quantity": "length"},
    "y": {"expression": "0", "value": 0.0, "quantity": "length"},
    "z": {"expression": "0", "value": 0.0, "quantity": "length"}
  }
}
```

`expressions` の各値は §9.1 になかったが、PRD-092(式を再編集できること)のために必須とする。
`quantity` は `length | angle | scalar`。

`create_wire`:

```json
{
  "wire": {"segments": [], "closed": false},
  "sourcePlaneId": null,
  "construction": false
}
```

`transform_wire`:

```json
{
  "method": "rotate",
  "inputs": [],
  "parameters": {
    "vector": {"x": 0.0, "y": 0.0, "z": 1.0},
    "point": {"x": 0.0, "y": 0.0, "z": 0.0},
    "scalar": {"expression": "deg(30)", "value": 0.5235987755982988, "quantity": "angle"},
    "scalar2": 0.0,
    "keepFirst": 0,
    "keepSecond": 0,
    "corner": -1
  }
}
```

method enumは `move copy rotate mirror trim extend split join fillet chamfer offset
meet_lines coincident tangent curvature corner_chamfer corner_fillet`。

`scalar2` `keepFirst` `keepSecond` `corner` は面取りの欄(V1 の「面取り」欄)。
`scalar2` は C面取りの B 側の切戻し(mm、0 なら `scalar` と同じ)、`keepFirst` / `keepSecond` は
残す側(0 自動、1 始点側、2 終点側)、`corner` は角の加工で 1 つの角だけにするときの頂点番号
(-1 なら全部)。無ければその既定で読む(古い文書はそのまま開ける)。

`freeze_derived`:

```json
{"sources": []}
```

未実装のFeature type(`create_work_plane` `project_wire` `create_guide_surface` `extrude`
`create_part_from_wire_cage` `boolean` `create_fabrication_model` `create_pattern`)は
`definition` を空objectとして書く。空でないものを読んだ場合はKCD2-D100の警告を付けて保持し、
値は捨てない。実装したWPでこの節を更新する。

`create_fabrication_model` の `definition` は実装済みで、`parts` `materialThickness`
`targetMaxDeviation` `fidelity` `method` `splitAxis` `automaticBoundaries` `maximumPartCount`
`minimumPartWidthMm` `manualBoundaries` `openingWires` `foldWires` `connectionWires`
`masterPercent` `creaseProgress` `bandProgress` に加えて、2026-09-11 から面の範囲
`rangeUMin` `rangeUMax` `rangeVMin` `rangeVMax`(V1 の plate_range。u は列方向、v は行方向の
0〜1)を持つ。無ければ 0〜1 全体として読む(古い文書はそのまま開ける)。
材料と積層は Feature ではなく Entity の `manufacturing`(`materialName` `layerCount` など)に持つ
(V1 の plate の材料と plate_laminate に当たる)。

`create_guide_surface` の `definition` も実装済みで、`method`(GuideSurfaceMethod の番号: 0 平面、
1 ルールド、2 ロフト、3 案内付きロフト、4 曲線網、5 境界埋め、6 離した面、7 回転体)、`chains`、
`roles`、`offsetDistanceMm` に加えて、2026-09-11 から回転体の `revolveAxisPoint`
`revolveAxisDirection` `revolveAngleRad`(V1 の回転面。断面 1 本を軸のまわりに回す)を持つ。
無ければ既定(回転体でなければ使わない)。

### 19.7 §8.2 Segmentの現状

`CurveSegment` は一様3次B-splineだけを持つため、`knots` `multiplicities` `weights` は
まだ書かない。`cubic_b_spline` は `degree`(常に3)、`controlPoints`、`periodic`(常にfalse)を持つ。
非一様・有理B-splineをWP-06で扱えるようにした時点で、この節と§8.2を同時に更新する。
`provenance` も同様に未実装であり、書かない。

読込は「近い形へ黙って直さない」。半径0の円弧、掃引角0、長さ0の直線、制御点不足のBezier、
零ベクトルの法線は、どれも折れ線などへ代替せずKCD2-D002で拒否する。

### 19.8 未知entryの扱い

`document.json` 以外のentryは、この版が意味を知らないものも含めて読み込み時に保持し、
保存し直したときにそのまま書き戻す。新しい版が付けたサムネイルや追加データを、
古い版で開いて保存しただけで失わないため。
