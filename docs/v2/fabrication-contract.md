# Wire-first V2 製作近似・型紙契約

## 1. 目的

製作近似は、完成品の自由曲面を「紙またはプラ板として実際に切り、曲げ、接着できる少数の大きな部材」へ
変換する。見た目だけ似た三角形群を作る機能ではない。

```text
完成品Part
  -> 製作条件付き近似
  -> FabricationModel
  -> Pattern
  -> 組立確認
  -> 任意状態Part / STL / STEP / SVG / DXF / PDF
```

## 2. 正本と出力

- `FAB-001`: 元の完成品Partを変更しない。
- `FAB-002`: FabricationModelは別Entityとして作る。
- `FAB-003`: FabricationModelは元Part、選択範囲、手動境界、開口、設定をFeature入力として保持する。
- `FAB-004`: 自動生成パネル、折り線、切れ目、対応辺はDerivedであり直接編集しない。
- `FAB-005`: 利用者が `現在状態を固定` した場合だけ、独立Wire/Partへ複製する。
- `FAB-006`: 元Part更新後に再計算できない場合、直前の結果をStale表示し、元Partや直前結果を壊さない。
- `FAB-007`: すべての組立状態で、部材境界、折り線、切れ目、開口を曲線種類付き3D Wireとして取得できる。
- `FAB-008`: 同時にWireとPartを固定するとき、Part境界は出力Wireと `modelLinearMm` 内で一致し、
  Wire用とPart用に別の近似計算を行わない。

## 3. FabricationSettings

```text
strategy:
  OnePiece | FewPieces | SeparatePanels | Hybrid

fidelityLevel:
  integer 1..10

explicitMaxDeviationMm:
  null または positive double

panelCountLimit:
  integer 1..200

minimumPanelWidthMm:
  positive double

preferredBendDirection:
  Auto | U | V | Both

panelTypesAllowed:
  Planar
  Cylindrical
  Conical
  TangentDevelopable

reliefCutsEnabled:
  bool

reliefDirection:
  Auto | U | V | Both

reliefShape:
  Auto | StraightSlit | VNotch | CurvedVNotch

maximumReliefDepthRatio:
  0..0.95

minimumLigamentMm:
  positive double

preserveOpenings:
  true

outputThicknessMm:
  positive double

material:
  Paper | Styrene | Brass | Other
```

- `preserveOpenings` は常にtrueであり、UIで無効化してはならない。
- `outputThicknessMm` の初期表示値は0.20mmとするが、プロジェクト設定として明示表示し保存する。
- 元Partに基準板厚があればその値を初期値にする。
- 材料ごとの塑性、K-factor、スプリングバックは初回切替では計算しない。その旨を出力注記へ含める。

### 3.1 再現度の数値化

モデル対角長をDとする。明示最大偏差がない場合、fidelityLevelを0から1の `q=(level-1)/9` に変換し、
対数補間する。

```text
coarse = max(0.50 mm, D * 0.010)
fine   = max(0.03 mm, D * 0.0005)
targetMaxDeviation = exp(lerp(log(coarse), log(fine), q))
```

- スライダー横に目標最大偏差mmを常時表示する。
- 実際の最大偏差とRMS偏差を生成後に表示する。
- panelCountLimit内で目標偏差を達成できない場合は、目標超過を警告し、勝手に部材数上限を超えない。
- 明示偏差が指定された場合はスライダー換算値より優先する。

## 4. パネルの幾何クラス

1つのFabricationPanelは、切り出した平面形状から伸縮なしで作れる次のいずれかとする。

| 種類 | 条件 | 展開 |
| --- | --- | --- |
| Planar | 両主曲率が許容差内で0 | 剛体平面変換 |
| Cylindrical | 一方の主曲率がほぼ0、もう一方が有限 | 平行母線で厳密展開 |
| Conical | 母線が1点へ収束するdevelopable面 | 扇形として厳密展開 |
| TangentDevelopable | 空間曲線の接線面で局所退化を避けられる | 母線積分で展開 |

