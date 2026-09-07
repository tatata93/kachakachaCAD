# Wire-first V2 要求トレーサビリティ

## 1. 使い方

この表は、必須要件、担当WP、受入試験の対応を固定する。実装担当は自分の行にある試験を削除、skip、
別の目視確認へ置換してはならない。要件を追加した場合は同じcommitでこの表と受入試験を追加する。

## 2. 製品要件

| 要件ID | 内容 | 担当WP | 受入試験 |
| --- | --- | --- | --- |
| PRD-001 | 作図点/平面/Wire/Feature設定が正本 | 03,04 | AT-DOC-001,002 |
| PRD-002 | Part/Guide/近似/型紙は再生成可能 | 03,06,07,09 | AT-DOC-002, AT-EXT-008 |
| PRD-003 | Derived直接編集禁止、Freeze | 03,10 | AT-DOC-004, AT-FAB-011 |
| PRD-004 | 再計算失敗で直前結果をStale保持 | 03,06 | AT-DOC-004 |
| PRD-005 | 全成功または全取消 | 03 | AT-DOC-003,004 |
| PRD-010 | 1 Part=1 connected closed solid | 07 | AT-GEO-010,013 |
| PRD-011 | 穴/空洞を同一Partに保持 | 07 | AT-EXT-004,007 |
| PRD-012 | 非連結結果は複数Part | 07 | AT-GEO-013, AT-EXT-005,007 |
| PRD-013 | zero/open/self-intersect/non-manifold拒否 | 07 | AT-GEO-011, AT-EXP-011 |
| PRD-014 | 完成品/製作/治具はPart role | 03,07,09 | AT-EXP-001, AT-FAB-011 |
| PRD-015 | 材料等は属性であり型を増やさない | 03,09 | AT-EXP-001, AT-FAB-011 |
| PRD-020 | Wire入力を3D/一覧から指定 | 08,10 | AT-UIX-001,007 |
| PRD-021 | 1論理鎖に複数曲線Segment | 04 | AT-WIR-002,003 |
| PRD-022 | 接続順/向き自動、分岐だけ選択 | 04,10 | AT-WIR-002,004, AT-UIX-007 |
| PRD-023 | 全Guide方式で複数線分 | 06 | AT-GEO-001から006 |
| PRD-024 | 両端接続ガイドと断面接触 | 04,06 | AT-WIR-005, AT-GEO-004 |
| PRD-025 | 非平面輪郭を勝手にFillしない | 06,10 | AT-GEO-006 |
| PRD-030 | 押し出し入力の各種類 | 07 | AT-EXT-001から006 |
| PRD-031 | Wire/Part出力を独立選択 | 07,10 | AT-EXT-002,003 |
| PRD-032 | open chainはPart不可 | 07 | AT-EXT-002 |
| PRD-033 | 距離/対称/両側/対象まで/貫通 | 07 | AT-EXT-006 |
| PRD-034 | 新規/足す/引く | 07 | AT-EXT-001,007 |
| PRD-035 | 入力保持と追従 | 03,07 | AT-EXT-008 |
| PRD-036 | 数式入力 | 04,08 | AT-WIR-008, AT-UIX-003 |
| PRD-040 | 完成品と製作モデルを分離 | 09,10 | AT-FAB-005,011 |
| PRD-041 | 1部材は大きな1軸曲げ優先 | 09 | AT-FAB-001,002,005,013 |
| PRD-042 | 二重曲率へ局所relief/分割 | 09 | AT-FAB-003,004,008,013 |
| PRD-043 | 再現度/部材数/幅/方向/可否設定 | 09,10 | AT-FAB-003から005,013 |
| PRD-044 | 開口保持 | 09 | AT-FAB-007,013 |
| PRD-045 | 0-100%で実長維持 | 09,10 | AT-FAB-009,010 |
| PRD-046 | 任意組立状態をPart/STL/STEP | 09,11 | AT-FAB-011, AT-EXP-010 |
| PRD-047 | 全部/選択部材出力 | 10,11 | AT-FAB-012 |
| PRD-048 | 最大/RMS偏差表示 | 09,10 | AT-FAB-003,013 |
| PRD-049 | 任意組立状態のWire/Part/両方を同一境界から固定 | 09,10,11 | AT-FAB-011,014 |
| PRD-050 | 基本作図種類と作図点 | 04,08 | AT-WIR-001,007, AT-UIX-003 |
| PRD-051 | trim/extend/split/join/G1/G2 | 04,08 | AT-WIR-006 |
| PRD-052 | 円弧作図方式 | 04,08 | AT-WIR-007 |
| PRD-053 | 数値欄自動focusとcursor表示 | 08 | AT-UIX-003 |
| PRD-054 | 2D/3Dが同じWire | 03,04,08 | AT-DOC-001, AT-WPL-003 |
| PRD-055 | WorkPlane作成方式 | 02,03,08 | AT-WPL-001,002 |
| PRD-060 | 幾何snapをgridより優先 | 04,08 | AT-UIX-004 |
| PRD-061 | hover ringとlabel | 08 | AT-UIX-004 |
| PRD-062 | Shiftでsnap完全無効 | 08 | AT-UIX-004 |
| PRD-063 | 右クリック近傍候補 | 08 | AT-UIX-004 |
| PRD-064 | 主点と1/2,1/3,1/4副点 | 08 | AT-UIX-005 |
| PRD-065 | grid原点の数値/drag | 08 | AT-UIX-005 |
| PRD-070 | 距離+dX/dY/dZ+投影+軸角 | 04,08 | AT-MEA-001 |
| PRD-071 | 3点/線/曲線/法線角 | 04,08 | AT-MEA-002から004 |
| PRD-072 | 測定から作図点 | 08 | AT-MEA-005 |
| PRD-073 | cube dragと姿勢同期 | 08 | AT-UIX-008 |
| PRD-074 | 自動回転/慣性/snapなし | 08 | AT-UIX-008 |
| PRD-075 | 世界/相対XYZ連続回転 | 08 | AT-UIX-008 |
| PRD-080 | STL/STEP/SVG/DXF/PDF | 11 | AT-EXP-010から013 |
| PRD-081 | STL/STEPは同じB-Rep | 11 | AT-EXP-010 |
| PRD-082 | 出力前検査 | 07,11 | AT-EXP-011 |
| PRD-083 | 1:1 PDF page/overlap/reference | 11 | AT-EXP-013 |
| PRD-084 | temp+validate+replace | 05,11 | AT-EXP-004,011 |
| PRD-090 | 4モード | 08,10 | AT-UIX-001 |
| PRD-091 | 左下操作ガイド維持 | 08,10 | AT-UIX-002 |
| PRD-092 | internal typeを選ばせない | 08,10 | AT-UIX-001 |
| PRD-093 | 理由/箇所/直し方、no crash | 03,08,10 | AT-UIX-009 |
| PRD-094 | Win95/標準theme同機能 | 08 | AT-UIX-010 |
| PRD-095 | 説明の適切な配置 | 08,10 | AT-UIX-002,010 |

