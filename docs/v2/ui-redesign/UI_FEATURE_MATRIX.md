# UI_FEATURE_MATRIX — 新 UI(3 HTML 正本)と既存 backend の対応表

正本: `kachakachaCAD_drawing_mode_UI_detailed.html` / `kachakachaCAD_part_mode_UI_inventor_like_v2.html` /
`kachakachaCAD_fabrication_mode_UI_detailed.html`(2026-09-18 受領)と指示書
`kachakachaCAD_Fable5_UI_implementation_prompt.md`。
棚卸しは 2026-09-18 に repo(main d3893fa 起点、v2-wp01 f9e07d0)を調べた事実。

STATUS: NOT_STARTED / IMPLEMENTING / CODE_COMPLETE / TESTED / PC_VERIFIED / BLOCKED_BACKEND / BLOCKED_HUMAN
(TESTED = 雲の ctest + 自己試験コードあり。PC_VERIFIED = Windows で build/ctest/自己試験/画面を確認)

列: ID | MODE | CATEGORY | TOOL | METHOD_VARIANT | EXISTING_BACKEND | NEW_UI_ENTRY | PREVIEW | HUMAN_TEST | STATUS | NOTES

## 共通(Common)

| ID | MODE | CATEGORY | TOOL | METHOD_VARIANT | EXISTING_BACKEND | NEW_UI_ENTRY | PREVIEW | HUMAN_TEST | STATUS | NOTES |
|---|---|---|---|---|---|---|---|---|---|---|
| C-01 | all | shell | モード行(作図/部品/製作/出力 + 測定) | — | `app/UiMode.h` AllUiModes、`V2ModeBar.cpp` | 上段 `.modes` 行、右端に「測定(全モード共通)」 | — | HP-UI | NOT_STARTED | 測定は `measure.open` 共通命令 |
| C-02 | all | shell | 2段 Ribbon(カテゴリ→道具) | モードごとのカテゴリ表 | `TopBarCommandIdsForMode` は1段のみ | 新 core `app/Ribbon.h`(mode→categories→tools)+ `V2Ribbon`(Qt) | — | HP-UI | NOT_STARTED | 旧2段目 toolPalette_ を置き換え |
| C-03 | all | explorer | Model Explorer 階層 | Project/Origin/WorkPlanes/Groups/Wires/Surfaces/Solids/Approximation/Generated | `V2EntityTree`(原点→まとまり→(まとまりなし)) | 種類ごとの節を core `app/ExplorerModel.h` で組む | — | HP-EX | NOT_STARTED | Origin 常に最上段(既存) |
| C-04 | all | explorer | 個別 表示/非表示 | entity 単位のチェック | Visibility は `SetVisibilityCommand`(entity)、group は `group.visible` | 各行に ◉ チェック | — | HP-EX | NOT_STARTED | 既存はまとまり/軸だけチェック |
| C-05 | all | explorer | 「まとまり」→「グループ」 | — | `group.*` 命令、`GroupTree` | 表示名のみ変更(命令 id は据え置き) | — | test | NOT_STARTED | 既知の退行 |
| C-06 | all | explorer | Tree ⇄ View 双方向選択 + scroll | — | `AdoptTreeSelection`、`syncingSelection_`; scrollToItem 無し | 3D→Tree で `scrollToItem` | — | HP-EX | NOT_STARTED | |
| C-07 | all | explorer | 右クリック(名前変更/表示/正対/グループへ/複製/削除/プロパティ) | — | 既存 ShowSelectMenu(台帳)、`edit.delete` は依存があれば断る | Tree 専用メニュー | — | HP-EX | NOT_STARTED | 複製は wire のみ(`wire.copy`)。Part 複製は BLOCKED_BACKEND |
| C-08 | all | explorer | 削除不可の理由を人向けに | — | `V2WireCommands.cpp:629-651`(隠すへ倒す) | 「◯◯が参照しているため削除できません」 | — | test | NOT_STARTED | |
| C-09 | all | panel | 右ペイン = 現在の道具 or 選択物のプロパティだけ | — | `V2OperationPanelHost`(ShelvesFor で複数ページ) | 1道具1ページ。2枚目ページ(Part 等)は出さない | — | HP-UI | NOT_STARTED | 既知の退行「古い右パネルが残る」 |
| C-10 | all | panel | 道具パネルの共通構造(見出し+hint / 作り方 / 入力 / オプション / 共通 / キャンセル・確定) | — | 各 Dock ばらばら | `V2ToolPanelFrame` 共通枠 | — | HP-UI | NOT_STARTED | |
| C-11 | all | viewport | HUD(左上: モード + hint) | — | 無し(status bar と footer のみ) | viewport に HUD 描画 | — | shot | NOT_STARTED | |
| C-12 | all | viewport | ステータス行(モード｜道具｜hint ｜ 座標 ｜ Grid ｜ Snap ｜ Enter/Esc) | — | `statusLabel_`、`ToolKeyHintJa` | 左: 3連、右: 座標/Grid/Snap/キー | — | shot | NOT_STARTED | |
| C-13 | all | viewport | View cube / scale bar | — | 既存(`V2Viewport` cube、`DrawScaleBar`) | 据え置き | — | — | PC_VERIFIED(既存) | |
| C-14 | all | measure | 距離/角度/半径/座標 | 2点/3点角/要素/選択 | `MeasurePanel.h`、`Measurement.h` | 上段「測定」+ 作図の測定カテゴリ | — | HP-ME | NOT_STARTED | |
| C-15 | all | measure | 面積 | — | **無し** | disabled + 理由 | — | — | BLOCKED_BACKEND | `Measurement.h` に面積なし |
| C-16 | all | measure | 実行中の道具を捨てず一時測定→復帰 | — | Measure は道具(DrawingTool::Measure)で他道具を捨てる | 測定を「重ね道具」にする(退避/復帰) | — | HP-ME | NOT_STARTED | |
| C-17 | all | keys | Enter/Esc が viewport focus に依存しない | — | `ToolKeys.h`、`HandleToolKey` | 据え置き + 回帰試験 | — | test | TESTED | 既存 |

