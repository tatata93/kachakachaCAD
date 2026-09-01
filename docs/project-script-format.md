# KCDプロジェクトスクリプト

## 目的

`.kcd` は、Qt UIで作成した作業平面、ワイヤー、面、板材とその依存関係を保存するプロジェクト記述である。

ユーザーはテキストで作業平面やワイヤーを定義し、ビューアで確認できる。

新しく保存したファイルは先頭に `format_version 1` を持つ。バージョン記載のない従来ファイルも互換読込し、未対応の新しいバージョンは内容を誤解釈せず読み込みエラーにする。

## 単位と座標

単位は mm。

```text
X: 車体長手方向
Y: 車体幅・奥行き方向
Z: 高さ方向
```

## コメント

`#` 以降はコメント。

## コマンド

### 3点から作業平面

```text
plane_three NAME ax ay az bx by bz cx cy cz
```

### 点＋法線から作業平面

```text
plane_point_normal NAME ox oy oz nx ny nz ux uy uz
```

### オフセット平面

```text
plane_offset NAME SOURCE distance
```

### 回転平面

```text
plane_rotate NAME SOURCE ax ay az dx dy dz angleDegrees
```

`SOURCE` 平面を、点 `(ax, ay, az)` と方向 `(dx, dy, dz)` の軸まわりに回転する。

### 作図点

```text
point3d NAME x y z SOURCE_PLANE_OR_-
```

線の交点や任意位置に置く、作図・スナップ用の点。作成元の作業平面がある場合は名前を、自由3D点なら `-` を指定する。

### 3D直線

```text
line3d NAME sx sy sz ex ey ez
```

### 3Dポリライン

```text
polyline3d NAME x1 y1 z1 x2 y2 z2 [x3 y3 z3 ...]
```

最低2点が必要。

### 3D Cubic Bezier

```text
bezier3d NAME sx sy sz c1x c1y c1z c2x c2y c2z ex ey ez
```

### 3D Cubic B-spline

```text
bspline3d NAME x1 y1 z1 x2 y2 z2 x3 y3 z3 x4 y4 z4 [x5 y5 z5 ...]
bspline3d_knots NAME CONTROL_COUNT x1 y1 z1 ... xN yN zN k1 k2 ... kN+4
```

4点以上の制御点を持つ、端点固定の3次B-splineを作る。`bspline3d` は一様な内部ノットを自動生成する旧来の簡易形式で、引き続き読み込める。保存時は、トリム・延長後の曲線形状も厳密に保てる `bspline3d_knots` を使い、制御点数に続けて制御点と `CONTROL_COUNT + 4` 個の非減少ノットを書く。画面の「スプライン」は指定した通過点を補間してから、この制御点・ノット形式で保存する。

### 3D円

```text
circle3d NAME cx cy cz ux uy uz vx vy vz radius
```

`(ux, uy, uz)` と `(vx, vy, vz)` は円を置く平面の方向を指定する。

### 3D円弧

```text
arc3d NAME cx cy cz ux uy uz vx vy vz radius startDegrees sweepDegrees
```

角度は度数法。`startDegrees` は開始角、`sweepDegrees` はそこから進む角度。

### 直接3Dワイヤーの平面メタ情報

```text
wire_meta NAME PLANE|none|- free|reference|locked
```

`line3d`、`polyline3d`、`bezier3d`、`bspline3d`、`bspline3d_knots`、`circle3d`、`arc3d` で作ったワイヤーに、作成元平面や平面ポリシーを後から付ける。`none` または `-` を指定すると、作成元平面なしのままポリシーだけを記録する。

### 直線の寸法拘束

```text
wire_constraint NAME LENGTH|- ANGLE_DEGREES|-
```

直線の始点を固定端として、長さと作業平面内の角度を保持する。`-` はその項目を拘束しない。角度は作成元平面のU方向を0度、V方向を90度とするため、角度を指定する場合は先に `wire_meta` で作成元平面を設定する。長さだけなら自由な3D直線にも使用できる。

### 円・円弧の半径拘束

```text
wire_radius_constraint NAME RADIUS
```

