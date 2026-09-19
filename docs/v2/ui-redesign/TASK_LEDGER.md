# TASK_LEDGER — UI 再構築(2026-09-18 開始)

正本は `UI_FEATURE_MATRIX.md`。ここは Stage ごとの実装単位と検証結果。
「検証」は必ずこのセッションの tool 出力に紐づける(雲 = core ctest / 当て木 / 自己試験コード、PC = run.txt / png)。

## Stage 一覧

| Stage | 内容 | 状態 | 検証 |
|---|---|---|---|
| S0 棚卸し | 3 HTML + repo の突き合わせ、3文書作成 | DONE(2026-09-18) | 雲: subagent 3本の inventory を MATRIX へ反映 |
| S1 Common | core `app/Ribbon`(mode→category→tool)、`V2Ribbon`、Explorer 階層 + 個別表示 + scroll-to + 「グループ」、右ペイン1道具1枚、HUD/状態行、測定の重ね道具 | CLOUD_DONE(2026-09-18) / PC 未検証 | 雲: ctest 148/148、typecheck 97 files、自己試験 HP-RB-01〜03 / HP-XP-01〜04(旧 HP-EX-01〜04。Extrude の HP-EX-01〜03 と ID 衝突していたため改名) / HP-ST-01〜03 を追加(PC で実行) |
| S2 Drawing-A | 基本作図/曲線の作り方カード(円/円弧/ベジェ/スプライン)、Tool 切替で panel 同期 | CLOUD_DONE(2026-09-18) / PC 未検証 | 雲: drawing_method_cards_tests、HP-DM-01〜03(PC で実行) |
| S3 Drawing-B | 編集/変形/作業面(12方式 + 下見)/注記 | PARTIAL(作業面の下見 D-24 = 雲側完了) / PC 未検証 | 雲: HP-WP-01。編集/変形/注記の欄は未 |
| S4 Drawing-C | 面作成(6主要 + その他)の新 panel、穴の明示 | NOT_STARTED | |
| S5 Part-A | 押し出し Inventor 型 panel(From/To/範囲/方向/演算/出力)、厚み、回転体案内 | CLOUD_DONE(押し出し棚 + 厚みの道具化) / PC 未検証 | 雲: HP-PA-01/02、HP-TH-01/02。回転体は帯で理由つき Blocked |
| S6 Part-B | 形状編集/面編集/ブール/配置(実装済み + disabled+理由) | NOT_STARTED | |
| S7 Fab-A | 近似(作り方4、候補カードに方式/平均誤差の扱い、表示) | PARTIAL(作り方カード = 雲側完了) / PC 未検証 | 雲: approx_input_tests、HP-AP-03/04。候補カードの方式/平均誤差の表示(F-03)と表示切替(F-04)は未 |
| S8 Fab-B | 部材編集(3D で部材を押す、分割/結合/切れ目/半径) | NOT_STARTED | |
| S9 Fab-C | 曲げ(slider/preset)/展開/生成、Explorer の Approximation 節 | PARTIAL(曲げの基準値/スライダ、生成カード = 雲側完了) / PC 未検証 | 雲: HP-AP-04、HP-GN-01/02。展開(F-11/F-12)は未 |
| S10 Integration | 回帰試験、寸法/拡大率撮影、旧 UI 片づけ、verifier | PARTIAL(I-01 回帰試験、I-03 の一部 = 雲側完了) | 雲: REGRESSIONS.md、RG-02/12/14。撮影(I-02)と verifier(I-04)は未 |

## 単位の記録

(Stage を閉じるたびに追記: commit / 何を実装 / 雲の結果 / PC の結果 / 残り)

### S0 棚卸し — 2026-09-18
- 雲: `docs/v2/ui-redesign/{UI_FEATURE_MATRIX,TASK_LEDGER,UI_LESSONS}.md` 作成。
- backend に無いもの(BLOCKED_BACKEND)は MATRIX の各行に明記。UI では disabled + 理由で出す。

### S1 Common — 2026-09-18(雲側完了、PC 未検証)
- commit `ce52688` 帯: core `app/Ribbon`(mode→category→tool、Blocked 理由つき)+ `V2Ribbon`(2段、setDefaultAction)。面作成は作図モードへ。HP-RB-01〜03。
- commit `d3fafae` Explorer: core `app/ExplorerModel`(節と所属)、`V2ExplorerBuild.cpp`(文書 → 8節 → 原点/グループ/もの、◉ で1つずつ出し隠し = SetVisibilityCommand)、`V2ExplorerMenu.cpp`(右クリック献立は台帳の QAction のみ、グループへ移動は落とすのと同じ道)、「まとまり」→「グループ」。HP-XP-01〜04(旧 HP-EX-01〜04。Extrude の自己試験と ID が衝突していたため改名、2026-09-19)。
- commit `2ce29b0` 状態行/HUD/測定: core `app/StatusLine`(左右の文言と HUD)、`EscapeAction` に ResumeToolAfterMeasure、`V2StatusLine.cpp`、`V2ViewportHud.cpp`、右の欄の見出し下に案内、ShelvesFor は押し出し/面/ブールの最中 1枚だけ。HP-ST-01〜03。
- 雲: ctest 148/148(status_line_tests, explorer_model_tests, ribbon_tests を追加)、qt stub typecheck 97 files OK。
- PC: 未実施(`_GO.cmd` → Release build → 自己試験 → 撮影 が要る)。**PC_VERIFIED ではない。**
- 残り(S1 のうち後ろへ回したもの): 道具パネルの共通枠(C-10)は「見出し + 案内」まで。作り方/入力/オプション/確定・キャンセルの段組みは S2 以降で各 Dock を作り替えるときに。測定の重ね道具は道具の種類を戻す(打ちかけの点は残らない = ToolController の仕様)。

