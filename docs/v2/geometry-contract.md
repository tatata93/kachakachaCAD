# Wire-first V2 幾何契約

## 1. 座標、単位、許容差

- 右手系を使う。
- 文書内部の長さはmm、角度はradとする。
- UI表示の角度はdegとする。
- ベクトル、座標、半径、長さは有限なdoubleだけを許可する。
- ゼロ除算、NaN、Infinityを結果へ残してはならない。

許容差は1か所の `GeometryTolerance` から供給し、機能ごとのハードコードを禁止する。

```text
numericEpsilon       = 1e-12
modelLinearMm        = clamp(max(1e-6, modelDiagonalMm * 1e-9), 1e-6, 1e-3)
modelAngularRad      = 1e-9
interactiveJoinMm    = 0.01 既定、UIで0.001から0.1まで変更可
displayPickPx        = 8
candidateMenuPx      = 14
```

- `modelLinearMm` はB-Rep生成と厳密接続判定に使う。
- `interactiveJoinMm` は接続候補を見つけるためだけに使う。
- 候補距離内だからという理由で、作図済み座標を無言で移動してはならない。
- 明示的な `端点を結合` Commandだけが端点座標を一致させる。

## 2. 曲線Segment

### 2.1 共通契約

全Segmentは次を実装する。

```cpp
Point3 Evaluate(double t);          // t in [0,1]
Vector3 FirstDerivative(double t);
Vector3 SecondDerivative(double t);
double ArcLength(double t0, double t1, double tolerance);
Bounds3 Bounds(double tolerance);
SplitResult Split(double t);
ClosestPointResult ClosestPoint(Point3 point);
```

- 範囲外tは暗黙clampせず、呼出契約に応じてエラーまたは明示clampする。
- 画面描画用折線を正本へ戻してはならない。
- トリム、延長、投影後も可能な限り元の曲線種類を保持する。
- B-splineはdegree、control points、knots、multiplicities、weightsの有無を保持する。
- 円弧は中心、法線、基準方向、半径、開始角、掃引角を保持する。

### 2.2 必須種類

- Line
- CircularArc
- Circle
- CubicBezier
- CubicBSpline

Ellipse、NURBS重み編集、周期B-splineは初回切替の必須ではない。読み込めない種類を近似Polylineへ
黙って変換してはならない。

## 3. WireEntityとWireChainRef

### 3.1 WireEntity

1つのWireEntityは1つ以上の順序付きSegmentを持つ。隣接Segmentの終点と始点は
`modelLinearMm` 内で一致しなければならない。閉Wireは最後と最初も一致する。
Circleは単独Segmentで閉Wireになる。

### 3.2 複数ワイヤーから論理鎖を作る

Feature入力では複数のWireEntityまたは一部Segmentを `WireChainRef` として組み合わせられる。

接続解析は次の順で決定論的に行う。

1. 対象端点を `EntityId, SegmentId, endpointIndex` で安定ソートする。
2. `modelLinearMm` 内の端点だけを同一ノードにまとめる。
3. 連結成分を求める。
4. open chainは次数1のノードが2つ、他が次数2であることを必須とする。
5. closed chainは全ノードの次数が2であることを必須とする。
6. 次数3以上があれば自動順序を決めず、分岐候補をUIへ返す。
7. 開始端は最小ID、closed chainの向きは最小IDの次候補で決定する。
8. Featureへ保存する時点で、決定したSegment順と向きを明示保存する。

`interactiveJoinMm` 内だが `modelLinearMm` 外の端点は「ほぼ接続」としてプレビューするが、形状生成を
実行しない。利用者へ `端点を結合`、`許容差を変更`、`別の線を選ぶ` を提示する。

### 3.3 両端接続ケース

次を必須対応とする。

- 2本の外形ガイドが開始側と終了側の両方で接続し、全体として閉輪郭になる。
- 複数の断面がそれぞれ2本の外形ガイドへ接する。
- 断面自体が複数Segmentである。
- 外形ガイド自体が複数Segmentである。
- 断面が閉Wireであり、ガイドなしで複数の閉断面をロフトする。
- ガイドの一方または両方が曲線と直線の混成鎖である。

接続点は任意のSegment内部でもよい。内部交差の場合はそのparameterで論理分割し、元Wireを変更せず
Feature入力側のSegmentRef範囲として保存する。

### 3.4 診断

