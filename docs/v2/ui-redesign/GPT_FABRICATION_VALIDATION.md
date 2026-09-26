# 製作近似 GPT版の検証・引継ぎ

対象ブランチ: `codex/v2-wp01-build-scaffold`。mainへ切り替えない。
2026-09-26 PC_TESTED: ソース `fc1ff8cab`。`_GO.cmd` で全体ゲートを通過し、同ブランチへpush済み。

## 実装範囲

独立した `fabrication.gpt_create`、方式2、面のB-Rep入力、柱面への近似、
外周と穴を保持した型紙、実長を変えない組立率、最大/RMSと隙間、
部材番号・方向矢印、保存再生成、Undo、SVG/DXF経路。
詳細と制限は [GPT_FABRICATION.md](GPT_FABRICATION.md)。

## Windows検証結果

- Windowsコア/OCCT試験 `gpt_fabrication_tests`: 5/5。
- 前頭部: 許容0.25 mm、分割方向の最小幅0.2 mm、上限12部材で11部材。
  画面経由では標本最大0.197971 mm、RMS 0.0407364 mm、隣接隙間は0.25 mm以内。
  許容条件自体は変更していない。
- 構造試験: 16/16。1500行/ファイルとcad_nextの100行/関数を含む。
- Windows Releaseビルド・ctest 183/183合格。Qt 6.9.2、OCCT 8.0.1。
- V2画面自己試験359/359合格（HP-GPT-F01/F02を含む）。配布フォルダーでも359/359合格。
- 配布ZIP作成・サンプル再読込の撮影確認、旧版の `kachakacha_cad.exe --self-test` 終了コード0。
- 日本語フォントを指定したHP-GPT-F02も合格。下見と型紙の画面を撮影確認。
- PC_TESTEDは自動検証の結果。オーナーの目視受入は別。
- クラウド/Linuxゲートは未実行。

## 追加した画面試験

HP-GPT-F01: 平面から近似、下見・取消の文書不変、組立率変更、確定、Undo/Redo、
保存再読込、型紙表示、SVGとDXFの実ファイル出力。
HP-GPT-F02: 前頭部の元ワイヤー→GPT面→GPT近似→番号と矢印→確定→型紙。

## 残る制約

型紙は中立面。板厚は記録されるが、内外面への補正・厚み付き部品固定は未対応。
追加の手動開口線・折り線・切れ目、分割線が穴を横切る条件は理由付きで拒否。
別々の入力面の接合は自動調整しない。最小幅は分割方向の値で、先細り部の局所幅の保証ではない。
標本偏差は数学的な全域上界ではない。

起動ファイル: `build-msvc2022-x64/Release/kachakacha_cad_next.exe`。配布版は `out/kachakachaCAD-v2/kachakacha_cad_next.exe`。