### S2 Drawing-A — 2026-09-18(雲側完了、PC 未検証)
- core `app/DrawingMethodCards`(道具 → カード。核に無いものは理由つき、円弧の始点接線は「その他」)。
- `V2DrawingDock`: 円弧の作り方コンボを廃し、作り方カード(押せる/押せない + 理由)+ カードの一文 + 入力 + オプションの段組みに。カードの押下が ArcMode を決める。
- HP-DM-01〜03(帯 → カード → 3D で円弧、スプライン/ベジェのカード)。
- 正本の HTML と指示書を `docs/v2/ui-redesign/mocks/` に置いた(次のセッションが読めるように)。

### S5 Part-A(押し出しの棚)— 2026-09-18(雲側完了、PC 未検証)
- `V2ExtrudeDock`: 範囲 5 通り(距離/左右対称/両方向に別々の距離/選んだ面まで/全部貫く)と方向 7 通りを棚に全部。逆側の距離・相手の面(作業平面)・向き x y z は選んだときだけ生える。開始面(From)とテーパーは押せない形 + 理由。
- `PrepareExtrudeChoice` / `RefreshExtrudeFromDock` が棚の範囲・逆側の距離・相手・向きの数を読む(詳細の窓は残るが棚だけで全部決められる)。
- HP-PA-01/02。回転体は帯で Blocked(作図の回転面 + 厚み へ案内)。

### S7 Fab-A(作り方カード)/ S9 の曲げ状態の基準値 — 2026-09-19(雲側完了、PC 未検証)
- commit `392646e`: core `ApproxPolicy`(標準/少部品優先/精度優先/手動条件)+ `CandidateForPolicy`。`V2FabricationDock` に作り方カード 4 枚、曲げ状態のスライダと基準値 0/25/50/75/100(組立率を打って当てるのと同じ道)。近似モデルが無いときの「当てる」は理由を状態行へ。
- HP-AP-03(作り方が既定の候補を決め、押した候補が勝つ)、HP-AP-04(基準値は組立率を打って当てる)。

### 2026-09-19 のまとめ(雲側完了、PC 未検証)
- 0fda226 部品「厚み」を 道具 → 3D で面 → 作り方 4 枚 → 下見(kernel 空回し)→ Enter に(P-10、HP-TH-01/02)。既存試験は 1 度目 = 構え、2 度目 = 確定 に。
- 773af56 生成の作り方カード 現在状態 / Flat 0% / Target 100%、fabrication.freeze_target、固定は 1 回の取り消し(F-13/F-14、HP-GN-01/02)。
- d9f6945 作業面の下見(D-24、HP-WP-01)。4b3ec7c 候補の行に方式と平均「—」(F-03)。
- 6cdbe98 既知の退行 15 件の固定表 REGRESSIONS.md、RG-02/12/14(I-01)。40e8550 部品モードの 2 枚目(形状ガイドの役割の表)を廃止(I-03 の一部)。
- **PC ではまだ何も実行していない。** 次: `_GO.cmd` → `_claudeout\ctest.txt` の `cad_next self-test:` 行を読む。

## BLOCKED_BACKEND(必要なら別担当へ)

| ID | 何が要るか | 入力 / 出力 | 既存の近い API | 無い理由 |
|---|---|---|---|---|
| D-04 | 3点円 | 3 点 → Circle segment | `ArcBuilders.h` 3点円弧 | `CurveSegment::MakeCircle` に3点版なし |
| D-08 | 中心・始点・終点の円弧 | 中心/始点/終点 → Arc | `ArcFromEndpointsAndRadius` | ArcMode に無い |
| D-13 | スプライン 通過点/Fit、次数、連続性、許容差、重み、接線 | 点列 + 条件 → BSpline | `MakeCubicBSpline`(制御点のみ) | 補間器なし |
| D-14 | 楕円 | — | — | CurveKind に無い |
| D-22 | スケール | 線 + 中心 + 倍率 | `TranslateCurve` 等 | `WireTransformMethod` に無い |
| D-34 | テキスト注記 | — | ReferenceDimension のみ | entity 種なし |
| C-15 | 面積 | 閉じた輪郭 → mm² | `SignedAreaOnPlane`(押し出し用、内部) | Measurement に無い(押し出しの内部関数を公開すれば可) |
| P-03 | 押し出し From(開始面) | 開始平面 → startOffset | `startOffsetMm`(内部) | Request/Choice に欄なし |
| P-07 | テーパー | 角度 | — | Prism のみ |
| P-08/09 | Solid 回転体/ロフト/スイープ | — | 面版 + 厚み | ThruSections solid=false |
| P-12/13 | Solid フィレット/面取り/シェル/分割 | — | — | TKFillet 未リンク |
| P-15 | 面オフセット/削除/置換 | — | — | なし |
| P-17 | 交差 | — | `BuildBoolean` Union/Difference | Common なし |
| P-18 | Part の移動/回転/ミラー/コピー/パターン | — | 線の変形 | TransformPart なし |
| F-03 | 平均誤差、部材ごとの方式(Plane/Cyl/Cone) | — | `CurvedPanelResult::analysis`(捨てている)、`ApproximatedBand::estimatedDeviationMm` | 集計と保持が無い(小改修で出せる) |
| F-04 | 誤差ヒートマップ | — | — | なし |
| F-06 | 分割の位置指定 | rail parameter | `PreviewBandSplit`(中央固定) | 引数なし(小改修) |
| F-08 | 曲面部材の切れ目 | — | — | 明示的に未実装 |
| F-11/12 | 展開先=作業面/XY、表裏反転 | — | LayoutPattern(紙) | placement に mirror なし |