- `GEO-W001 DisconnectedChain`: 1つの役割へ入れた線が複数成分。
- `GEO-W002 BranchedChain`: 自動順序を決められない分岐。
- `GEO-W003 EndpointGap`: 候補距離内だが厳密接続していない。
- `GEO-W004 SelfIntersection`: 用途上許されない自己交差。
- `GEO-W005 DegenerateSegment`: 長さまたは半径が許容差以下。
- `GEO-W006 DuplicateSegment`: 同じSegment範囲を同じ役割へ重複登録。

## 4. 作図点、交差、スナップ用幾何

- 線と線、線と円/円弧、円/円弧同士、線とBezier/B-spline、曲線同士の交差候補を3Dで求める。
- 作業平面モードでは平面へ投影した2D交差と、実際の3D交差を区別して返す。
- 3D交差は2曲線上の最近点距離が `modelLinearMm` 以下のときだけ交点とする。
- 見かけ上の交差は `画面交差` と表示し、既定スナップ候補にしない。
- Segment内部点、端点、中心、中点、四半点、接点、垂足を候補型として返す。
- 候補選択の画面優先順位は `ui-workflows.md` に従う。

## 5. ワイヤー編集

### 5.1 トリム

- 対象Segment上のクリックparameterと、境界との実3D交差parameterを使う。
- 交差が複数ある場合は削除される区間をプレビューし、クリック位置を含む区間を削除する。
- 元曲線を保持できる厳密splitを使う。
- closed Circleは2交点以上でCircularArcへ変換できる。
- closed複合Wireは、切断点を2つ指定してからトリムする。

### 5.2 延長

- Lineは同一直線上へ延長する。
- CircularArcは同じ中心・半径・平面を維持し、掃引角を増やす。
- CubicBezierは端の導関数を保つ多項式延長を使う。
- CubicBSplineは端区間を厳密延長し、B-splineとして保持する。
- Circleとclosed Wireは延長不可とする。
- 境界まで延長する場合は最初に到達する実3D交点を使う。

### 5.3 接続

- 端点一致はどちらを固定するか、または中点へ寄せるかを選ぶ。
- G1は接線方向を一致させる。
- G2は接線方向と曲率ベクトルを一致させる。
- 解がない場合は候補Documentを破棄し、どの拘束が成立しないかを表示する。
- 依存連鎖は上流から反復解決し、上限回数と収束値をDiagnosticへ残す。

## 6. 形状ガイド

### 6.1 種類

`CreateGuideSurface` は次のmethodを持つ。

| method | 必須入力 | 用途 |
| --- | --- | --- |
| PlanarBoundary | 1つ以上の閉輪郭 | 平面の外形と穴 |
| RuledSections | open/closed断面2群 | 2断面を直線母線で接続 |
| LoftSections | open/closed断面3群以上 | 多断面を滑らかに補間 |
| GuidedLoft | 外形ガイド2群+断面1群以上 | 両側外形を保持したロフト |
| GordonNetwork | U方向2群以上+V方向2群以上 | 交差する曲線網を通る面 |
| BoundaryFill | 閉じた3辺または4辺 | 非平面境界のCoons/Fill |
| OffsetGuide | 既存形状ガイド+距離 | 参照用オフセット |

### 6.2 PlanarBoundary

- 全輪郭が同一平面へ `modelLinearMm` 内で載ること。
- 外周と穴は向きではなく包含関係で分類する。
- 外周同士が重なる、穴が外周へ接する、自己交差する場合は拒否する。
- 複数の非接触外周を選んだ場合は複数のGuideSurface出力をプレビューする。

### 6.3 RuledSections

- 2断面はともにopen、またはともにclosedでなければならない。
- openの場合は端点対応を自動評価し、ねじれが少ない向きを既定にする。
- closedの場合は明示または自動の対応基準点を持つ。
- Segment数が異なっても弧長parameterで対応させるが、元曲線をPolylineへ変換しない。
- 断面間距離が全域で許容差以下なら退化として拒否する。

### 6.4 LoftSections

- 断面順は利用者が表で変更できる。
- 自動順序は断面重心を主成分軸へ投影した値で決め、同値ならID順とする。
- closed断面は対応基準点を保存する。
- 生成後に断面を通る偏差を測り、最大偏差が `modelLinearMm * 10` を超えたら失敗とする。
- 隣接断面で法線が反転または自己交差する場合は警告ではなく失敗とする。

### 6.5 GuidedLoft