円または円弧の中心や向き、円弧の開始角・中心角を編集しても、指定した半径を保持する。

### 補助線

```text
wire_role NAME construction|model
```

`construction` はスナップや寸法基準に使う補助線で、面・投影・開口・切断出力の形状には使われない。省略時は `model`。

### 端点の一致拘束

```text
wire_coincident ANCHOR_WIRE start|end FOLLOWER_WIRE start|end
```

固定側ワイヤーの端点へ追従側ワイヤーの端点を一致させ、編集後も関係を保持する。閉じたワイヤーと投影ワイヤーは対象外。現在は競合を避けるため、追従側の直線へ長さ・角度拘束を同時設定できない。

### 端点の接線拘束

```text
wire_tangent ANCHOR_WIRE start|end FOLLOWER_CURVE start|end
```

同じ組み合わせの `wire_coincident` に続けて指定する。固定側の端点方向が変化すると、追従側ベジェまたはB-splineの端点制御点、追従側円弧の向きを更新してG1接続を維持する。曲線の端点ハンドル長、円弧の半径と中心角は保持する。

### 端点の曲率拘束

```text
wire_curvature ANCHOR_WIRE start|end FOLLOWER_BEZIER start|end
```

同じ組み合わせの `wire_coincident` に続けて指定する。追従側は3次ベジェ曲線とし、接続端に近い2制御点を再計算して位置、接線、曲率が連続するG2接続を維持する。追従側の反対側端点は動かさない。作業平面に固定された追従曲線が平面外へ曲がる関係は作成しない。

### 作業平面上の直線

```text
sketch_line NAME PLANE su sv eu ev [free|reference|locked]
```

### 作業平面上の円

```text
sketch_circle NAME PLANE cu cv radius [free|reference|locked]
```

### 作業平面上の円弧

```text
sketch_arc NAME PLANE cu cv radius startDegrees sweepDegrees [free|reference|locked]
```

### 作業平面上の Cubic Bezier

```text
sketch_bezier NAME PLANE su sv c1u c1v c2u c2v eu ev [free|reference|locked]
```

省略時は `reference`。作業平面を編集基準として記録するが、3Dワイヤー自体を平面へ閉じ込めない。

```text
free       作成元平面は記録するが、3D自由ワイヤーとして扱う
reference  作成元平面を2D編集の基準として使う
locked     作成元平面上に固定する
```

### 閉じた輪郭から平面

```text
surface_planar NAME BOUNDARY_WIRE...
```

すでに閉じたワイヤー1本、または端点同士が閉じる複数の直線・円弧・ベジェ・B-splineを指定できる。
複数指定時は順不同でも端点から自動整列し、必要なワイヤーの向きを反転する。元ワイヤーは独立した編集対象のまま残り、変更時に面を再計算する。

### 2断面からルールド面

```text
surface_ruled NAME SECTION_WIRE_A SECTION_WIRE_B
```

断面ワイヤーの向きが逆の場合は、端点の対応が短くなる向きへ自動でそろえる。
片方には `wire_project` で作った投影ワイヤーも指定できる。ライト最前面の元輪郭と、車体面へ投影した根元輪郭を指定すると、飛び出すケース側面になる。面と投影ワイヤーは依存順に保存・再計算される。

### 3断面以上からロフト面

```text
surface_loft NAME SECTION_WIRE_A SECTION_WIRE_B SECTION_WIRE_C [...]
```

断面は車体の前から後ろの順に並べる。3断面以上では各断面を通り、断面位置で長手方向の接線が連続する三次補間を使う。断面ワイヤーを編集すると面を再計算する。

### 断面群とガイド線からゴードン面

```text
surface_gordon NAME SECTION_COUNT SECTION_WIRE... GUIDE_COUNT GUIDE_WIRE...
```

断面ワイヤー(2本以上)と外形ガイド線(1本以上)を両方とも厳密に通る面。`SECTION_COUNT`・`GUIDE_COUNT` はそれぞれの直後に続く名前の個数を示す。各ガイドは全ての断面と交差していなければならず、交差しないガイドを指定するとコアが拒否する。断面・ガイドの名前は互いに重複できず、投影ワイヤー・plate-offsetワイヤー・constructionワイヤーは断面にもガイドにも使用できない。断面またはガイドのワイヤーを編集すると面を再計算する。