## 作図モード(Drawing)

| ID | MODE | CATEGORY | TOOL | METHOD_VARIANT | EXISTING_BACKEND | NEW_UI_ENTRY | PREVIEW | HUMAN_TEST | STATUS | NOTES |
|---|---|---|---|---|---|---|---|---|---|---|
| D-01 | 作図 | 基本作図 | 線 | 2点 | `DrawingTool::Line` | 基本作図→線、作り方「2点」 | 既存 hover | HP-DR | NOT_STARTED | |
| D-02 | 作図 | 基本作図 | 線 | 点+長さ+角度 | `CursorInput`(長さ/角度の打ち込み) | 作り方「点+長さ+角度」= 1点目後に欄へ打つ | 既存 | HP-DR | NOT_STARTED | 欄は CursorInput を再利用 |
| D-03 | 作図 | 基本作図 | 円 | 中心+半径 | `DrawingTool::Circle` | 作り方カード | 既存 | HP-DR | NOT_STARTED | |
| D-04 | 作図 | 基本作図 | 円 | 3点 | **無し** | カード disabled+理由 | — | — | BLOCKED_BACKEND | `ArcBuilders` に3点円なし(3点円弧はある) |
| D-05 | 作図 | 基本作図 | 円 | 直径指定 | `CursorInput` 直径欄(直径→半径) | 作り方「直径指定」= 中心後に直径を打つ | 既存 | HP-DR | NOT_STARTED | |
| D-06 | 作図 | 基本作図 | 円弧 | 3点 | `ArcMode::ThreePoints` | 作り方カード | 既存 | HP-DR | NOT_STARTED | |
| D-07 | 作図 | 基本作図 | 円弧 | 始点・終点・半径 | `ArcMode::EndpointsAndRadius` | カード | 既存 | HP-DR | NOT_STARTED | largeArc/clockwise は core が固定 false → オプション化は backend 小改修 |
| D-08 | 作図 | 基本作図 | 円弧 | 中心・始点・終点 | **無し**(StartTangent はある) | カード disabled+理由 | — | — | BLOCKED_BACKEND | `ArcMode::StartTangent`(始点接線+半径+角)は「その他」として収容 |
| D-09 | 作図 | 基本作図 | 矩形 / 多角形 / 点 | 2角 / — / 1点 | Rectangle, Polyline, Point | 基本作図 | 既存 | HP-DR | NOT_STARTED | 多角形 = Polyline(HTML「多角形」) |
| D-10 | 作図 | 曲線 | ベジェ | 制御点(3次4点) | `DrawingTool::Bezier` 3次固定 | 曲線→ベジェ、次数は「3次」のみ有効 | 既存 | HP-DR | NOT_STARTED | 次数2/4・接線指定は BLOCKED_BACKEND(表示は disabled) |
| D-11 | 作図 | 曲線 | ベジェ | 制御点を点として残す / 制御多角形を補助線に | `keepPoints`(拾った点を残す)、多角形は無し | オプション: 点=既存 keepPoints、多角形=補助線として AddPlainWire(construction) | — | HP-DR | NOT_STARTED | 多角形は UI 側で線を足す(小) |
| D-12 | 作図 | 曲線 | スプライン | 制御点(3次 B-spline) | `DrawingTool::Spline` | 作り方「制御点」 | 既存 | HP-DR | NOT_STARTED | |
| D-13 | 作図 | 曲線 | スプライン | 通過点 / Fit | **無し** | カード disabled+理由 | — | — | BLOCKED_BACKEND | degree/continuity/tolerance/weight/tangent も無し → 欄は disabled |
| D-14 | 作図 | 曲線 | 楕円 | — | **無し** | disabled | — | — | BLOCKED_BACKEND | |
| D-15 | 作図 | 編集 | トリム / 延長 / 分割 / 結合 / オフセット | 既存 | `wire.trim/extend/split/join/offset` | 編集カテゴリ | 面取りと同じ下見(可能なもの) | HP-DR | NOT_STARTED | join の G1/G2 は検査のみ |
| D-16 | 作図 | 編集 | 面取り / 丸め(線どうし) | 対称/非対称/残す側 | `wire.chamfer/fillet` + V2CornerPreview | 編集カテゴリ | あり(HP-CN) | HP-CN-01/02 | TESTED | 2026-09-17 実装 |
| D-17 | 作図 | 編集 | 角の加工(折れ線の角) | 全角 / 1頂点 | `wire.corner_*` | 面取りの作り方に収容 | — | test | NOT_STARTED | |
| D-18 | 作図 | 編集 | 交点/中心点/主要点 を点に、基準線 set/clear | — | `wire.intersection_points` 等 | 編集カテゴリ「点を作る」「基準線」 | — | test | NOT_STARTED | |
| D-19 | 作図 | 編集 | 投影(平面/面/巻き付け) | 3種 | `wire.project*` | 編集カテゴリ | — | test | NOT_STARTED | |
| D-20 | 作図 | 編集 | 数値で直す(edit.numeric) | 線/折れ線/円/円弧/ベジェ/スプライン/作業面 | `V2EditDock` | 選択物プロパティ(右ペイン) | — | test | NOT_STARTED | C-09 の「Property」 |
| D-21 | 作図 | 変形 | 移動/回転/ミラー/コピー | 2点/3点 | `wire.move/rotate/mirror/copy` | 変形カテゴリ | 既存 | HP-DR | NOT_STARTED | |
| D-22 | 作図 | 変形 | スケール | — | **無し** | disabled+理由 | — | — | BLOCKED_BACKEND | |
| D-23 | 作図 | 変形 | 配列(直線/円形) | 既存 | `wire.array_*`(ダイアログ) | 変形カテゴリ、右ペインに欄 | 無し | test | NOT_STARTED | |
| D-24 | 作図 | 作業面 | 作業面(New) | 12方式 | `WorkPlane.h` 12 methods、`V2WorkPlaneDock` | 作業面カテゴリ→作業面、作り方=12 | 無し→平面の下見を足す | HP-WP | NOT_STARTED | |
| D-25 | 作図 | 作業面 | Select / Set Current / 正対 / 表示 | — | `workplane.set_active`, `view.align_workplane`, visibility | 同カテゴリ | — | HP-WP | NOT_STARTED | Current/Selected/Other の描き分けは viewport |
| D-26 | 作図 | 面作成 | 平面 | 閉輪郭内側クリック(外形+穴) | `PlanarBoundary` + `ProfileRegion` | 面作成→平面 | あり | HP-SF | TESTED(既存) | |
| D-27 | 作図 | 面作成 | ルールド面 | 断面2本 | `RuledSections` | 面作成 | あり | HP-SF | TESTED(既存) | |
| D-28 | 作図 | 面作成 | ロフト面 | AUTO順 / MANUAL順 | `LoftSections` + lockSectionOrder | 面作成 | あり | HP-SF-07 | TESTED | |
| D-29 | 作図 | 面作成 | ガイド付きロフト | 断面+ガイド | `GuidedLoft` | 面作成 | あり | HP-SF-06 | TESTED | createVirtualEndSections は未露出(advanced 候補) |
| D-30 | 作図 | 面作成 | 境界面 | boundary / G1 | `BoundaryFill`(tangentContinuity は未露出) | 面作成 | あり | HP-SF | NOT_STARTED | G1 指定は advanced に収容(backend あり) |
| D-31 | 作図 | 面作成 | 曲線網 | U/V | `GordonNetwork`(断面=U、ガイド=V) | 面作成 | あり | HP-SF | NOT_STARTED | |
| D-32 | 作図 | 面作成 | 離した面 / 回転面 | offset / 軸+角度 | `OffsetGuide`, `Revolve`(HP-SF-08) | 面作成「その他」へ収容 | あり | HP-SF-08 | TESTED(回転) | HTML に無いが失わない |
| D-33 | 作図 | 注記 | 寸法 | 参照寸法 | `AddReferenceDimensionCommand`(描画なし) | 注記→寸法(測定から残す) | 無し | — | NOT_STARTED | viewport 描画は backend/描画追加が要る → 段階2 |
| D-34 | 作図 | 注記 | テキスト | — | **無し** | disabled+理由 | — | — | BLOCKED_BACKEND | |
| D-35 | 作図 | 測定 | 距離/角度/半径/面積 | — | C-14/C-15 | 測定カテゴリ | — | HP-ME | NOT_STARTED | |