- 2つの外形ガイドは両端で直接接続してもよい。
- 各open断面の両端はガイド上の点へ接続すること。
- 断面端がガイドSegment内部に接する場合も扱う。
- 断面とガイドの交差順が両ガイドで一致しなければならない。
- 断面が端部にない場合、自動仮想断面を作るかどうかを設定で選ぶ。既定は作る。
- 仮想断面はDerivedとして表示でき、生成理由と偏差を示す。
- ガイド、断面のいずれかを無視して近似面を作ってはならない。

### 6.6 GordonNetwork

- 全U鎖と全V鎖は、相互の各組で1回交差すること。
- 交差順はすべての鎖で単調でなければならない。
- 欠損交差、2重交差、順序逆転を距離と対象ID付きで拒否する。
- 生成面は全入力曲線を `modelLinearMm * 10` 以内で通ることを検査する。

### 6.7 BoundaryFill

- 辺数は3または4とする。
- 境界が非平面で面が一意でないことをUIで明示する。
- 連続条件は辺ごとにG0またはG1を選べる。既定はG0。
- 5辺以上は勝手に三角分割せず、分割案またはGordonNetworkを提示する。

### 6.8 診断

- `GEO-G001 NonPlanarBoundary`
- `GEO-G002 MixedOpenClosedSections`
- `GEO-G003 SectionOrderInvalid`
- `GEO-G004 GuideSectionNotConnected`
- `GEO-G005 NetworkIntersectionMissing`
- `GEO-G006 NetworkIntersectionOrderInvalid`
- `GEO-G007 SurfaceSelfIntersection`
- `GEO-G008 SurfaceFitExceeded`

## 7. 閉じたワイヤー群から部品を作る

### 7.1 入力範囲

利用者は次のいずれかを選ぶ。

- 明示選択したワイヤーだけ。
- 選択グループ内のワイヤー。
- 画面で囲んだ範囲内のワイヤー。
- 「閉じた立体輪郭を探す」で候補になった1連結成分。

文書全体から無関係な線を自動採用してはならない。

### 7.2 パッチ候補

閉シェルを構成する内部パッチは次の由来だけを許す。

1. 同一平面の閉WireChainから作る平面パッチ。
2. 形状ガイドを境界WireChainでtrimしたパッチ。
3. Extrude、Loft、GuidedLoft等のFeatureが意味的に生成した側面パッチ。
4. 利用者がBoundaryFillとして明示したパッチ。

任意の非平面閉輪郭へ理由のないFillを自動適用してはならない。

### 7.3 自動検出

1. 選択ワイヤーを端点と交差点で論理分割する。
2. グラフから重複しない最小閉路候補を列挙する。
3. 各閉路へ上記パッチ由来を割り当てる。
4. 各辺がちょうど2パッチから使われる組合せだけを閉シェル候補にする。
5. 候補が1つならプレビューする。
6. 候補が複数なら、面数、未解決辺、体積見込を表示して利用者に選ばせる。
7. OCCTでsew、向き統一、solid化、妥当性、連結数、体積を検査する。
8. 検査成功後だけPartEntityをcommitする。

### 7.4 必須検査

- 全境界辺が2回使われる。
- non-manifold edgeがない。
- shellが閉じている。
- B-Rep validatorがvalid。
- 体積が `modelLinearMm^3` より大きい。
- 自己交差がない。
- 連結solidが1つ。

診断:

- `GEO-S001 NoClosedShellCandidate`
- `GEO-S002 AmbiguousShellCandidates`
- `GEO-S003 OpenShell`
- `GEO-S004 NonManifoldShell`
- `GEO-S005 InvalidSolid`
- `GEO-S006 ZeroVolume`
- `GEO-S007 MultipleSolids`。これは自動分割プレビューを伴う情報で、利用者が続行すれば複数部品を作る。
- `GEO-S008 KernelFailure`

## 8. 押し出し

### 8.1 入力

- 閉輪郭は同一平面にあること。
- 複数輪郭は包含関係から外周と穴へ分類する。
- 非接触の複数外周は、それぞれ独立する出力候補とする。
- open WireChainは輪郭ワイヤー出力だけに使える。
- GuideSurface入力は法線オフセットの輪郭ワイヤー出力だけに使える。初回切替では厚み付きsolid offsetを行わない。

### 8.2 方向

```text
ProfileNormal
WorkPlaneNormal
WorldX | WorldY | WorldZ
SelectedVector
CustomXYZ
```