- Gaussian curvatureが許容値を超える領域を、そのまま1枚のdevelopable panelとして登録してはならない。
- 二重曲率領域は境界変更、切れ目、追加分割のいずれかで吸収する。
- 表示用三角形は許すが、パネルの編集単位、境界、型紙の正本にしてはならない。
- 型紙の外周と開口は可能な限りLine/Arc/Bezier/B-splineとして保持する。

## 5. パネル優先生成

### 5.1 入力解析

1. 元Partの選択SubshapeまたはGuideSurfaceを連続領域へ分ける。表示meshではなくB-Rep providerを使う。
2. 主曲率、Gaussian curvature、曲率勾配、境界、開口、既存稜線、利用者の手動線を標本化する。
3. 大きな平面領域、一方向曲率領域、緩い二重曲率、強い二重曲率を分類する。
4. 候補のbend directionを主曲率方向、部品長手方向、手動指定から作る。
5. 開口を消す、またぐ位置だけを理由に微小パネルへ分ける候補は除外する。

曲率標本は目標最大偏差から適応細分し、標本化の測定偏差を目標最大偏差の1/5以下にする。
達成できない場合は近似計算へ進まず、source patchと測定偏差をDiagnosticへ出す。

### 5.2 候補評価

候補ごとに次を測る。

```text
maxDeviationMm
rmsDeviationMm
panelCount
totalSeamLengthMm
reliefCutCount
totalReliefLengthMm
minimumPanelWidthMm
openingDistortionMm
assemblyClosureGapMm
```

選択目的は辞書式とする。

1. 必須制約違反を0にする。
2. targetMaxDeviation以下にする。
3. panelCountを少なくする。
4. totalSeamLengthを短くする。
5. reliefCutCountとtotalReliefLengthを少なくする。
6. 細長すぎる部材を避ける。

重み付き和だけで精度違反と部材数を交換してはならない。

### 5.3 分割の優先順位

1. 元から存在する部品境界。
2. 利用者指定の分割線。
3. 曲率の谷、接線不連続、材料が変わる位置。
4. 開口のない直線または緩い曲線位置。
5. relief cutで吸収できない強二重曲率。
6. 最後の手段として追加パネル。

三角形の全面分割は生成失敗であり、完成結果として提示してはならない。

### 5.4 戦略

- `OnePiece`: 連結した1枚を優先し、必要箇所に折り線とrelief cutを使う。トポロジー上不可能なら理由を表示する。
- `FewPieces`: 目標偏差内で部品数最小を優先する。既定。
- `SeparatePanels`: 各developable panelを別部品にし、接着対応辺を付ける。
- `Hybrid`: 大きな連続パネルを維持し、強二重曲率部だけ別部品またはrelief cutにする。

## 6. 手動境界と役割

利用者は既存Wireまたは新規投影Wireへ次の役割を付けられる。

```text
PanelBoundary      完全分割
FoldLine           つながったまま折る
ReliefCut          途中まで切る
Opening            貫通穴
KeepTogether       この線をまたいで同じ部材を優先
NoCutZone          この範囲へ切れ目を入れない
BendDirection      曲げ母線の向きを指定
```

- 役割変更は元Wireを変更せず、FabricationFeature内の参照として保存する。
- 同じSegment範囲へ矛盾する役割を付けた場合はcommitしない。
- 手動線は自動再生成より優先する。
- 元形状更新で投影不能になった手動線を最寄り位置へ無言移動してはならない。
- 壊れた参照を赤表示し、再指定、無効化、削除を選ばせる。

## 7. Relief cut

### 7.1 StraightSlit

幅0の切断線。切れ目先端は材料がつながる。展開と組立のトポロジーでは切断線の両側を別境界として持つ。
レーザー等で幅が必要な場合は出力設定でkerfを加える。

### 7.2 VNotch

切れ目入口に2側辺を持つ除去領域。次を持つ。

```text
centerPath
leftSide
rightSide
tip
depth
openingAngle
matePair
```

直線Vだけでなく、左右側辺をArc/Bezier/B-splineにできる。

### 7.3 CurvedVNotch

組立後に左右側辺が一致するよう、対象曲面の計量と目標曲げから側辺曲線を求める。

