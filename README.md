# kachakachaCAD

kachakachaCAD は、鉄道模型製作用のワイヤーフレーム／板材CADです。

目的は、画面上で形を作るだけではなく、プラ板・紙・3Dプリント部品・3Dプリント治具として実際に製作できるデータへつなげることです。

## 中核コンセプト

**3D空間内に自由に紙を置いて、その紙の上に正確な2D図面を描けるCAD。**

この「紙」が作業平面です。正面、側面、上面だけでなく、任意角度、辺との角度指定、3点指定、オフセット指定などで作成できます。

## 技術方針

本命構成は以下です。

```text
言語: C++20
UI: Qt 6
CADカーネル: Open CASCADE Technology
数値計算: Eigen
ビルド: CMake
```

初期段階では、UIやOCCT連携より先に、幾何コアを小さく実装して検証します。

## 現在の骨格

```text
src/core/      UIに依存しない幾何・モデルの中核
tests/         小さな検証テスト
scripts/       開発用スクリプト
docs/          方針・仕様・作業手順
```

現在のコアでは、作業平面、3D直線ワイヤー、3Dポリライン、Cubic Bezier、円、円弧、作業平面上の2D作図から3Dワイヤーへの変換を扱います。

## 最初の確認

PowerShell で以下を実行します。

```powershell
.\scripts\doctor.ps1
.\scripts\check.ps1
```

`check.ps1` は構成、ビルド、テストをまとめて実行します。Windows では Visual Studio Build Tools 2022 を優先して使います。

## 画面で確認

本命のQtデスクトップ版を起動します。

```powershell
.\scripts\run-cad.ps1
```

既存プロジェクトを開いて起動する場合:

```powershell
.\scripts\run-cad.ps1 -Project examples\first-check.kcd
```

Qt版では、画面からプロジェクトの新規作成・読み込み・保存、作業平面の数値作成、3Dワイヤーと平面上ワイヤーの数値作成、選択物の数値編集、交差直線のC面取りとR丸め、削除、Undo/Redoができます。

単体で起動できる配布フォルダを作る場合:

```powershell
.\scripts\package-cad.ps1
```

作成後は `out\kachakachaCAD\kachakacha_cad.exe` を直接起動できます。

## 開発用ビューア

最小ビューアを起動します。

```powershell
.\scripts\run-viewer.ps1
```

表示された画面では、ワイヤーや作業平面をクリックして選択できます。右側の情報パネルの一覧から名前をクリックしても選択できます。選択した対象は強調表示され、名前、種類、平面ポリシー、座標、概算長さなどが表示されます。
選択中の対象は、`A`/`D` で X 方向、`W`/`S` で Y 方向、`Q`/`E` で Z 方向に移動できます。通常は 0.5mm、`Shift` を押しながらなら 5mm、`Ctrl` を押しながらなら 0.1mm ずつ動きます。
`C` で選択中の対象を複製、`Delete` または `Backspace` で削除、`Ctrl+Z` で Undo、`Ctrl+Y` または `Ctrl+Shift+Z` で Redo できます。
`Ctrl+S` で現在の表示内容を `out` フォルダへ `.kcd` として保存できます。
`Tab` で次の対象を選択、`Esc` で選択解除、`1`、`2`、`3` でサンプル切り替え、マウスドラッグで回転、ホイールでズームできます。

数値で定義したプロジェクトを開く場合:

```powershell
.\scripts\run-viewer.ps1 -Project examples\first-check.kcd
```

画像だけ確認する場合:

```powershell
.\scripts\snapshot-viewer.ps1
```

プロジェクトを `.kcd` として書き出す場合:

```powershell
.\scripts\export-project.ps1 -Project examples\first-check.kcd -OutputPath out\first-check-export.kcd
```