## 3. アーキテクチャ要件

| 要件ID | 内容 | 担当WP | 受入試験 |
| --- | --- | --- | --- |
| DOC-001 | coreにQt/OCCTなし | 01,02から05,09 | AT-ARC-001 |
| DOC-002 | UIに幾何処理なし | 08,10 | AT-ARC-001,005 |
| DOC-003 | OCCTにUI/name lookupなし | 06,07,11 | AT-ARC-001,002 |
| DOC-004 | 旧ProjectをV2正本にしない | 03,13 | AT-ARC-003, AT-DOC-001 |
| DOC-005 | domainは純データのみで上位service/OCCTへ依存しない | 02,03 | AT-ARC-001 |
| DOC-016 | 製作近似はB-Rep provider経由、表示mesh入力禁止 | 06,09 | AT-ARC-006, AT-FAB-013 |
| DOC-010 | DAG cycleなし | 03 | AT-DOC-003 |
| DOC-011 | output ID維持 | 03 | AT-DOC-002, AT-EXT-008 |
| DOC-012 | 影響下流だけ再計算 | 03 | AT-DOC-002 |
| DOC-013 | 同一入力の決定性 | 全WP | AT-ARC-003, AT-DOC-002 |
| DOC-014 | 乱数/反復順を結果に使わない | 03,04,09 | AT-DOC-002, AT-WIR-002, AT-FAB-013 |
| DOC-015 | disabled下流の自動付替え禁止 | 03 | AT-GEO-007, AT-DOC-004 |

## 4. 詳細契約