CustomXYZは正規化前の入力値を表示に保持し、長さが `numericEpsilon` 以下なら拒否する。
方向反転ボタンを持つ。

### 8.3 終端

```text
Distance             片側距離
SymmetricDistance    中央から両側同距離
TwoDistances         正負側を別指定
ToTarget             指定Subshapeまで
ThroughAll           BooleanCut専用
```

- 距離は数式入力を受ける。
- 0距離は部品出力では拒否し、同位置の輪郭ワイヤー出力なら確認後に許可する。
- ToTargetは全profile点から方向レイが同じ対象領域へ到達できることを必須とする。
- 到達しない点がある場合は最小/最大不足距離と位置を返し、部分結果を作らない。
- 曲面までの押し出しはOCCTの厳密trimを使い、平均距離の一定押し出しへ置換しない。

### 8.4 出力

UIは次の独立チェックを持つ。

```text
[ ] 押し出し先の輪郭ワイヤー
[ ] 側面の境界ワイヤー
[ ] 部品
```

最低1つを選ぶ。部品を選んだ場合は操作を選ぶ。

```text
新規部品
選択部品へ足す
選択部品から引く
```

- ワイヤーと部品を同時選択した場合、同じ評価結果の意味的境界から両方を作る。
- 別々に近似計算して位置差を発生させてはならない。
- 足す結果が非連結なら拒否する。
- 引く結果が複数solidなら、元部品を置換する前に分割個数と名前をプレビューする。
- 演算対象は1つの明示選択Partだけとし、近いPartを自動選択しない。

診断:

- `EXT-001 ProfileNotPlanar`
- `EXT-002 ProfileOpenForSolid`
- `EXT-003 TargetNotReached`
- `EXT-004 BooleanNoIntersection`
- `EXT-005 BooleanWouldBeDisconnected`
- `EXT-006 ZeroDistance`
- `EXT-007 InvalidDirection`
- `EXT-008 ResultInvalid`

## 9. 投影

- 投影元WireChain、投影方向、対象GuideSurfaceまたはPart SubshapeをFeature入力として保持する。
- 交点が複数ある場合は `NearestAlongPositiveDirection`、`NearestEitherDirection`、`AllHits` を選ぶ。
- 既定は正方向の最初の交点。
- 交点がない区間を勝手に端へ吸着しない。
- 曲線種類を対象面上のp-curveと3D curveとして保持できる場合は保持する。
- 表示用Polylineだけしか得られない場合は最大偏差を報告し、明示許可がない限り正本にしない。
- 開口に使う閉投影Wireは対象面上で閉じていることを検査する。

## 10. 測定

### 10.1 2点

```text
distance3d
deltaX, deltaY, deltaZ
distanceXY, distanceYZ, distanceZX
angleToX, angleToY, angleToZ
```

符号付きdeltaと絶対投影距離を区別して表示する。距離値はモデル座標から計算し、画面投影を使わない。

### 10.2 3点

指定順は `始点 - 頂点 - 終点`。0から180degの内角と、方向付き角度に使った法線を表示する。
辺長が許容差以下なら拒否する。

### 10.3 要素

- Line: 接線。固有の曲率法線なし。
- Circle/Arc: 接線、中心向き曲率法線、半径、弧長。
- Bezier/B-spline: 指定parameterの接線、二階微分から求めた曲率法線、曲率半径。
- 直線と曲線: 交点または指定最近点での接線角度と、曲線法線に対する角度。
- Part face: 指定点の法線、主曲率、Gaussian curvature。
- GuideSurface: 指定UVの法線、主曲率、Gaussian curvature、developability指標。

`MEA-001 UndefinedNormal`、`MEA-002 DegenerateSelection`、`MEA-003 NoCommonPoint` を定義する。

## 11. 数値入力

数式評価は既存 `NumericExpression` の試験済み実装を移植する。

必須演算:

```text
+ - * / ^ ( )
単項+ -
pi
deg(x)
rad(x)
```

- localeにかかわらず保存は小数点 `.`。
- UI入力は全角数字と全角演算子を半角へ正規化してよい。
- 単位suffixは `mm`、`cm`、`m`、`deg`、`rad` を扱う。
- 長さ欄に角度、角度欄に長さを入れたら拒否する。
- 計算式と評価値の両方をFeatureへ保存し、式を再編集できる。
- 0除算、構文エラー、範囲外はcommitしない。
