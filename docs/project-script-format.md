# KCDプロジェクトスクリプト

## 目的

`.kcd` は、Qt UIで作成した作業平面、ワイヤー、面、板材とその依存関係を保存するプロジェクト記述である。

ユーザーはテキストで作業平面やワイヤーを定義し、ビューアで確認できる。

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

`line3d`、`polyline3d`、`bezier3d`、`circle3d`、`arc3d` で作ったワイヤーに、作成元平面や平面ポリシーを後から付ける。`none` または `-` を指定すると、作成元平面なしのままポリシーだけを記録する。

### 直線の寸法拘束

```text
wire_constraint NAME LENGTH|- ANGLE_DEGREES|-
```

直線の始点を固定端として、長さと作業平面内の角度を保持する。`-` はその項目を拘束しない。角度は作成元平面のU方向を0度、V方向を90度とするため、角度を指定する場合は先に `wire_meta` で作成元平面を設定する。長さだけなら自由な3D直線にも使用できる。

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

### 閉じたワイヤーから平面

```text
surface_planar NAME BOUNDARY_WIRE
```

### 2断面からルールド面

```text
surface_ruled NAME SECTION_WIRE_A SECTION_WIRE_B
```

断面ワイヤーの向きが逆の場合は、端点の対応が短くなる向きへ自動でそろえる。

### 3断面以上からロフト面

```text
surface_loft NAME SECTION_WIRE_A SECTION_WIRE_B SECTION_WIRE_C [...]
```

断面は車体の前から後ろの順に並べる。現段階では隣接断面を直線的に補間する。断面ワイヤーを編集すると面を再計算する。

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

### 板材が使う曲面範囲

```text
plate_range PLATE minimumU maximumU minimumV maximumV
```

曲面板材を手動分割した各部品が、元の面のどの範囲を使うかを0から1の正規化座標で保持する。`U` は断面内、`V` は断面間・車体長手方向。通常の未分割板材は `0 1 0 1` であり、このコマンドを省略する。

### 表示・非表示

```text
visibility workplane|wire|surface|plate NAME shown|hidden
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
