# TASK_LEDGER — UI 再構築(2026-09-18 開始)

正本は `UI_FEATURE_MATRIX.md`。ここは Stage ごとの実装単位と検証結果。
「検証」は必ずこのセッションの tool 出力に紐づける(雲 = core ctest / 当て木 / 自己試験コード、PC = run.txt / png)。

## Stage 一覧

| Stage | 内容 | 状態 | 検証 |
|---|---|---|---|
| S0 棚卸し | 3 HTML + repo の突き合わせ、3文書作成 | DONE(2026-09-18) | 雲: subagent 3本の inventory を MATRIX へ反映 |
| S1 Common | core `app/Ribbon`(mode→category→tool)、`V2Ribbon`、Explorer 階層 + 個別表示 + scroll-to + 「グループ」、右ペイン1道具1枚、HUD/状態行、測定の重ね道具 | NOT_STARTED | |
| S2 Drawing-A | 基本作図/曲線の作り方カード(円/円弧/ベジェ/スプライン)、Tool 切替で panel 同期 | NOT_STARTED | |
| S3 Drawing-B | 編集/変形/作業面(12方式 + 下見)/注記 | NOT_STARTED | |
| S4 Drawing-C | 面作成(6主要 + その他)の新 panel、穴の明示 | NOT_STARTED | |
| S5 Part-A | 押し出し Inventor 型 panel(From/To/範囲/方向/演算/出力)、厚み、回転体案内 | NOT_STARTED | |
| S6 Part-B | 形状編集/面編集/ブール/配置(実装済み + disabled+理由) | NOT_STARTED | |
| S7 Fab-A | 近似(作り方4、候補カードに方式/平均誤差の扱い、表示) | NOT_STARTED | |
| S8 Fab-B | 部材編集(3D で部材を押す、分割/結合/切れ目/半径) | NOT_STARTED | |
| S9 Fab-C | 曲げ(slider/preset)/展開/生成、Explorer の Approximation 節 | NOT_STARTED | |
| S10 Integration | 回帰試験、寸法/拡大率撮影、旧 UI 片づけ、verifier | NOT_STARTED | |

## 単位の記録

(Stage を閉じるたびに追記: commit / 何を実装 / 雲の結果 / PC の結果 / 残り)

### S0 棚卸し — 2026-09-18
- 雲: `docs/v2/ui-redesign/{UI_FEATURE_MATRIX,TASK_LEDGER,UI_LESSONS}.md` 作成。
- backend に無いもの(BLOCKED_BACKEND)は MATRIX の各行に明記。UI では disabled + 理由で出す。

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