| 要件ID | 内容 | 担当WP | 受入試験 |
| --- | --- | --- | --- |
| FAB-001 | 元完成品Partを変更しない | 03,09 | AT-FAB-005,011 |
| FAB-002 | FabricationModelは別Entity | 03,09 | AT-FAB-005,011 |
| FAB-003 | source/range/manual role/settingsをFeature入力として保持 | 03,09 | AT-FAB-006, AT-EXP-001 |
| FAB-004 | 自動panel/fold/cut/mateはDerived | 03,09 | AT-FAB-006,014 |
| FAB-005 | 明示Freeze時だけ独立Wire/Part | 03,09,10 | AT-FAB-011,014 |
| FAB-006 | 再計算失敗で直前結果をStale保持 | 03,09 | AT-DOC-004, AT-PER-002 |
| FAB-007 | 全組立状態で製作用3D Wireを取得 | 09,10 | AT-FAB-009,014 |
| FAB-008 | Wire/Partは同じ境界評価を使う | 09,11 | AT-FAB-011,014 |
| UIX-001 | モード変更で選択維持 | 08 | AT-UIX-001 |
| UIX-002 | 実行不可理由を表示 | 08,10 | AT-UIX-001,009 |
| UIX-003 | モード変更だけで文書を変更しない | 08 | AT-UIX-001 |
| UIX-004 | 旧内部種類を通常UIで選ばせない | 08,10 | AT-UIX-001 |
| UIX-011 | 全UI入口を一意な実装と試験へ結ぶ | 08,10 | AT-UIX-011 |

## 5. 幾何契約

| 領域 | 担当WP | 主試験 |
| --- | --- | --- |
| 単位と許容差 | 02 | AT-WIR-001, AT-ARC-005 |
| Curve Segment | 04 | AT-WIR-001,006,007 |
| 複数Wire chain | 04 | AT-WIR-002から005 |
| WorkPlane作成方式と平面拘束 | 02,03,08 | AT-WPL-001から003 |
| snap候補幾何 | 04,08 | AT-UIX-004 |
| Planar/Ruled/Loft/Guided/Gordon/Fill | 06 | AT-GEO-001から006 |
| Wire cageからPart | 07 | AT-GEO-010から013 |
| Extrude/Boolean | 07 | AT-EXT-001から008 |
| Projection/opening source | 06,09 | AT-FAB-007 |
| Measurement | 04,08 | AT-MEA-001から004 |
| Numeric expression | 04,08 | AT-WIR-008, AT-UIX-003 |

全Diagnostic codeはAT-ARC-004で重複と日本語表示を検査し、各invalid fixtureで期待codeを比較する。

## 6. 製作契約

| 領域 | 担当WP | 主試験 |
| --- | --- | --- |
| 元Partと別Entity | 03,09 | AT-FAB-005,011 |
| fidelityと偏差 | 09 | AT-FAB-003,013 |
| developable panel | 09 | AT-FAB-001,002 |
| panel-first optimizer | 09 | AT-FAB-005,013 |
| B-Rep extraction/provider boundary | 06,09 | AT-ARC-006 |
| 手動境界役割 | 09,10 | AT-FAB-006 |
| U/V/Both relief | 09 | AT-FAB-003,004 |
| curved V mate | 09 | AT-FAB-008 |
| opening across panels | 09 | AT-FAB-007 |
| pattern metric | 09 | AT-FAB-001,002,009 |
| wrinkle-free assembly | 09 | AT-FAB-009,010 |
| arbitrary state | 09,11 | AT-FAB-011 |
| selected panel output | 10,11 | AT-FAB-012 |
| ER1/ER2 1/87 | 09,12 | AT-FAB-013 |
| arbitrary state Wire/Part materialization | 09,10,11 | AT-FAB-014 |

## 7. 非機能要件

| 内容 | 担当WP | 受入試験 |
| --- | --- | --- |
| UIをblockingしない | 06から11 | AT-PER-001 |
| 古いworker結果を捨てる | 03,06,09 | AT-PER-002 |
| 大規模文書の時間/メモリ | 03,05,08 | AT-PER-003 |
| 500操作安定性 | 03,08,09 | AT-PER-004 |
| 失敗後も継続可能 | 全WP | AT-UIX-009 |
| DPI/theme layout | 08,10 | AT-UIX-010 |
| 配布zip実起動 | 12 | Windows最終gate |
| 全機能画像manual | 12 | acceptance 14章 |

## 8. 現時点の仕様穴

必須要件に試験未割当の項目は0件とする。表へ未割当行が生じた場合、WP-00または該当仕様変更を
未完へ戻す。実装中に試験が不可能と判明した場合、試験を削除せず、測定可能な契約へ仕様変更するADRを先に作る。