## 部品モード(Part)

| ID | MODE | CATEGORY | TOOL | METHOD_VARIANT | EXISTING_BACKEND | NEW_UI_ENTRY | PREVIEW | HUMAN_TEST | STATUS | NOTES |
|---|---|---|---|---|---|---|---|---|---|---|
| P-01 | 部品 | 作成 | 押し出し | 入力: Profile 領域 / 平面 Face | `ProfileRegion`, `FacePushPull`, `ExtrudeInputState` | 作成→押し出し、1. 入力 | あり | HP-EX-01..03 | TESTED(既存) | |
| P-02 | 部品 | 作成 | 押し出し | 範囲: 距離/対称/非対称(2距離)/面まで/貫通 | `ExtrudeExtentMode` 5種 | 3. 範囲/方向(全部を棚に) | あり | HP-EX | NOT_STARTED | 貫通は「引く」時のみ(core) |
| P-03 | 部品 | 作成 | 押し出し | 2面間(From/To) | **From は無し**(startOffset 内部のみ)、To=作業面のみ | From 欄 disabled+理由、To=作業面 | — | — | BLOCKED_BACKEND(From)/ NOT_STARTED(To) | To は `ExtrudeTargetKind::Plane` |
| P-04 | 部品 | 作成 | 押し出し | 方向 7種 + 反転 | `ExtrudeDirectionMode` | 3. 方向 combo | あり | test | NOT_STARTED | SelectedVector/CustomXYZ に xyz 欄を足す |
| P-05 | 部品 | 作成 | 押し出し | 演算 New/Add/Cut + 対象 Solid | `ExtrudeBooleanMode` | 4. 演算(segment)+ 対象 slot | あり | HP-EX | TESTED(既存) | |
| P-06 | 部品 | 作成 | 押し出し | 出力プリセット + custom 4フラグ | `ExtrudeOutputs` | 5. 出力 | — | test | TESTED(既存) | startWire は Qt 層で合成(rebuild で消える: NOTES) |
| P-07 | 部品 | 作成 | 押し出し | テーパー | **無し** | 欄 disabled+理由 | — | — | BLOCKED_BACKEND | |
| P-08 | 部品 | 作成 | 回転体(Solid) | 全回転/角度 | **Solid は無し**(面の回転体はある) | 作成→回転体 = 回転面 + 厚み の案内 | — | — | BLOCKED_BACKEND | HP-SF-08 の回転面を「部品」から呼べるようにする |
| P-09 | 部品 | 作成 | ロフト立体 / スイープ | — | **無し**(面のみ) | disabled+理由(面作成→厚み を案内) | — | — | BLOCKED_BACKEND | |
| P-10 | 部品 | 作成 | 厚み | 外側/中央/内側、平面まで | `part.thicken`, `thicken_to_plane`, placement | 作成→厚み(slot: 面、作り方3+平面まで) | 無し→足す | HP-PT | NOT_STARTED | per-face は BLOCKED_BACKEND |
| P-11 | 部品 | 作成 | ワイヤー群から部品 / 治具 | — | `part.from_wire_cage`, `part.surface_jig` | 作成「その他」 | — | test | NOT_STARTED | HTML に無いが失わない |
| P-12 | 部品 | 形状編集 | フィレット/面取り(Solid edge) | — | **無し**(TKFillet 未リンク) | disabled+理由 | — | — | BLOCKED_BACKEND | |
| P-13 | 部品 | 形状編集 | シェル / 分割 / 結合 | — | **無し**(結合=足す) | disabled+理由(結合は足すへ) | — | — | BLOCKED_BACKEND | |
| P-14 | 部品 | 面編集 | 押し引き | 法線方向 | `FacePushPull`(平面のみ) | 面編集→押し引き | あり | HP-EX-03 | TESTED(既存) | |
| P-15 | 部品 | 面編集 | 面オフセット/面削除/面置換 | — | **無し** | disabled+理由 | — | — | BLOCKED_BACKEND | |
| P-16 | 部品 | ブール | 足す / 引く | Target→Tool slot | `BooleanInputState`, V2BooleanDock | ブール演算カテゴリ | あり | HP-BO-01/02 | TESTED | |
| P-17 | 部品 | ブール | 交差 | — | **無し** | disabled+理由 | — | — | BLOCKED_BACKEND | |
| P-18 | 部品 | 配置 | 移動/回転/ミラー/コピー/パターン(Part) | — | **無し**(線のみ) | disabled+理由 | — | — | BLOCKED_BACKEND | |
| P-19 | 部品 | 共通 | 押し出し距離 ≠ 板厚(20mm 上限が漏れない) | — | `ExtrudeLengthMm` / `ExtrudeDistance` 分離済み | 押し出し棚は ExtrudeLengthMm だけ | — | test | TESTED(既存) | 回帰試験で固定 |

