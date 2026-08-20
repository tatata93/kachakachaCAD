# ADR 0001: メイン技術スタック

## 状態

採用

## 決定

本命構成は以下とする。

```text
言語: C++20
UI: Qt 6
CADカーネル: Open CASCADE Technology
数値計算: Eigen
ビルド: CMake
```

## 理由

このCADの難所は、正確な点・線・曲線・ワイヤー・面・板厚・出力を扱う幾何処理にある。

Open CASCADE Technology は C++ のCAD/CAM/CAE向けライブラリで、2D/3D形状、BRep、モデリングアルゴリズム、STEP/STLなどのデータ交換に向いている。

Qt 6 は、デスクトップCADに必要なドックUI、ツリー、プロパティパネル、OpenGL表示を作る土台として使う。

## 却下した主案

### TypeScript + Tauri + Three.js + OpenCascade.js

試作には良いが、長期的なCADカーネル中心の実装では C++/OCCT 本体の方が堅い。

### Electron

UI開発は速いが、軽量性を重視する今回の方針では優先しない。

## 実装順

最初からQtやOCCTへ深く依存させない。

1. UI非依存の幾何コア。
2. 作業平面とワイヤー。
3. OCCT変換層。
4. Qt UI。