### 平面図ワイヤーを面へ投影

```text
wire_project NAME SOURCE_WIRE TARGET_SURFACE dx dy dz
```

投影元の平面図、対象面、投影方向を関係として保持する。断面または元の平面図を編集すると、面と投影ワイヤーを再計算する。

### 面から板材

```text
plate NAME SOURCE_SURFACE thickness positive|centered|negative MATERIAL
```

`thickness` はmm。厚み方向は元の面から外側、面を中央、元の面から内側のいずれか。`MATERIAL` は `styrene`、`paper`、`brass` などの製作材料名を保持する。

### 板材へ開口を追加

```text
plate_opening PLATE PROJECTED_CLOSED_WIRE
```

板材の元の面へ投影した閉じたワイヤーを、板材の開口境界として関連付ける。元の平面図や断面を編集すると投影ワイヤーと開口が追従する。

### 板材の展開切れ目

```text
plate_relief_cut PLATE PROJECTED_WIRE
```

板材の元の面へ投影したワイヤーを、展開図で途中まで切る線として関連付ける。開いた線を使用できる。元の平面図や断面を編集すると切れ目も追従する。

### 展開片の分割線

展開図を複数の紙片へ完全に分ける投影ワイヤーは次で指定する。途中で止まる切れ目とは別の関係になる。

```text
plate_split_line PLATE PROJECTED_WIRE
```

投影ワイヤーが展開用の折りメッシュを横切る位置で接続を切り、別々の閉じた外周を持つ紙片にする。元の3D板を分割する命令ではない。

### 面から成形治具Body

```text
body_surface_jig NAME SOURCE_SURFACE positive|negative CLEARANCE THICKNESS minU maxU minV maxV
```

元面の指定範囲から、雄型または雌型側へ逃げ量 `CLEARANCE` を取り、指定肉厚の閉じた治具Bodyを定義する。Bodyはメッシュを保存せず、元断面と面の編集から再生成する。

### 板材が使う曲面範囲

```text
plate_range PLATE minimumU maximumU minimumV maximumV
```

曲面板材を手動分割した各部品が、元の面のどの範囲を使うかを0から1の正規化座標で保持する。`U` は断面内、`V` は断面間・車体長手方向。通常の未分割板材は `0 1 0 1` であり、このコマンドを省略する。

### 参照寸法

```text
reference_dimension NAME KIND FIRST_REFERENCE SECOND_REFERENCE
```

`KIND` は次のいずれか。

```text
point_distance wire_length wire_radius wire_distance wire_angle
point_wire_distance point_plane_distance wire_plane_angle plane_angle plane_distance
```

参照対象は `point x y z`、`wire WIRE_NAME PARAMETER`、`plane PLANE_NAME`、未使用位置の `none` で表す。`PARAMETER` はワイヤーの始点0から終点1までの位置で、円では0から1周する位置になる。例:

```text
reference_dimension lamp_radius wire_radius wire lamp_circle 0.25 none
reference_dimension window_gap wire_distance wire window_top 0.5 wire window_bottom 0.5
reference_dimension floor_gap point_plane_distance point 12 4 7 plane floor
```

参照寸法は形状を動かさず、参照元を編集したときに表示値を再計算する。

### 表示・非表示

```text
visibility workplane|point|wire|surface|plate|body|dimension NAME shown|hidden
```

非表示の作図用断面や平面図もプロジェクトから削除せず、依存関係を保ったままモデル一覧へ残す。

## 実行

```powershell
.\scripts\run-viewer.ps1 -Project examples\first-check.kcd
```

画像出力だけなら:

```powershell
.\scripts\snapshot-viewer.ps1 -Project examples\first-check.kcd
```

ビューアで編集した内容は `Ctrl+S` で `out` フォルダへ保存できる。コマンドだけで書き出す場合:

```powershell
.\scripts\export-project.ps1 -Project examples\first-check.kcd -OutputPath out\first-check-export.kcd
```