## 製作モード(Fabrication)

| ID | MODE | CATEGORY | TOOL | METHOD_VARIANT | EXISTING_BACKEND | NEW_UI_ENTRY | PREVIEW | HUMAN_TEST | STATUS | NOTES |
|---|---|---|---|---|---|---|---|---|---|---|
| F-01 | 製作 | 近似 | 近似 | 対象: Surface / Solid Face(複数は追加クリック) | `ApproxInput`, `FabricationSourcesFor(splitSolidFaces)` | 近似→近似、1. 対象 | あり | HP-AP-01/02 | TESTED | |
| F-02 | 製作 | 近似 | 近似 | 作り方: 標準/少部品優先/精度優先/手動条件 | 候補 A/B/C(実評価)、棚の欄 | 作り方カード → 候補の並べ方/欄の既定 | あり | HP-AP | NOT_STARTED | 標準=棚どおり、少部品=C、精度=B(上限大)、手動=欄を出す |
| F-03 | 製作 | 近似 | 近似 | 候補: 部品数・最大誤差・平均誤差・方式 | partCount/maxDev あり。**平均誤差・方式(Plane/Cyl…)無し** | 候補カード | あり | HP-AP | NOT_STARTED | 平均誤差=BLOCKED_BACKEND(表示「—」)、方式=帯/面ごと で表記 |
| F-04 | 製作 | 近似 | 近似 | 表示: 元/近似/部材境界/誤差ヒートマップ | 下見(レール)、ヒートマップ無し | 4. 表示 | — | — | NOT_STARTED / ヒートマップ BLOCKED_BACKEND | |
| F-05 | 製作 | 部材編集 | 近似部品編集(方式/誤差/半径 AUTO-LOCK) | — | `BendRadius` AUTO/LOCK、per-part 誤差は band に内在(未表示) | 部材編集→近似部品編集(ApproxPart slot) | — | HP-FB | NOT_STARTED | 部材の 3D クリック選択が要る(いまは番号欄) |
| F-06 | 製作 | 部材編集 | 分割 | 中央 / 位置指定 / 候補境界 | `PreviewBandSplit`(中央のみ) | 分割(作り方: 中央=有効、他 disabled) | before/after あり | HP-FB | NOT_STARTED | 位置指定は backend 小改修で可(rail parameter) |
| F-07 | 製作 | 部材編集 | 結合 | 隣接 | `PreviewBandMerge` | 結合(部材A/B slot) | before/after | HP-FB | NOT_STARTED | 方式維持/手動方式は帯方式では意味なし → 1方式 |
| F-08 | 製作 | 部材編集 | 切れ目 | Relief Cut(平面部材のみ) | `assign_relief_cut` | 切れ目(対象部材+線) | — | test | NOT_STARTED | 曲面部材は BLOCKED_BACKEND |
| F-09 | 製作 | 部材編集 | 半径編集 | AUTO / LOCK / 隣接同期 | AUTO/LOCK あり | 半径編集(AUTO/LOCK) | — | test | TESTED(既存 組立率⇄半径) | 隣接同期は BLOCKED_BACKEND |
| F-10 | 製作 | 曲げ・展開 | 曲げ状態 | スライダ 0-100 + 0/25/50/75/100 | masterPercent、bandProgress、0%=真の展開 | 曲げ状態(slider+preset) | あり(レール) | HP-FB | NOT_STARTED | 比較表示は 0%/100% 重ね(下見の線を足す) |
| F-11 | 製作 | 曲げ・展開 | 展開 | 自動 / 基準辺指定 / 複数配置 | create_pattern(A4 固定)、set_unfold_base | 展開(基準辺 slot、展開先) | PatternDock | HP-FB | NOT_STARTED | 展開先=紙のみ。作業面/XY は BLOCKED_BACKEND |
| F-12 | 製作 | 曲げ・展開 | 展開基準辺 / 表裏反転 | — | set_unfold_base / **反転無し** | 展開基準辺 / 反転 disabled | — | test | NOT_STARTED / BLOCKED | |
| F-13 | 製作 | 生成 | 現在形状を生成 | 現在/Flat/Target、Surface/Face/Wire | `freeze_state` + FreezeOutput | 生成(対象+曲げ状態+出力) | — | HP-FB | NOT_STARTED | ApproxPart は残る(既存) |
| F-14 | 製作 | 生成 | Flat Wire / 輪郭 Wire | — | FreezeFlatPanels / freeze WiresOnly | 生成カテゴリ | — | test | NOT_STARTED | |
| F-15 | 製作 | explorer | Approximation 単位(Candidate/Parts/Relief/Generated) | — | tree は FabricationModel 1行 | Explorer 節を足す(Parts = panels) | — | HP-EX | NOT_STARTED | Generated は derivedGroupId を設定して集める |

## 統合(Integration)

| ID | 内容 | STATUS |
|---|---|---|
| I-01 | 既知の退行 15 件の回帰試験(指示書 known_regressions) | NOT_STARTED |
| I-02 | 1280x720 / 1920x1080 / 2560x1440 × 100/125/150% の撮影と目視 | NOT_STARTED |
| I-03 | 旧 UI の重複(形状ガイド(旧)表、旧 toolPalette、Shelf::Part 常設)の片づけ | NOT_STARTED |
| I-04 | fresh verifier による仕様照合 | NOT_STARTED |
