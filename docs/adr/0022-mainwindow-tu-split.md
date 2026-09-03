# ADR 0022: MainWindow.cpp の話題別翻訳単位への分割

## 状態

採用(2026-09-03)。実装済み。

## 背景

`MainWindow.cpp` が10,450行に達し、ADR 0018 のガードレール(1ファイル肥大の抑制、
3,000行超の追加禁止)に照らして通常の編集作業が困難になっていた。
`wc -l` 実測(2026-09-03): MainWindow.cpp 10,450 / CadViewport.cpp 5,177 /
PlateFlatPattern.cpp 3,768 / Project.cpp 3,022 / MainWindowSelfTest.cpp 2,993。

## 決定

ADR 0018 の方針(逐語移動による分割、先例: MainWindowSelfTest.cpp、
MainWindowPartModel.cpp)に従い、`MainWindow.cpp` を話題別の翻訳単位へ分割した。
関数本体は一切変更せず、宣言(MainWindow.h)もそのまま。

1. **`MainWindowUiHelpers.h`(新設)** — 旧・無名名前空間の共有ヘルパー
   (ToQString/ToName/MakeNumberField/ExpressionDoubleSpinBox/
   RetargetLineConstraints ほか約380行)を `namespace mainwindow_helpers` の
   inline 関数としてヘッダー化。各TUは `using namespace mainwindow_helpers;` で
   従来どおりの非修飾名で使う。これにより MainWindowSelfTest.cpp にあった
   「無名名前空間はTU間で共有できない」ための逐語コピー4件も削除した。
2. **`MainWindowPanels.cpp`** — 右パネル各タブのUI構築
   (BuildDrawingPanel 〜 BuildInfoPanel、表示設定の保存・読込)。
3. **`MainWindowOutput.cpp`** — 出力タブと書き出し一式
   (BuildOutputPanel、ExportPlanar/ExportSelectedPlate(Pdf)/ExportSelectedBody/
   ExportPlateAssemblyState、型紙・組立ガイド構築、RefreshExportSummary)。
4. **`MainWindowSurface.cpp`** — 面・板の操作ハンドラ
   (面入力グループ、Gordon面、投影、板材化・更新・分割・開口・切れ目)。
5. **`MainWindowMeasure.cpp`** — 計測・参照寸法・基準。

分割後の行数: MainWindow.cpp 5,100 / Panels 1,873 / Output 1,529 /
Surface 1,410 / Measure 756 / UiHelpers.h 430。全ファイルが3,000行未満。

## 運用ルール

- 各TUの冒頭 include・using ブロックは MainWindow.cpp の写し(上位集合可)で始める。
  **MainWindow.cpp と MainWindowSelfTest.cpp の `#include`・`using` は引き続き完全一致**
  (AGENTS.md 既存ルール)。
- 新しい関数は話題の合うTUへ置く。MainWindow.cpp への追加は配線・小物のみ。
- `MainWindowUiHelpers.h` に置けるのは状態を持たない小物だけ。パネル構築や
  プロジェクト操作は置かない。
- 将来の本命はパネルの QWidget サブクラス化(refactoring-plan.md 項目(3)、
  先例: PartModelPanel)。本ADRの分割はその前段であり、矛盾しない。

## 残る大型ファイルの扱い(2026-09-03 判断)

- CadViewport.cpp 5,177行: 分割より先に項目(4)(ViewportToolのStrategy化)を
  行うべきで、その際に自然に分かれる。今回は見送り。
- PlateFlatPattern.cpp 3,768 / Project.cpp 3,022: 単一責務のアルゴリズム・
  モデル実装であり、UIファイルと違って話題の切れ目が細かい。3,000行前後で
  安定しているため現状維持。今後3,500行を超える追加をする場合は分割を先に行う。

## 検証方法

linux-core 12テスト → Windows 実機ビルド(警告確認)→ 17テスト → 起動確認。