- 左右側辺へ0から1の対応parameterを持つ。
- 対応点間の組立後距離を `mateGap` として測る。
- 片側だけを任意に短縮して合わせてはならない。
- 必要除去幅が0へ収束する位置をtipとする。
- tipで鋭角が工作不能な場合は曲率を持たせてよいが、これは固定Rではなく形状再現から決める。
- 最小工具Rを指定した場合だけ、形状計算後に製造制約としてRを適用する。
- R適用で偏差目標を超える場合は警告する。

### 7.4 向き

`reliefDirection` はパネル局所UVに対して定義する。

- U: U方向へ切れ目を進める。
- V: V方向へ切れ目を進める。
- Both: U/V候補を同時評価する。
- Auto: 曲率と境界から選び、選択理由を表示する。

縦だけに固定してはならない。U/V併用時も切れ目同士の交差と最小ligamentを検査する。

### 7.5 制約

- 深さは局所パネル幅の `maximumReliefDepthRatio` 以下。
- tipから反対境界まで `minimumLigamentMm` 以上残す。
- 開口へ侵入しない。
- 切れ目同士は交差しない。
- 小部品を脱落させる閉ループを作らない。
- mate edgeの組立後最大gapが目標偏差以下。
- 条件を満たせない場合、追加分割候補を提示する。

診断:

- `FAB-C001 ReliefDisabledButRequired`
- `FAB-C002 ReliefTooDeep`
- `FAB-C003 ReliefIntersectsOpening`
- `FAB-C004 ReliefsIntersect`
- `FAB-C005 LigamentTooSmall`
- `FAB-C006 MateGapExceeded`

## 8. 開口

### 8.1 入力

開口は次から取得する。

- 元Partの貫通穴の意味的境界。
- 利用者がOpening役割を付けた閉投影Wire。
- 元FeatureのBooleanCutで作られた穴のprovenance。

### 8.2 パネルをまたぐ開口

- 開口曲線を各パネル領域へclipする。
- パネル境界上の切断点へ同じ `OpeningJunctionId` を付ける。
- 型紙では各部材の実際の切断線として出力する。
- 組立100%で全片を連結した開口境界が元開口へ対応することを検査する。
- ライトや窓を丸い多角形へ置換してはならない。Arc/B-splineを保持するか、明示偏差以内で近似する。
- 開口を消して生成を成功扱いにしてはならない。

診断:

- `FAB-O001 OpeningProjectionFailed`
- `FAB-O002 OpeningNotClosedAfterAssembly`
- `FAB-O003 OpeningApproximationExceeded`
- `FAB-O004 OpeningConflictsWithSeam`

## 9. 型紙

- パネル上の実長と測地距離を保つdevelopable mappingを使う。
- 輪郭を長方形へ均さない。
- 曲線境界は曲線のまま出力する。
- 各部材に安定した部材番号を付ける。
- FoldLineは山折り、谷折り、角度、FoldIdを表示する。
- 接着する切断辺は同じMatePair番号を表示する。
- 開口、relief cut、外周、折り線は別レイヤーにする。
- のりしろは初回切替では作らない。
- 1枚へ結合する場合、パネル隣接グラフから切断辺を選んでspanning treeにし、重なりを検出する。
- 重なりがある場合は別のcut graphを探索し、解がなければ重なり位置を表示して分割を提案する。
- 自動配置で部材を拡大縮小、鏡像反転してはならない。回転と平行移動だけを許す。

## 10. 組立スライダー

### 10.1 基本

- 0%は型紙の平面状態。
- 100%はFabricationModelの完成状態。
- 中間値は頂点座標の線形補間で作ってはならない。
- FoldLineは目標角度へ比例進行させる。
- developable curved panelは母線間の回転または曲率を比例進行させ、面内距離を保つ。
- 接着前のMatePairは近づく様子を表示し、指定接着タイミング以降に一致させる。
- スライダーを離した時に勝手に100%や離散角度へ吸着しない。

### 10.2 しわ防止の幾何条件

各状態tで次を満たす。

```text
edgeLengthChangeRelative <= 1e-6
panelGeodesicSampleChangeRelative <= 1e-5
rigidPanelScaleError <= 1e-9
triangleFlipCount == 0
selfIntersection is reported
```

平面パネルは剛体変換だけで動かす。曲げパネルは展開計量を保つ離散developable frameを積分する。
各頂点を独立補間する実装は禁止する。

