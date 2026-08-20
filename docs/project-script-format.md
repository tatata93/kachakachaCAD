# KCDプロジェクトスクリプト

## 目的

`.kcd` は、Qt UIを作る前に数値入力のCAD操作を検証するための小さなプロジェクト記述である。

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

### 3D Cubic Bezier

```text
bezier3d NAME sx sy sz c1x c1y c1z c2x c2y c2z ex ey ez
```

### 作業平面上の直線

```text
sketch_line NAME PLANE su sv eu ev [free|reference|locked]
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

## 実行

```powershell
.\scripts\run-viewer.ps1 -Project examples\first-check.kcd
```

画像出力だけなら:

```powershell
.\scripts\snapshot-viewer.ps1 -Project examples\first-check.kcd
```