### 10.3 閉ループと接着

- 連続パネルのFoldLineはspanning forestとして順に変換する。
- 接着MatePairを含む閉ループは、剛体/ヒンジ拘束の最小二乗で位置合わせする。
- 最適化変数は剛体変換と許可された折り角だけとし、スケールとせん断を禁止する。
- 100%の最大closure gapが目標偏差を超える結果は完成扱いにしない。
- 解けない場合は最も大きい残差のMatePairを表示し、切れ目追加または部品分割を提案する。

### 10.4 個別折り

各Foldは `targetAngleDeg` と `progressPercent` を持つ。マスタースライダー変更時は、
個別overrideがないFoldだけを更新する。個別値を変更した状態も保存する。

## 11. 任意状態の実体化と出力

- `現在状態を固定` は現在のmaster percentと個別Fold値をSnapshotする。
- 固定出力は `ワイヤーのみ / 部品のみ / 両方` から選ぶ。
- ワイヤーは現在姿勢の部材境界、折り線、切れ目、開口を元のLine/Arc/Bezier/B-spline種類で保持する。
- 出力Partは `用途=製作部材`、`sourceFabricationModelId`、状態値を持つ。
- Partは同じ評価bundleの境界Wireとpanel面から作り、別の離散化や近似で境界を作り直さない。
- 厚みは各パネルの法線方向へ与える。厚み方向は外側、中央、内側を選べる。
- 折り線近傍は隣接solidを許容差内で結合し、self-intersectionと最小肉厚を検査する。
- StraightSlit/VNotchは実際の切断境界として3D shapeへ反映する。
- 開口は実穴として反映する。
- 1つの製作部品が非連結になれば複数Partへ分ける。
- STLとSTEPは同じ検査済みB-Repから生成する。
- 「選択部品だけ」はPanelId/FabricationPartIdの明示選択を使い、非表示状態だけで対象を決めない。
- 0%、30%、100%の出力で、対応する辺長とパネル面内寸法が許容差内で同じでなければならない。
- `両方` の場合、出力WireとPart境界は `modelLinearMm` 内で一致しなければならない。
- 100%以外を元の完成品Partへ置換して出力してはならない。

診断:

- `FAB-A001 AssemblyClosureFailed`
- `FAB-A002 AssemblySelfIntersection`
- `FAB-A003 MetricDistortion`
- `FAB-E001 ThicknessRequired`
- `FAB-E002 FoldSolidInvalid`
- `FAB-E003 SelectedPartMissing`

## 12. 元形状への対応

近似後に周辺ワイヤーや接続部を変形する場合、名前や最近点だけで対応させない。

```text
Attachment
- source Entity/Subshape/Segment reference
- source parameter or UV
- target PanelId
- target parameter or local UV
- mapping method
- valid revision
```

- 元Wireが形状ガイド上のUVを持つ場合はUVを正本にする。
- Part Subshape由来は意味的SubshapeKeyと局所parameterを使う。
- 最近点は初回対応候補にだけ使い、確定後はAttachmentを保存する。
- 形状変更で対応先が消えたらBrokenReferenceとし、別の近い部材へ勝手に移さない。
- 接続用派生Wireは元Wireを変えず、近似後の実形状上へ生成する。
- 対応誤差を最大/RMSで表示する。

## 13. ER1/初期ER2 1/87受入モデル

`docs/test-models/er1-er2-round-cab-1-87.md` のモデルをV2 fixtureへ移植する。

必須評価領域:

- 窓下中央の一方向曲率
- 左右コーナーの強い水平曲率
- 6枚窓の開口と回り込み
- 額中央の緩い二重曲率
- 額左右と屋根肩の強い二重曲率
- 通常屋根への曲率遷移
- 上部中央前照灯の開口または別部品接続

期待する近似の性格:

- 腰部は横長の大きな部材。
- 窓帯は可能な限り連続した帯。
- 額は少数の大きな帯。
- 強二重曲率の肩だけrelief cutまたは追加分割。
- 全面三角形化しない。
- 6枚窓と前照灯開口を保持する。
- 30%組立でしわ、縮尺変化、辺長変化を起こさない。
