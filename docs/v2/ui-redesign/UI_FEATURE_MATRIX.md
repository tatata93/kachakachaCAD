# UI_FEATURE_MATRIX — 新 UI(3 HTML 正本)と既存 backend の対応表

正本: `kachakachaCAD_drawing_mode_UI_detailed.html` / `kachakachaCAD_part_mode_UI_inventor_like_v2.html` /
`kachakachaCAD_fabrication_mode_UI_detailed.html`(2026-09-18 受領)と指示書
`kachakachaCAD_Fable5_UI_implementation_prompt.md`。
棚卸しは 2026-09-18 に repo(main d3893fa 起点、v2-wp01 f9e07d0)を調べた事実。

STATUS: NOT_STARTED / IMPLEMENTING / CODE_COMPLETE / TESTED / PC_TESTED / PC_VERIFIED / BLOCKED_BACKEND / BLOCKED_HUMAN
(TESTED = 雲の ctest + 自己試験コードあり。CLOUD_TESTED = 雲の ctest と Qt 当て木の型検査まで(自己試験は PC で走る)。
PC_TESTED = Windows の build・ctest・自己試験(offscreen)で通過。画面の目視はまだ。
PC_VERIFIED = Windows で build/ctest/自己試験/画面を確認)

「実装済み」と書くのは次が全部そろったときだけ(2026-09-22 自由曲面の指示書 task_tracking):
UI の入口がある / 人の操作で入力を入れられる / 下見がある / 確定・やめるが動く / 核が全部の入力を使う /
元に戻す・やり直しと保存を壊さない / 対応する試験がある。

列: ID | MODE | CATEGORY | TOOL | METHOD_VARIANT | EXISTING_BACKEND | NEW_UI_ENTRY | PREVIEW | HUMAN_TEST | STATUS | NOTES

## 共通(Common)

| ID | MODE | CATEGORY | TOOL | METHOD_VARIANT | EXISTING_BACKEND | NEW_UI_ENTRY | PREVIEW | HUMAN_TEST | STATUS | NOTES |
|---|---|---|---|---|---|---|---|---|---|---|
| C-01 | all | shell | モード行(作図/部品/製作/出力 + 測定) | — | `app/UiMode.h` AllUiModes、`V2ModeBar.cpp` | 上段 `.modes` 行、右端に「測定(全モード共通)」 | — | HP-UI | PC_TESTED(c2a7b28) | HP-RB-01〜03(PC 未実行)(PC c2a7b28: 自己試験 HP-UI の 3 本が通過) |
| C-02 | all | shell | 2段 Ribbon(カテゴリ→道具) | モードごとのカテゴリ表 | `TopBarCommandIdsForMode` は1段のみ | 新 core `app/Ribbon.h`(mode→categories→tools)+ `V2Ribbon`(Qt) | — | HP-UI | PC_TESTED(c2a7b28) | HP-RB-01〜03(PC 未実行)(PC c2a7b28: 自己試験 HP-UI の 3 本が通過) |
| C-03 | all | explorer | Model Explorer 階層 | Project/Origin/WorkPlanes/Groups/Wires/Surfaces/Solids/Approximation/Generated | `V2EntityTree`(原点→まとまり→(まとまりなし)) | 種類ごとの節を core `app/ExplorerModel.h` で組む | — | HP-XP | PC_TESTED(c2a7b28) | HP-XP-01(節の並び、原点先頭)。PC 未実行。旧 HP-EX-01 から改名(Extrude の HP-EX-01 と ID が衝突していたため)(PC c2a7b28: 自己試験 HP-XP の 4 本が通過) |
| C-04 | all | explorer | 個別 表示/非表示 | entity 単位のチェック | Visibility は `SetVisibilityCommand`(entity)、group は `group.visible` | 各行に ◉ チェック | — | HP-XP | PC_TESTED(c2a7b28) | HP-XP-02(◉ → SetVisibilityCommand、取り消しで戻る)。旧 HP-EX-02 から改名(PC c2a7b28: 自己試験 HP-XP の 4 本が通過) |
| C-05 | all | explorer | 「まとまり」→「グループ」 | — | `group.*` 命令、`GroupTree` | 表示名のみ変更(命令 id は据え置き) | — | test | PC_TESTED(c2a7b28) | 利用者に見える文言を「グループ」へ(命令 id 据え置き)(PC c2a7b28: ctest 172/172 通過) |
| C-06 | all | explorer | Tree ⇄ View 双方向選択 + scroll | — | `AdoptTreeSelection`、`syncingSelection_`; scrollToItem 無し | 3D→Tree で `scrollToItem` | — | HP-XP | PC_TESTED(c2a7b28) | HP-XP-03(両方向)。3D→Tree は scrollToItem 済み。旧 HP-EX-03 から改名(PC c2a7b28: 自己試験 HP-XP の 4 本が通過) |
| C-07 | all | explorer | 右クリック(名前変更/表示/正対/グループへ/複製/削除/プロパティ) | — | 既存 ShowSelectMenu(台帳)、`edit.delete` は依存があれば断る | Tree 専用メニュー | — | HP-XP | PC_TESTED(c2a7b28) | V2ExplorerMenu.cpp。HP-XP-04(旧 HP-EX-04 から改名。同シリーズの HP-EX-01〜03 が Extrude の HP-EX-01〜03 と ID 衝突していたため、04 も合わせて改名)。複製は wire.copy のみ(Part 複製は BLOCKED_BACKEND)。献立の実ラベルは 名前を変える/選択を隠す/すべて表示/選択に正対/グループへ移動/グループ化/グループを解く/コピー(wire.copy)/削除/測定/プロパティ(PC c2a7b28: 自己試験 HP-XP の 4 本が通過) |
| C-08 | all | explorer | 削除不可の理由を人向けに | — | `V2WireCommands.cpp:629-651`(隠すへ倒す) | 「◯◯が参照しているため削除できません」 | — | test | TESTED | RemoveFeatureCommand が「これを使っているものがあるので消せません。先に ◯◯ を消すか…」を出す(core 既存) |
| C-09 | all | panel | 右ペイン = 現在の道具 or 選択物のプロパティだけ | — | `V2OperationPanelHost`(ShelvesFor で複数ページ) | 1道具1ページ。2枚目ページ(Part 等)は出さない | — | HP-UI | PC_TESTED(c2a7b28) | ShelvesFor: 押し出し/面/ブール/厚みの最中は 1枚。面取り(`ChamferOrFilletPair`)は 2026-09-19 に `{Corner}` の1枚へ(量は面取りの棚が数の棚と同じ値を持ち、`SetSizeHandler` で数の棚へ流す。shelf_layout_tests「面取りは面取りの棚1枚」)。**残る2枚組みは無い**(2026-09-20)。かつての ① `UiMode::Fabrication` + 選択 → `{Fabrication, Parameter}` は、数の棚に自分の入口(`view.number_settings`「数の設定」、表示メニュー)を与えて 1 枚にした。以前の理由は次のとおり: 製作の棚は板厚・許すずれを `SetParameterMm` で数の棚へ映しているが、数の棚(`Shelf::Parameter`)を単独で前に出す命令(`ShowShelf(Shelf::Parameter)`)がどこにも無いため、ここで1枚に削ると数の棚がどの組み合わせからも出せなくなり「出せる棚は全部どこかの組み合わせで出る」試験が落ちる。② ~~`UiMode::Output` + 選択 → `{Export, Pattern}`~~ → 2026-09-19 に `{Export}` の1枚へ。型紙の下見は `fabrication.create_pattern` が作ったときに自分で前へ出す。どちらも ShelfLayout.cpp のコメントに理由を明記済み(PC c2a7b28: 自己試験 HP-UI の 3 本が通過) |
| C-10 | all | panel | 道具パネルの共通構造(見出し+hint / 作り方 / 入力 / オプション / 共通 / キャンセル・確定) | — | 各 Dock ばらばら | `V2ToolPanelFrame` 共通枠 | — | HP-UI | PC_TESTED(c2a7b28) | cf43492: 道具の棚の共通の枠(core app/PanelFrame、画面 V2PanelFrame)。押し出し・面を作る・面の編集・厚み・足す引く・配列・近似・作図・面取りを「作り方 → 入力 → 設定 → 共通 → 状態」と下のキャンセル Esc / 確定 Enter にそろえた(厚みは入力 → 作り方だったのを直した)。作図に「共通」(スナップ = 状態行の Snap)とキャンセル・確定(3D の Esc・Enter と同じ道)。panel_frame_tests、自己試験 HP-PF-01〜03 が実際の棚の見出しの並びとボタンを読む門。見出し + 案内(V2OperationPanelHost::SetHint)まで。段組みは各 Dock 作り替え時(S2〜) |
| C-11 | all | viewport | HUD(左上: モード + hint) | — | 無し(status bar と footer のみ) | viewport に HUD 描画 | — | shot | CLOUD_TESTED | V2ViewportHud.cpp。HP-ST-01(左上・画面内・文言一致)。PC 撮影は未 |
| C-12 | all | viewport | ステータス行(モード｜道具｜hint ｜ 座標 ｜ Grid ｜ Snap ｜ Enter/Esc) | — | `statusLabel_`、`ToolKeyHintJa` | 左: 3連、右: 座標/Grid/Snap/キー | — | shot | CLOUD_TESTED | V2StatusLine.cpp + core StatusLine。HP-ST-01。PC 撮影は未 |
| C-13 | all | viewport | View cube / scale bar | — | 既存(`V2Viewport` cube、`DrawScaleBar`) | 据え置き | — | — | PC_VERIFIED(既存) | |
| C-14 | all | measure | 距離/角度/半径/座標 | 2点/3点角/要素/選択 | `MeasurePanel.h`、`Measurement.h` | 上段「測定」+ 作図の測定カテゴリ | — | HP-ME | CLOUD_TESTED | 帯の測定カテゴリ(距離/角度/半径/選択)→ measure.open + MeasureMode。HP-RB |
| C-15 | all | measure | 面積 | — | `app/MeasurePanel` MeasureLoopArea(`modeling::SignedAreaOnPlane`、閉じた輪は `geometry::AnalyzeChain`) | 帯の測定 → 面積(測定の棚の測り方「面積(閉じた線)」) | 表(面積・求め方・周の長さ・平面の法線) | HP-ME-02 | PC_TESTED(2f92305) | 290aef0: 選んだ線(順不同・向き不問)を 1 つの閉じた輪にそろえ、平面に載っていれば囲む面積。直線と円弧だけなら厳密、曲線を含めば刻んだ近似と「求め方」に言う。1 辺を押せば線の全体で測る。閉じていない UI-M002、平面に載らない UI-M003、寸法には残さない UI-M004。面の面積(曲面)は未。measure_panel_tests、selection_tests、ribbon_tests、自己試験 HP-ME-02 |
| C-16 | all | measure | 実行中の道具を捨てず一時測定→復帰 | — | Measure は道具(DrawingTool::Measure)で他道具を捨てる | 測定を「重ね道具」にする(退避/復帰) | — | HP-ME | CLOUD_TESTED | SelectTool で戻り先を覚え、Esc は ResumeToolAfterMeasure(core)。道具の種類は戻るが打ちかけの点は残らない。HP-ST-02/03 |
| C-17 | all | keys | Enter/Esc が viewport focus に依存しない | — | `ToolKeys.h`、`HandleToolKey` | 据え置き + 回帰試験 | — | test | TESTED | 既存 |

## 作図モード(Drawing)

| ID | MODE | CATEGORY | TOOL | METHOD_VARIANT | EXISTING_BACKEND | NEW_UI_ENTRY | PREVIEW | HUMAN_TEST | STATUS | NOTES |
|---|---|---|---|---|---|---|---|---|---|---|
| D-01 | 作図 | 基本作図 | 線 | 2点 | `DrawingTool::Line` | 基本作図→線、作り方「2点」 | 既存 hover | HP-DR | CLOUD_TESTED | 作り方カード「2点」(core DrawingMethodCards)。HP-DM |
| D-02 | 作図 | 基本作図 | 線 | 点+長さ+角度 | `CursorInput`(長さ/角度の打ち込み) | 作り方「点+長さ+角度」= 1点目後に欄へ打つ | 既存 | HP-DR | CLOUD_TESTED | カード「点＋長さ＋角度」= 一文で CursorInput(長さ/角度)へ導く。欄は既存 |
| D-03 | 作図 | 基本作図 | 円 | 中心+半径 | `DrawingTool::Circle` | 作り方カード | 既存 | HP-DR | CLOUD_TESTED | カード「中心＋半径」。HP-DM-01 |
| D-04 | 作図 | 基本作図 | 円 | 3点 | `geometry/ArcBuilders` CircleThroughThreePoints | 作り方カード「3点」 | 既存(3 点目で円が決まる) | HP-DM-07 | PC_TESTED(c2a7b28) | f949853: 3 か所を押すと、その 3 点を通る円(一直線・重なりは GEO-C020 で断る)。3 点目の打ち込み欄は出さない(点で決まる)。tool_tests、drawing_method_cards_tests、自己試験 HP-DM-07 |
| D-05 | 作図 | 基本作図 | 円 | 直径指定 | `CursorInput` 直径欄(直径→半径) | 作り方「直径指定」= 中心後に直径を打つ | 既存 | HP-DR | CLOUD_TESTED | カード「直径指定」= 一文で CursorInput の直径欄へ導く |
| D-06 | 作図 | 基本作図 | 円弧 | 3点 | `ArcMode::ThreePoints` | 作り方カード | 既存 | HP-DR | CLOUD_TESTED | カード「3点」→ ArcMode::ThreePoints。HP-DM-02 で円弧ができる |
| D-07 | 作図 | 基本作図 | 円弧 | 始点・終点・半径 | `ArcMode::EndpointsAndRadius` | カード | 既存 | HP-DR | CLOUD_TESTED | カード「始点・終点・半径」→ EndpointsAndRadius。HP-DM-01 |
| D-08 | 作図 | 基本作図 | 円弧 | 中心・始点・終点 | `geometry/ArcBuilders` ArcFromCenterStartEnd | 作り方カード「中心・始点・終点」 | 既存 | HP-DM-08 | PC_TESTED(c2a7b28) | f949853: 中心 → 始点(半径が決まる)→ 終点(向きだけ使う)で左回りの円弧(GEO-C022)。始点接線はその他カードで残す。tool_tests、自己試験 HP-DM-08 |
| D-09 | 作図 | 基本作図 | 矩形 / 多角形 / 点 | 2角 / — / 1点 | Rectangle, Polyline, Point | 基本作図 | 既存 | HP-DR | CLOUD_TESTED | 矩形「2角」/ 多角形「頂点を順に」/ 点「1点」の 1 枚カード |
| D-10 | 作図 | 曲線 | ベジェ | 制御点(3次4点) | `DrawingTool::Bezier` 3次固定 | 曲線→ベジェ、次数は「3次」のみ有効 | 既存 | HP-DR | CLOUD_TESTED | カード「制御点で作成」(3次固定)。HP-DM-03 |
| D-11 | 作図 | 曲線 | ベジェ | 制御点を点として残す / 制御多角形を補助線に | `keepPoints`(拾った点を残す)、多角形は無し | オプション: 点=既存 keepPoints、多角形=補助線として AddPlainWire(construction) | — | HP-DR | PC_TESTED(c2a7b28) | 8a30a03。棚のオプション「制御多角形を補助線として残す」(ベジェのときだけ出る)。4 つの制御点を結ぶ折れ線を補助線で、線・指定点と同じまとまり(1 回で戻る)。session_tests、drawing_shelf_rows_tests。多角形は UI 側で線を足す(小) |
| D-12 | 作図 | 曲線 | スプライン | 制御点(3次 B-spline) | `DrawingTool::Spline` | 作り方「制御点」 | 既存 | HP-DR | CLOUD_TESTED | カード「制御点」。HP-DM-03 |
| D-13 | 作図 | 曲線 | スプライン | 通過点 / Fit | `geometry/SplineThroughPoints` CubicBSplineThroughPoints | 作り方カード「通過点」(近似 / Fit は押せない形 + 理由) | 既存 | HP-DM-09 | PC_TESTED(c2a7b28) / Fit BLOCKED | f949853: 押した点をすべて通る 3 次の B スプライン(自然端、3 点以上、何点でも。Enter で締める)。近似 / Fit は許すずれの決め方が無いので押せない形 + 理由。tool_tests、自己試験 HP-DM-09 |
| D-14 | 作図 | 曲線 | 楕円 | — | **無し** | disabled | — | — | BLOCKED_BACKEND | |
| D-15 | 作図 | 編集 | トリム / 延長 / 分割 / 結合 / オフセット | 既存 | `wire.trim/extend/split/join/offset` | 編集カテゴリ = 道具のページ(Shelf::Drawing) | あり(トリム・延長・分割: 線の上に置くと消える区間 / 伸びる先 / 分かれる点。4ae6ef4) | HP-DR、HP-TR-01/02 | PC_TESTED(f526c00) | 2026-09-23: トリム・延長・分割を Inventor の手順に(道具を持つだけ → 線の上に置いて押す。V2HoverEditTool)。2026-09-19: `ShelvesFor` を「一道具一枚 = 道具のページ」に統一(指示書 C-09)。Trim/Extend/Move/Copy/Mirror/Rotate/JoinEndpoints/TangentJoin/CurvatureJoin は Edit 棚ではなく Drawing 棚(作り方カード+使い方の一文)を前に出す。カード: トリム「消したい側を押す」/延長「伸ばす端を押す」/分割「押した場所で」/結合3種(端点2つ/接線/曲率)。トリム・延長は既存 `wire.trim`/`wire.extend`(kToolBindings)で道具として選べる。分割・結合3種はカード/棚は用意できたが、`wire.split`/`wire.join`等はまだ即時コマンド(IsWireEditCommand)のみで、その DrawingTool 値を選ぶ入口(kToolBindings)が無い(引き続き NOT_STARTED な部分)。オフセットは DrawingTool を持たない即時コマンドのため対象外。ctest: `v2_shelf_layout_tests`(直す道具も作図の棚道具のページ)、`v2_drawing_method_cards_tests`(どのカードにも次にすることの一文がある)。join の G1/G2 は引き続き検査のみ。4ae6ef4: トリムを Inventor の手順に(道具を持って線の上に置くと消える区間が赤い破線で見え、押すと消える。境目は画面の全部の線。円は円弧に、真ん中なら 2 本。延長・分割も選んでいなければ置いて押す)。curve_trim_tests、hover_edit_plan_tests、HP-TR-01/02 |
| D-16 | 作図 | 編集 | 面取り / 丸め(直線・円弧・円・ベジェの組) | 対称/非対称/残す側/押した位置 | `wire.chamfer/fillet` + `geometry/CornerCurves` + V2CornerPreview | 編集カテゴリ | あり(HP-CN) | HP-CN-01/02/03 | PC_TESTED(101d7b94) | 2026-09-23 曲線の組(db511c9、HP-CN-03 PC 通過)。2026-09-17 実装 |
| D-17 | 作図 | 編集 | 角の加工(折れ線の角) | 全角 / 1頂点 | `wire.corner_*` | 面取りの作り方に収容 | — | test | PC_TESTED(6314e46) | 帯の編集の「その他」に 角を落とす / 角を丸める(wire.corner_*)。。26f00d3: 線を何本選んでも線ごとに 1 つずつ作り、1 本でも作れなければ全部やめる、1 回の元に戻すで消える(全部を 1 本につないで閉じ角を落としていた)。 |
| D-18 | 作図 | 編集 | 交点/中心点/主要点 を点に、基準線 set/clear | — | `wire.intersection_points` 等 | 編集カテゴリ「点を作る」「基準線」 | — | test | PC_TESTED(c2a7b28) | 帯の編集の「その他」に 交点を点に / 中心を点に / 主要点を点に / 基準線にする / 外す。帯の道具は全部つながっている(自己試験「未接続のコマンドが1つも無い」) |
| D-19 | 作図 | 編集 | 投影(平面/面/巻き付け) | 3種 | `wire.project*` | 編集カテゴリ | — | test | PC_TESTED(c2a7b28) | 帯の編集の「その他」に 平面へ投影 / 面へ投影 / 巻き付け投影(面へ投影・回り込みは 5dd930f の面の編集と同じ核) |
| D-20 | 作図 | 編集 | 数値で直す(edit.numeric) | 線/折れ線/円/円弧/ベジェ/スプライン/作業面 | `V2EditDock` | 選択物プロパティ(右ペイン) | — | test | PC_TESTED(c2a7b28) | 帯の編集の「その他」に「数値で直す」(edit.numeric、右の棚)。C-09 の「Property」 |
| D-21 | 作図 | 変形 | 移動/回転/ミラー/コピー | 2点/3点 | `wire.move/rotate/mirror/copy` | 変形カテゴリ = 道具のページ(Shelf::Drawing) | 既存(CursorInput の下見) | HP-DR | CLOUD_TESTED | 2026-09-19: D-15 と同じ変更で Move/Copy/Mirror/Rotate も Drawing 棚(作り方カード+一文)。カード: 移動「2点」(CursorFieldsFor(Move) がまだ角度欄を持たないため「点+距離+角度」は未提供)/コピー「2点」/ミラー「鏡の線 2点」/回転「中心+2方向」。`wire.move/copy/mirror/rotate` は既存の kToolBindings でそのまま道具になる。ctest は D-15 と同じ2本 26f00d3: 何本でも 1 回の元に戻すで戻る(置き換えを Transaction でまとめた)。 |
| D-22 | 作図 | 変形 | スケール | — | `geometry/WireEdit` ScaleCurve、`modeling/TransformInput` PlanScale | 帯の変形 → スケール(作り方カード 倍率 / 基準の2点) | 既存(変形の下見) | HP-DM-10 | PC_TESTED(2f92305) | 368c822: 倍率(棚の「倍率」)か、基準点・元の長さ・新しい長さの 2 点で線を拡大縮小(倍率 1・0 以下は UI-X005 / GEO-E004 で断る)。TransformWire(Scale、開き直しても同じ倍率)。部品は断る。transform_input_tests、wire_edit_tests、自己試験 HP-DM-10 |
| D-23 | 作図 | 変形 | 配列(直線/円形) | 既存 | `wire.array_*`(ダイアログ → 右ペインの棚) | 変形カテゴリ、右ペインに欄(Shelf::Array、V2ArrayDock) | 無し(下見は未実装、理由は NOTES) | HP-AR | PC_TESTED(c2a7b28) | 2026-09-19: `wire.array_linear`/`wire.array_circular` は、自己試験が `SetArrayChooser` を差し替えていなければ右の棚(新設 Shelf::Array)で「作り方(直線/円形)・個数・間隔+方向 or 中心+全体角度・確定/キャンセル」を聞く(V2ArrayDock)。差し替えがあれば従来どおり窓に聞いたことにする(既存自己試験 4 本は無改造で通る)。確定は既存の `PlanLinearArray`/`PlanCircularArray` + `TransformOneWire` の道をそのまま通す(CommitLinearArray/CommitCircularArray に共通化)。中心・方向は作業平面上の (u, v) で持ち、確定時に `WorkPlaneFrame::PointAt`/uAxis・vAxis で世界座標へ変換。Esc/キャンセルは棚を片付けるだけ(何も作らない)。**下見は実装していない**: 確定前に「何本、どこへ」複製されるかを線で見せるには、TransformOneWire の計算をコミットせずに複製後の形だけを取り出す道が要り、いまの TransformOneWire は文書へ Feature を足すところまでが一体になっている。棚だけを切り出す今回の範囲を超えるため見送った(既存ダイアログにも下見は無かった)。自己試験: `V2SelfTestArray.cpp` の `ArrayCases()`(HP-AR-01 棚で個数を決めて確定→本数が増え、1回の undo で戻る。HP-AR-02 Esc で何も作らず棚が引っ込む)。cloud 側は core の `v2_shelf_layout_tests`(出せる棚は全部どこかの組み合わせで出る、openedByOwnCommand に Array を追加)と qt stub 型検査まで。実機の Qt ビルド・自己試験は PC 未実行(PC c2a7b28: 自己試験 HP-AR の 2 本が通過) |
| D-24 | 作図 | 作業面 | 作業面(New) | 12方式 | `WorkPlane.h` 12 methods、`V2WorkPlaneDock` | 作業面カテゴリ→作業面、作り方=12 | 無し→平面の下見を足す | HP-WP | PC_TESTED(c2a7b28) | 棚の欄が変わるたび 40mm 四方の下見(V2WorkPlanePreview.cpp)。作ると消える。HP-WP-01(PC c2a7b28: 自己試験 HP-WP の 1 本が通過) |
| D-25 | 作図 | 作業面 | Select / Set Current / 正対 / 表示 | — | `workplane.set_active`, `view.align_workplane`, visibility | 同カテゴリ | — | HP-WP | PC_TESTED(c2a7b28) | 帯の作業面に 作業面 / 選択に正対 / 作業面に正対 / 作業中にする / グリッド。表示は一覧の ◉。Current/Selected/Other の描き分けは viewport |
| D-26 | 作図 | 面作成 | 平面 | 閉輪郭内側クリック(外形+穴)、外周は何本でも(複数の島) | `PlanarBoundary` + `ProfileRegion` | 面作成→平面(おまかせでも閉じた平らな輪は平面) | あり | HP-SF-10/13 | PC_TESTED(66d3cac) | 面を作るの道具では線の上を押すと線 1 本、線の無い内側を押したときだけ輪郭をまとめて拾う(66d3cac。押し出しは従来どおり縁でも輪郭)。外周 1〜任意・穴 0〜任意(SurfaceCardinality) |
| D-27 | 作図 | 面作成 | ルールド面 | 断面 2〜任意(隣り合う 2 本ずつの帯をつなぐ) | `RuledSections` | 面作成 | あり | HP-SF | PC_TESTED(66d3cac) | 3 本以上は 1-2、2-3… の帯を順につなぐ(f879333) |
| D-28 | 作図 | 面作成 | ロフト面(多レール) | 断面 1〜任意 + ガイド 0〜任意 + 中心線 0〜1、AUTO順 / MANUAL順 | `LoftSections`(LoftSolver: 通常 / 網 = 外側のガイドが両脇(Gordon、仮想断面つき)/ 両端 2 本の掃き(片方だけ伸びる・仮想断面なし)/ 全部を拘束 / 中心線)、`modeling/LoftInput` | 面作成→ロフト面(ガイド付きロフトはここへ統合) | あり(核で実際に作る) | HP-SF-07/11 | PC_TESTED(1405941) | f879333、1405941(外側のガイドが両脇なら断面とガイドの網で作る。両端 2 本の掃きは断面 3 本で断面の間が波打っていた = PC の撮影 ui/12。仮想断面は端の断面と相似な形をガイドの端へ運ぶ。loft_input_tests 15 に、はしご形の真ん中の筋の山は 1 つ、kernel_loft_tests に面の標本の山の数と断面 1 本 + 仮想断面の面積)。3 本目以降のガイドも全部拘束し、作ったあと全部の線からの離れを測って超えたら捨てる。断面 1 本は両端にガイドが 1 本ずつあるときだけ。交わらないガイド・多重交差・ガイド間の断面順の逆転・並びの入れ替わりは線の名前で断る(loft_input_tests 12、kernel_loft_tests: 2断面0本 / 3断面1〜3本 / 5断面5本 / 3本目を持ち上げると面が変わる / 中心線)。欄は可変長の一覧(× で外す・向き反転・手動の並び)で素のクリックで足し外し(70cfb9c、HP-SF-11) |
| D-29 | 作図 | 面作成 | ガイド付きロフト(互換) | 断面+ガイド 1 本以上 | `GuidedLoft`(中身はロフトと同じ) | 棚の「その他」(互換の入口) | あり | HP-SF-06 | PC_TESTED(1405941) | 既存の 2 レール文書は同じ意味で読める(1405941 から LoftSolver = 網。ガイドの片方だけが端の断面より外へ伸びる形は従来の 2 本レール) |
| D-30 | 作図 | 面作成 | 境界面 | 外周 1 輪(線は何本でも)+ 通る線 0〜任意、辺ごとの G0/G1/G2 + 支持面 | `BoundaryFill` + `OuterLoopSplit` + `SurfaceContinuity` | 面作成→境界面、境界の一覧で辺を選ぶと G0/G1/G2・支持面 | あり | HP-SF-10 | PC_TESTED(66d3cac) | 6b604f3(通る線)、819b8f2(連続条件: 支持面の無い G1/G2・受けない作り方の G1/G2 は理由を言って断る。作ったあと法線の角度と横切る向きの法曲率の差を自分で測り、1.5 度 / 0.1 を超えたら断る) |
| D-31 | 作図 | 面作成 | 曲線網 | U 2〜任意 × V 2〜任意。厳密(Gordon)と近似(Filling) | `GordonNetwork`(core で S=L_U+L_V−T、核は B-spline へ)、`CurveNetworkExact` | 面作成→曲線網、棚の「作り方の内訳」に成り立つ方 | あり | HP-SF | PC_TESTED(66d3cac) | 0173810。厳密は全部の線から 0.02 mm 以内でなければ採用しない。外側が端で交わらない網は厳密を断り近似を薦める。交差不足・多重交差・順の矛盾は理由つき(gordon_grid_tests、guide_surface_tests) |
| D-32 | 作図 | 面作成 | 離した面 / 回転面 | 元の面 1〜任意 / 断面 1〜任意 + 軸 1 | `OffsetGuide`, `Revolve`(HP-SF-08) | 面作成「その他」、帯の回転体 | あり | HP-SF-08 | PC_TESTED(66d3cac、回転 / 6314e46、選んでから押す複数断面) | 1 回の生成が 1 つしか受けない役割は一括(1 つずつ全部作り 1 回で戻る、70cfb9c)。回転体を選んでから押すと最後の直線が軸・ほかは全部断面(818113d、自己試験「回転体は選んだ断面を全部回し…」は PC 6314e46 で通過) |
| D-36 | 作図 | 面作成 | 四辺面 | 4 辺(U0/U1/V0/V1)+ 通る線 0〜任意、張り方 標準/平坦優先/丸み優先、辺ごとの G0/G1/G2 | `FourEdgePatch`(GeomFill_BSplineCurves Coons 系 + MakeFilling の張り直し) | 帯「四辺面」、棚の張り方 | あり | HP-SF-12 | PC_TESTED(66d3cac) | f879333 / 70cfb9c / 819b8f2。閉じない 4 辺は離れ量を言って断る、並び・向きは直す、直線・短い円弧は 3 次へ上げてから渡す(0173810)。5 辺は作れないと言う |
| D-37 | 作図 | 面の編集 | 面を合わせる | 縁 → 合わせ先の縁、G0/G1/G2 | `kernel::MatchSurfaceEdge`(隣の縁を角で合わせ先の面に沿わせて張り直し、測る) | 帯「面作成」その他 → 面を合わせる(道具 → 3D で縁) | あり(核で作る) | kernel_surface_edit | PC_TESTED(66d3cac) | 5dd930f / b3853ee。端が離れた縁は断る。新しい面を作り元の面は残す |
| D-38 | 作図 | 面の編集 | 面をつなぐ(ブリッジ) | 縁 2 本、両端 G0/G1/G2、張り | `kernel::BridgeSurfaceEdges` | 同上 → 面をつなぐ | あり | kernel_surface_edit | PC_TESTED(66d3cac) | 66d3cac: G0/G1 は面を直に組む(縁に沿う 3 次 C1 × 横切る 3 次ベジェ。縁の上の点で横切る向き = 縁から出る向き)。G2 の側があるときだけ埋め直す。張り 1.8 で 3.2 度折れていた不具合を PC で確認して直した |
| D-39 | 作図 | 面の編集 | 整える / 対称 / U/V 線 / 面へ投影 | 面 1〜任意(投影は面 1 + 線 1〜任意) | `RefitSurface` / `MirrorSurface` / `ExtractIsoCurves` / `ProjectWiresOntoSurface` | 同上 | あり | kernel_surface_edit | PC_TESTED(66d3cac) | 何枚でも 1 回の取り消しで戻る(1 面 1 Feature `EditSurface`、保存・開き直しの作り直しあり)。解析的な面・減らない面は理由つきで断る |
| D-40 | 作図 | 面の解析 | ゼブラ / 平均・ガウス曲率 / U/V 線 / 曲率コーム / 境目の連続 / 入力線からのずれ / 製作性の目安 | 面 1〜任意(無ければ全部)+ 面を作る・面の編集の下見 | `kernel::OcctSurfaceAnalysis` + `app/SurfaceAnalysis` | 「面の解析」の棚、`view.analysis_zebra` | 下見も塗る | HP-SA-01/02 | PC_TESTED(66d3cac) | 57bb941。可展性は ε≈\|K\|(大きさ/2)²/6 で 0.1 % 未満 = ほぼ可展・1 % 以上 = 強い二重曲率(数値の基準を出し断定しない)。平面・円筒・円錐はほぼ可展(surface_analysis_tests、kernel_surface_analysis_tests) |
| D-41 | 作図 | 面作成 | おまかせ(初心者の入口) | 押した線の役割(断面/ガイド/境界/通る線/中心線)と作り方を決める | `app/SurfaceRoleAssist` + `SurfaceRoleTopology`(候補は AnalyzeGuideSurfaceRequest に通してから言う) | 作り方を選ばずに始めると自動。棚「おまかせ」、行ごとの「役割」、3D の右クリック | あり | HP-SF-13 | PC_TESTED(66d3cac) | b3853ee。事実・おすすめと理由・他の候補(この作り方にする)・おまかせに戻す。役割の色(ガイド青・断面橙・境界紫・通る線緑・中心線青緑)。選んだ順が違っても同じ答え。人が決めた役割はそのまま使う(surface_role_assist_tests 12) |
| D-42 | 作図 | 面作成 | 選んだ線の一覧(Selection Set) | 欄ごとの一覧・× で外す・向き反転・並べ替え・素のクリックで足し外し | `app/SurfaceInputState`(欄が正本、3D はその印) | 右の棚の 4 欄(断面/ガイド/中心線/境界) | — | HP-SF-06/11/13 | PC_TESTED(66d3cac) | 70cfb9c。元に戻す・保存は Feature 側(一括は 1 回で戻る) |
| D-43 | 作図 | 面作成 | 面にする(線を選ぶだけ) | 線 1〜24 本。端点のつながりから閉じた輪を全部(弦のある輪は除く。1 本の線に面は 2 枚まで。小さい輪を先に)。T 字で線を分ける。輪が無く開いた線が並べばロフト。平面 / 4 辺で平らでない → 四辺面 / それ以外 → 境界面。輪ごとに作り方を変えられ、作らない輪を外せる。ずれは挙げて [寄せる](直線の端だけ)/[そのまま]。辺の連続(すでにある面の縁・同じ計画の輪)G0/G1/G2 を欄と Tab で。直前の操作 [開いて直す]。元の線は残す | `app/LoopGraph` + `app/LoopFaces`(片・T 字・隣・連続)+ `app/WireFacts`(事実の行)、画面 `V2LoopFacesTool` + `V2LoopFacesDock`、命令 `surface.from_lines`(Shift+F) | 帯の面作成の先頭「面にする」+ 自分の棚(Shelf::LoopFaces) | あり(輪は実線と番号の札、ずれは赤系の破線と ×、T 字の点、G1/G2 の札) | HP-LF-01〜08 | PC_TESTED(35954ef: HP-LF-01〜09、ctest 182/182、自己試験 354/354) | 2026-09-23: 調査 → 設計案 → 実装(TASK_LEDGER 2026-09-23 面作成 UI)。2026-09-22 オーナー決定(a ずれは伝えて寄せるか断るか選ばせる b 元の線は残す c ボタンを足す)。core: loop_faces_tests(23)、wire_facts_tests(6)。2026-09-24: 辺を角で「側」に束ねて 4 側なら四辺面(裾 2 本でも)、境界面は Coons 初期面(4 側・G0)、削除は面・作業平面にも効く |
| D-33 | 作図 | 注記 | 寸法 | 参照寸法 | `AddReferenceDimensionCommand`(描画なし) | 注記→寸法(測定から残す) | 無し | — | PC_TESTED(c2a7b28) / テキスト注記 BLOCKED | 8e0ae0d: 測定の「残す」で参照寸法に 3D の位置(anchors)を持たせ、3D に寸法線・端の印・値を描く(保存して開き直しても出る。document_file_tests・measure_panel_tests、自己試験で描いた寸法の数)。テキストの注記は文書に文字の要素が無いので押せない形 + 理由。viewport 描画は backend/描画追加が要る → 段階2 |
| D-34 | 作図 | 注記 | テキスト | — | **無し** | disabled+理由 | — | — | BLOCKED_BACKEND | |
| D-35 | 作図 | 測定 | 距離/角度/半径/面積 | — | C-14/C-15 | 測定カテゴリ | — | HP-ME | PC_TESTED(c2a7b28) | 帯の測定に 距離 / 角度 / 半径・直径 / 座標。面積は核に道が無いので押せない形 + 理由 |

## 部品モード(Part)

| ID | MODE | CATEGORY | TOOL | METHOD_VARIANT | EXISTING_BACKEND | NEW_UI_ENTRY | PREVIEW | HUMAN_TEST | STATUS | NOTES |
|---|---|---|---|---|---|---|---|---|---|---|
| P-01 | 部品 | 作成 | 押し出し | 入力: Profile 領域 / 平面 Face | `ProfileRegion`, `FacePushPull`, `ExtrudeInputState` | 作成→押し出し、1. 入力 | あり | HP-EX-01..03 | TESTED(既存) | 818113d: 離れた輪郭 N 個 → 新しい部品 N 個のとき、部品ごとに自分の輪郭(外周と穴)だけを記録する。作り直すと 2 個目以降が 1 個目の写しになっていた(古い文書・足す引くで分かれた部品は、作り直しの段取りが同じ定義の何番目かを持つ: shape_rebuild_tests)。自己試験「輪郭 2 つの押し出しは…開き直しても別の場所にある」は PC 6314e46 で通過。違う平面の輪郭を別々の押し出しにするのは未(今は同じ平面を要る)。 |
| P-02 | 部品 | 作成 | 押し出し | 範囲: 距離/対称/非対称(2距離)/面まで/貫通 | `ExtrudeExtentMode` 5種 | 3. 範囲/方向(全部を棚に) | あり | HP-EX | PC_TESTED(c2a7b28) | 棚の「範囲」に 5 通り。逆側の距離 / 相手の面 は範囲に応じて生える。HP-PA-01/02(PC c2a7b28: 自己試験 HP-EX の 7 本が通過) |
| P-03 | 部品 | 作成 | 押し出し | 2面間(From/To) | **From は無し**(startOffset 内部のみ)、To=作業面のみ | From 欄 disabled+理由、To=作業面 | — | — | BLOCKED_BACKEND(From) / CLOUD_TESTED(To) | 開始面は押せない形 + 理由。To は作業平面を棚の「相手の面」で選ぶ |
| P-04 | 部品 | 作成 | 押し出し | 方向 7種 + 反転 | `ExtrudeDirectionMode` | 3. 方向 combo | あり | test | PC_TESTED(c2a7b28) | 棚の「方向」に 7 通り。数値で決める/選んだ線の向き は x y z 欄が生える。HP-PA-01(PC c2a7b28: ctest 172/172 通過) |
| P-05 | 部品 | 作成 | 押し出し | 演算 New/Add/Cut + 対象 Solid | `ExtrudeBooleanMode` | 4. 演算(segment)+ 対象 slot | あり | HP-EX | TESTED(既存) | |
| P-06 | 部品 | 作成 | 押し出し | 出力プリセット + custom 4フラグ | `ExtrudeOutputs` | 5. 出力 | — | test | TESTED(既存) | startWire は Qt 層で合成(rebuild で消える: NOTES) |
| P-07 | 部品 | 作成 | 押し出し | テーパー | **無し** | 欄 disabled+理由 | — | — | BLOCKED_BACKEND | テーパー欄は押せない形 + 理由(HP-PA-01) |
| P-08 | 部品 | 作成 | 回転体(Solid) | 全回転/角度 | `modeling/SolidInput`(検査と予測)、`kernel/OcctSolid`(形)、`app/SolidInputState`(欄) | 帯の作成 → 回転体 / ロフト立体 / スイープ → 棚「立体を作る」(V2SolidDock、V2SolidTool) | 実際に作った稜線 | HP-SO | PC_TESTED(c2a7b28) | c2a7b28: 部品の「作成」に 回転体(全回転 / 角度指定 / 対称回転)。道具から始め、3D で閉じた輪郭(外周と穴)と軸の直線を押すと実際に核で回した稜線を下見(BRepPrimAPI_MakeRevol)。体積は Pappus の予測と突き合わせ、合わなければ KER-O002。新しい部品 / 足す / 引く(相手は隠す)。CreateSolid(輪郭・軸・角度だけを持ち、開き直したら同じ道で作り直す)。solid_input_tests、solid_input_state_tests、kernel_solid_tests、document_file_tests、自己試験 HP-SO-01 / 04、HP-PF-01 |
| P-09 | 部品 | 作成 | ロフト立体 / スイープ | — | `modeling/SolidInput`(検査と予測)、`kernel/OcctSolid`(形)、`app/SolidInputState`(欄) | 帯の作成 → 回転体 / ロフト立体 / スイープ → 棚「立体を作る」(V2SolidDock、V2SolidTool) | 実際に作った稜線 | HP-SO | PC_TESTED(c2a7b28) / ガイド付き・中心線付き・ねじれ BLOCKED | c2a7b28: ロフト立体(閉じた断面 2〜任意を押した順に BRepOffsetAPI_ThruSections)とスイープ(閉じた輪郭 + 1 本につながる経路、BRepOffsetAPI_MakePipe、姿勢は経路に追従)。ロフトのガイド付き・中心線付き、スイープのねじれ指定・ガイド付きはこの版に作り方が無いので押せない形 + 理由(面作成のロフト面 + 厚み を案内)。自己試験 HP-SO-02 / 03 |
| P-10 | 部品 | 作成 | 厚み | 外側/中央/内側、平面まで。面は何枚でも | `part.thicken`, `thicken_to_plane`, placement | 作成→厚み(slot: 面 1〜任意、作り方3+平面まで) | あり(面ごと) | HP-PT | PC_TESTED(66d3cac) | 道具 → 3D で面 → 作り方(外側/中央/内側/平面まで)→ 下見(kernel の空回し)→ Enter。V2ThickenDock / ThickenInputState。HP-TH-01/02/03。26f00d3: 面を何枚でも入れ、1 枚ずつ別の部品にして 1 回の元に戻すで消える(HP-TH-03)。818113d: 面を平面まで立体に も面 1〜任意 + 平面 1(CLOUD_TESTED)。1 つの立体へ縫い合わせるのは形式の変更と縫い合わせが要るので今はしない |
| P-11 | 部品 | 作成 | ワイヤー群から部品 / 治具 | かご: 閉シェルごとに 1 部品 / 治具: 面 1〜任意 | `part.from_wire_cage`, `part.surface_jig` | 作成「その他」 | — | test | PC_TESTED(6314e46) | HTML に無いが失わない。818113d: かごは先頭のシェルだけ部品にして「N 個」と言っていた → シェルごとに 1 部品(部品はそのシェルの線だけを記録、WireCageShellWires、wire_cage_tests)、1 回で戻る。治具は面ごとに当たり面 + 当て板を 1 組、JIG-E003 は 0 枚のときだけ(surface_jig_tests) |
| P-12 | 部品 | 形状編集 | フィレット/面取り(Solid edge) | — | `kernel/OcctEdgeFinish`(BRepFilletAPI_MakeFillet / MakeChamfer、TKFillet) | 帯の形状編集 → フィレット / 面取り → 棚「辺の丸め・面取り」(V2EdgeFinishDock、V2EdgeFinishTool) | 実際に丸めた稜線 | HP-FL | PC_TESTED(2f92305) | 5d331bf: 3D で部品の辺の近くを押すと一番近い辺が入る(何本でも、押し直すと外れる)。半径 / 距離で実際に丸めて下見、体積の変わり方を状態に出す。EdgeFinish(辺は真ん中の点で持ち、開き直したら同じ辺を丸め直す。元の部品は隠す)。KER-R001〜R004。kernel_edge_finish_tests((1 - π/4) r² L、2 × d²/2 × L)、edge_finish_input_state_tests、document_file_tests、自己試験 HP-FL-01/02 |
| P-13 | 部品 | 形状編集 | シェル / 分割 / 結合 | — | `kernel/OcctShellSplit`(BRepOffsetAPI_MakeThickSolid ByJoin、半空間と共通部分) | 帯の形状編集 → シェル / 分割 → 棚「シェル・分割」(V2ShellSplitDock、V2ShellSplitTool)。結合 = 足す | 実際に作った稜線(分割は両側) | HP-SH | PC_TESTED(2f92305) | ffda3db: シェル = 3D で開けたい面を押す(何枚でも、押し直すと外れる)→ 肉厚で内側へ残す。分割 = 部品を押す → いまの作業平面(「ずらす」で法線の向きへ)で両側を 2 つの部品に(片側が離れた塊なら数を知らせる。平面が通らなければ KER-H005)。ShellSplit(面は面の上の点、分割は平面と側を持ち、開き直したら作り直す。元は隠す)。kernel_shell_split_tests、shell_split_input_state_tests、document_file_tests、自己試験 HP-SH-01/02。2f92305 では作り方ボタンの名前が長く、狭い画面の試験で窓が 1122px に広がった(右の棚は QStackedWidget なので一番広いページの幅になる)→ b4fd388 で「シェル」「分割」に短くした(送信前の PC で確かめる) |
| P-14 | 部品 | 面編集 | 押し引き | 法線方向 | `FacePushPull`(平面のみ) | 面編集→押し引き | あり | HP-EX-03 | TESTED(既存) | |
| P-15 | 部品 | 面編集 | 面オフセット/面削除/面置換 | — | **無し** | disabled+理由 | — | — | BLOCKED_BACKEND | |
| P-16 | 部品 | ブール | 足す / 引く | 土台 1 + 相手 1〜任意(順に足す/引く) | `BooleanInputState`, V2BooleanDock | ブール演算カテゴリ | あり | HP-BO-01/02 | PC_TESTED(66d3cac) | 26f00d3: 相手は何個でも(押し直すと外れる)、核は順に足す/引く、1 つでも作れなければ何も作らない。形式は前から targets[]/tools[](開き直しも全部の相手で作り直す)。選択の条件は「部品 2 つ以上」(818113d で TwoOrMoreParts) |
| P-17 | 部品 | ブール | 交差 | — | **無し** | disabled+理由 | — | — | PC_TESTED(c2a7b28) | 03e39ed: 足す・引くと同じ道具・棚に「交差」(BRepAlgoAPI_Common、相手は何個でも = 全部に共通)。重ならなければ KER-B004、土台のままなら KER-B003 で断る。文書は mode 2、開き直しも交差で作り直す。kernel_boolean_tests、boolean_input_tests、自己試験 HP-BO-03 |
| P-18 | 部品 | 配置 | 移動/回転/ミラー/コピー/パターン(Part) | — | **無し**(線のみ) | disabled+理由 | — | — | PC_TESTED(c2a7b28) | 7df59de: 線と同じ道具・点の置き方で部品の移動(2点)・回転(3点)・ミラー(2点の線、面は作業平面に垂直)・コピー、配列の棚でパターン(直線・円)。TransformPart(元の部品と変換だけを持ち、開き直したら元を作り直して同じ変換)。移動・回転は元を隠し、コピー・ミラー・パターンは残す。剛体なので体積が変わったら KER-P003。kernel_place_tests、shape_rebuild_tests、document_file_tests、自己試験 HP-PL-01〜03 |
| P-19 | 部品 | 共通 | 押し出し距離 ≠ 板厚(20mm 上限が漏れない) | — | `ExtrudeLengthMm` / `ExtrudeDistance` 分離済み | 押し出し棚は ExtrudeLengthMm だけ | — | test | TESTED(既存) | 回帰試験で固定 |

## 製作モード(Fabrication)

| ID | MODE | CATEGORY | TOOL | METHOD_VARIANT | EXISTING_BACKEND | NEW_UI_ENTRY | PREVIEW | HUMAN_TEST | STATUS | NOTES |
|---|---|---|---|---|---|---|---|---|---|---|
| F-01 | 製作 | 近似 | 近似 | 対象: Surface / Solid Face(複数は追加クリック) | `ApproxInput`, `FabricationSourcesFor(splitSolidFaces)` | 近似→近似、1. 対象 | あり | HP-AP-01/02 | TESTED | |
| F-02 | 製作 | 近似 | 近似 | 作り方: 標準/少部品優先/精度優先/手動条件 | 候補 A/B/C(実評価)、棚の欄 | 作り方カード → 候補の並べ方/欄の既定 | あり | HP-AP | PC_TESTED(c2a7b28) | 作り方カード 4 枚(core ApproxPolicy / CandidateForPolicy)。候補を人が押せばそれが勝つ。HP-AP-03(PC c2a7b28: 自己試験 HP-AP の 4 本が通過) |
| F-03 | 製作 | 近似 | 近似 | 候補: 部品数・最大誤差・平均誤差・方式 | partCount/maxDev あり。**平均誤差・方式(Plane/Cyl…)無し** | 候補カード | あり | HP-AP | CLOUD_TESTED | 候補の行に 部材数/最大/平均 —/方式(面ごと|帯)。平均誤差は BLOCKED_BACKEND のまま「—」 |
| F-04 | 製作 | 近似 | 近似 | 表示: 元/近似/部材境界/誤差ヒートマップ | 下見(レール)、ヒートマップ無し | 4. 表示 | — | — | PC_TESTED(c2a7b28) / ヒートマップ BLOCKED_BACKEND | a5c5ce3: 近似の段に表示「元の面 / 近似の姿」(見るだけ、文書は変えない)。誤差の色は核が点ごとの外れを返さないので押せない形 + 理由。自己試験「製作の表示は元の面と近似の姿を出し分け文書は変えない」 |
| F-05 | 製作 | 部材編集 | 近似部品編集(方式/誤差/半径 AUTO-LOCK) | — | `BendRadius` AUTO/LOCK、per-part 誤差は band に内在(未表示) | 部材編集→近似部品編集(ApproxPart slot) | — | HP-FB | PC_TESTED(c2a7b28) | 3D で元の面/立体の面を押すと panelOrigins(core)で部材番号へ解く。方式/最大誤差(モデル全体)を表示。per-part 誤差は無し。HP-PE-01(PC c2a7b28: 自己試験 HP-FB の 1 本が通過) |
| F-06 | 製作 | 部材編集 | 分割 | 中央 / 位置指定 / 候補境界 | `PreviewBandSplit`(中央のみ) | 分割(作り方: 中央=有効、他 disabled) | before/after あり | HP-FB | PC_TESTED(c2a7b28) / 候補境界 BLOCKED_BACKEND | a5c5ce3: 挙げた部材(何枚でも)をそれぞれ 2〜8 枚に等分(1 枚でも細すぎれば全部断る)。値の引き継ぎは 1 枚ずつの決まりを繰り返す。3fa1e09: 2 枚のときは位置(番号の小さい側から 5〜95%)を選べる(PreviewBandSplitAt、細いほうが最小幅未満なら断る)。候補境界は近似が境目の候補を返さないので無い。band_partition_tests、自己試験「部材は何枚にでも等分でき…位置も選べる」。対象部材は 3D クリックで入る(HP-PE-01)。分割位置指定/候補境界は BLOCKED_BACKEND(中央のみ) |
| F-07 | 製作 | 部材編集 | 結合 | 隣接 | `PreviewBandMerge` | 結合(部材A/B slot) | before/after | HP-FB | PC_TESTED(c2a7b28) | a5c5ce3: 番号 1 つならその次と、隣り合う番号なら全部を 1 枚に(離れた番号は断る)。捨てる値は元の番号で前もって言う。対象部材は 3D クリック(Ctrl で 2 つ)で入る。結合は既存 PreviewBandMerge |
| F-08 | 製作 | 部材編集 | 切れ目 | Relief Cut(平面部材のみ)、線 1〜任意 | `assign_relief_cut` | 切れ目(対象部材+線) | — | test | PC_TESTED(6314e46) | 近似の段と部材の編集に「選択した開いた線を切れ目にする」(平面部材)。一覧の近似モデルの下に切れ目の線が出る(a5c5ce3)。曲面部材は BLOCKED_BACKEND。818113d: もう役割(切れ目・開口・折り線)を持つ線は入れ直さない(2 度押すと 2 本に数えていた) |
| F-09 | 製作 | 部材編集 | 半径編集 | AUTO / LOCK / 隣接同期 | AUTO/LOCK あり | 半径編集(AUTO/LOCK) | — | test | TESTED(既存 組立率⇄半径) | 隣接同期は BLOCKED_BACKEND |
| F-10 | 製作 | 曲げ・展開 | 曲げ状態 | スライダ 0-100 + 0/25/50/75/100 | masterPercent、bandProgress、0%=真の展開 | 曲げ状態(slider+preset) | あり(レール) | HP-FB | PC_TESTED(c2a7b28) | スライダ 0〜100 + 基準値 0/25/50/75/100(組立率を打って当てる道)。HP-AP-04。比較表示(0%/100% 重ね)は未(PC c2a7b28: 自己試験 HP-FB の 1 本が通過) |
| F-11 | 製作 | 曲げ・展開 | 展開 | 自動 / 基準辺指定 / 複数配置 | create_pattern(A4 固定)、set_unfold_base | 展開(作り方カード 自動展開/基準辺指定/複数部材配置 + 配置 展開先/表裏 + 「展開」ボタン) | PatternDock | HP-UF | PC_TESTED(c2a7b28) | 自動展開は create_pattern、基準辺指定は set_unfold_base(実際に読むのは「対象部材」欄の番号。3D の線選びではない)の後に create_pattern を通す。複数部材配置は disabled(理由「複数部材の同一平面配置はまだできません」)。展開先コンボは 紙(A4型紙)/XY平面/現在の作業面/新しい作業面 の4項目を並べるが欄ごと disabled(選べるのは紙だけ)にし、理由をラベルで常時表示(UnfoldTargetReasonJa)。自己試験 HP-UF-01(PC c2a7b28: 自己試験 HP-UF の 1 本が通過) |
| F-12 | 製作 | 曲げ・展開 | 展開基準辺 / 表裏反転 | — | set_unfold_base / **反転無し** | 展開基準辺(基準辺指定カード + 対象部材欄) / 表裏 disabled | — | HP-UF | PARTIAL / BLOCKED(反転) | 展開基準辺は基準辺指定カード経由の set_unfold_base で動く(自己試験 HP-UF-01 で確認)。表裏の反転は核に道が無く、表裏コンボは常に disabled + 理由 tooltip「表裏の反転はまだできません(核に反転がありません)」 |
| F-13 | 製作 | 生成 | 現在形状を生成 | 現在/Flat/Target、Surface/Face/Wire | `freeze_state`/`freeze_flat`/`freeze_target` + FreezeOutput | 生成(対象+曲げ状態+出力) | — | HP-FB | PC_TESTED(c2a7b28) | 作り方カード「現在状態」「Flat 0%」「Target 100%」(fabrication.freeze_state/freeze_flat/freeze_target)。自己試験 HP-GN-01/HP-GN-02。ApproxPart は残る(既存) 818113d: 選んだ近似モデルを全部、1 回の元に戻すで固定する(1 つ目だけだった)。面ごとの方式はその近似モデルの部材だけを線にする(全部の近似モデルの部材を置いていた)。(PC c2a7b28: 自己試験 HP-FB の 1 本が通過) |
| F-14 | 製作 | 生成 | Flat Wire / 輪郭 Wire | — | FreezeFlatPanels / freeze WiresOnly | 生成カテゴリ | — | test | PC_TESTED(c2a7b28) | Flat Wire は「Flat 0%」カード(fabrication.freeze_flat)。自己試験 HP-GN-02 で線が増え近似モデルが残ることを確認。「輪郭 Wire」は 2026-09-19 まで `fabrication.freeze_state`(「現在形状を生成」と同じ命令)を指す張りぼてボタンだった(verifier 監査で指摘)。新設した `fabrication.freeze_wires`(「輪郭を線にする」)を指すよう直し、固定で作るもの(FreezeOutput)の設定に関わらず線だけを作ることを自己試験 HP-GN-03 で確認 818113d: 展開 0% は線を 1 本ずつ戻していた → 1 回の元に戻すで消える。選んだ近似モデルを全部。(PC c2a7b28: ctest 172/172 通過) |
| F-15 | 製作 | explorer | Approximation 単位(Candidate/Parts/Relief/Generated) | — | tree は FabricationModel 1行 | Explorer 節を足す(Parts = panels) | — | HP-EX | PC_TESTED(c2a7b28) | a5c5ce3 + F-15 由来: 近似モデルの下に 候補 / 部材 N / 開口・折り線・切れ目の線 / 生成物。生成で作ったもの(線・面・部品)は Entity.generatedFrom に作った近似モデルを持ち(由来であって依存ではない、kcd2 に保存)、一覧はその近似モデルの下の「生成物」に並べる(グループに入れたものはグループが先、近似モデルが無ければ種類の節)。型紙(Pattern)は出力なので「生成物」の節のまま。自己試験 HP-GN-01。Generated は derivedGroupId を設定して集める |

## 入力の数(CARDINALITY)

2026-09-22〜23 の棚卸し(自由曲面の指示書 any_count_audit)。**数学的に決まった数でない入力は、何個でも受ける**。
1 回の生成が 1 つしか受けない演算は、人の側では複数を選べて「1 つずつ全部作り、1 回の元に戻すで消える」(一括)。
1 つでも作れなければ全部やめる(半分だけ作らない)。面の役割の数は `modeling/SurfaceCardinality` の 1 か所が正本で、
画面・検査・表が同じ表を読む。状態の意味は冒頭と同じ(PC_TESTED(66d3cac) = ctest 165/166・自己試験 309/309、PC_TESTED(6314e46) = ctest 166/166・自己試験 313/313 で通過)。

| 入力 | 最小 | 最大 | 決まった数である数学的な理由 | いまの核(backend)の制約 | 一括・元に戻す | 状態 | 根拠(commit / 試験) |
|---|---|---|---|---|---|---|---|
| 面: ロフトの断面 | 1(両端にガイドが 1 本ずつあるとき。仮想断面を足して網にする)/ ふつう 2 | 任意 | なし | 3 本目以降のガイドも全部拘束(LoftSolver)、作ったあと全部の線からの離れを測る。外側のガイドが両脇なら網(断面の間で波打たない) | 1 Feature | PC_TESTED | f879333、1405941、loft_input_tests、kernel_loft_tests、HP-SF-11 |
| 面: ロフトのガイド(レール) | 0 | 任意 | なし | 交わらない・多重交差・断面順の逆転は線の名前で断る | 同上 | PC_TESTED | 同上(3 本目を持ち上げると面が変わる = 入力を無視していない証拠) |
| 面: 中心線 | 0 | 1 | 断面を運ぶ 1 本の道筋。2 本あると沿う先が決まらない | 中心線は面に乗らないので離れの測定から外す | — | PC_TESTED | f879333 |
| 面: ルールドの断面 | 2 | 任意 | 1 枚のルールド面は 2 曲線で決まる → 3 本以上は隣り合う 2 本ずつの帯をつなぐ | — | 1 Feature | PC_TESTED | f879333 |
| 面: 平面の外周 / 穴 | 1 / 0 | 任意 / 任意 | なし | 同じ平面にある外周は複数の島として 1 つの面 | 1 Feature | PC_TESTED | HP-SF-10 |
| 面: 境界面の外周 / 通る線 | 1 輪(線は何本でも)/ 0 | 1 輪 / 任意 | 外周は 1 つの輪(穴は別の役割) | 通る線は MakeFilling の拘束、作ったあと全部の線との距離を測る | 1 Feature | PC_TESTED | 6b604f3、HP-SF-10 |
| 面: 四辺面の辺 / 通る線 | 4 / 0 | 4 / 任意 | 四辺面は U0・U1・V0・V1 の 4 辺で囲う面(定義上 4) | Coons 系(GeomFill_BSplineCurves)+ G1/G2・通る線は張り直し | 1 Feature | PC_TESTED | f879333、819b8f2、HP-SF-12 |
| 面: 曲線網の U / V | 2 / 2 | 任意 / 任意 | 網の外側に U・V が 2 本ずつ要る(網の最小) | 厳密(Gordon)は外側が端で交わる網だけ、ずれ 0.02 mm 以内でなければ採用しない | 1 Feature | PC_TESTED | 0173810、gordon_grid_tests(U5V4) |
| 面: 辺ごとの連続条件 / 支持面 | 0 | 辺の数 | 1 辺に 1 つ(G0/G1/G2) | 支持面の無い G1/G2・受けない作り方は断る。作ったあと測る | — | PC_TESTED | 819b8f2 |
| 面: 離した面の元の面 | 1 | 任意 | 1 回の生成は 1 つ | — | 一括(1 つずつ・1 回で戻る) | PC_TESTED | 70cfb9c |
| 面: 回転体の断面 / 軸 | 1 / 1 | 任意 / 1 | 軸は定義上 1 本 | — | 一括 | PC_TESTED(道具 66d3cac / 選んでから押す複数断面 6314e46) | 70cfb9c、818113d、自己試験「回転体は選んだ断面を全部回し最後の直線を軸にする」 |
| 面の編集: 合わせる / つなぐ の縁 | 2 / 2 | 2 / 2 | 直す縁と合わせ先の縁 / 渡す 2 本の縁(定義上 2) | — | — | PC_TESTED | 5dd930f、66d3cac、kernel_surface_edit_tests |
| 面の編集: 整える・対称・U/V 線 の面 | 1 | 任意 | なし | 解析的な面・減らない面は断る | 一括(1 面 1 Feature、1 回で戻る) | PC_TESTED | 5dd930f |
| 面の編集: 面へ投影 / 回り込み投影 | 面 1 + 線 1〜 / 面 2〜 + 線 1〜 | 線は任意 | 落とす先が 1 枚か、またぐ面を全部か | — | 1 回で戻る | PC_TESTED | 5dd930f |
| 面の解析の対象 | 0(0 なら全部) | 任意 | なし | 何枚かは共通の目盛りで塗る | — | PC_TESTED | 57bb941、HP-SA-01/02 |
| おまかせに入れる線 | 1 | 任意 | なし | 役割の読み分けは core、候補は検査に通してから言う | — | PC_TESTED | b3853ee、HP-SF-13 |
| 押し出しの輪郭 | 1 | 任意(違う平面でもよい) | なし(1 回の押し出しは 1 平面: 輪郭の平面が向きと厚みを決める) | 違う平面の輪郭は平面ごとに別の押し出しにする。下見に全部の輪郭を出し、確定の前に「平面ごとに分ける」と言う。足す・引くは前の平面の結果を次の平面の相手につなぐ(途中の形が分かれたら断る) | N 部品 / 平面の数だけの押し出しを 1 回で戻る。部品ごとに自分の輪郭を記録 | PC_TESTED(6162b46、違う平面)/ PC_TESTED(6314e46、同じ平面) | shape_rebuild_tests、profile_region_tests(平面の組)、自己試験「輪郭 2 つの押し出しは…」「違う平面の輪郭は平面ごとに…」「違う平面の輪郭の足すは…」 |
| 押し出しの相手の立体 / 押す面 | 0 / 1 | 1 / 1 | 足す・引く相手は結果の持ち主 1 つ / 押し引きは面 1 枚 | — | — | TESTED(既存) | — |
| 立体: 回転体の輪郭 / 軸 | 1 / 1 | 任意(外周と穴。同じ平面)/ 1 | 軸は定義上 1 本 | 輪郭の読み方は押し出しと同じ(1 平面)。軸が輪郭を横切れば SOL-004 | 1 Feature、1 回で戻る | PC_TESTED(c2a7b28) | solid_input_tests、HP-SO-01/04 |
| 立体: ロフト立体の断面 | 2 | 任意(押した順に通す) | 2 つが立体の最小 | 断面ごとに 1 つの閉じた線、両端は平ら。ガイド・中心線は未(押せない形) | 1 Feature、1 回で戻る | PC_TESTED(c2a7b28) | solid_input_tests、HP-SO-03 |
| 立体: スイープの輪郭 / 経路 | 1 / 1 | 任意(外周と穴。同じ平面)/ 任意(1 本につながる並び) | なし | 経路は輪郭の平面から始まり、面に沿わない(SOL-008)。姿勢は経路に追従だけ | 1 Feature、1 回で戻る | PC_TESTED(c2a7b28) | solid_input_tests、HP-SO-02 |
| 線の面取り・丸め(2 本の間) | 2 | 2 | 2 本の線の間の角(定義上 2) | — | — | TESTED(既存) | HP-CN-01/02 |
| 角の加工(折れ線の角)/ オフセット | 1 | 任意 | なし | — | 線ごとに 1 つ、1 回で戻る | PC_TESTED(6314e46) | 26f00d3、自己試験「角の加工は何本選んでも線ごとに作り1回で戻る」 |
| 立体の辺のフィレット / 面取り | 1 | 任意(同じ部品の辺) | 1 回は 1 つの部品(辺はその部品の辺) | 辺は真ん中の点で持つ。1 本でも見つからなければ作らない(黙って残りだけ丸めない) | 1 Feature、1 回で戻る | PC_TESTED(2f92305) | P-12、kernel_edge_finish_tests、HP-FL-01/02 |
| シェル(面を除く) | 1 | 任意(同じ部品の面) | 1 回は 1 つの部品 | 面は面の上の点で持つ(辺のそばは内側へ寄せ、辺の上は断る)。1 枚でも見つからなければ作らない | 1 Feature、1 回で戻る | PC_TESTED(2f92305) | P-13、kernel_shell_split_tests、HP-SH-01 |
| 分割(立体を平面で) | 部品 1 | 部品 1 / 平面 1(作業平面) | 1 回は 1 つの部品を 1 枚の平面で(両側が 2 つの部品) | 片側が離れた塊でも 1 つの部品として残し、数を知らせる | 2 Feature、1 回で戻る | PC_TESTED(2f92305) | P-13、kernel_shell_split_tests、HP-SH-02 |
| 厚みの面 | 1 | 任意 | なし | 1 つの立体へ縫い合わせるのは形式の変更と縫い合わせが要る(今は面ごとに 1 部品) | 一括(面ごとに 1 部品、1 回で戻る) | PC_TESTED | 26f00d3、HP-TH-03 |
| 面を平面まで立体に | 面 1 + 平面 1 | 面は任意 / 平面 1 | 平面が 2 つあると埋める先が決まらない | — | 一括 | PC_TESTED(6314e46) | — |
| 足す・引く・交差の相手 | 1 | 任意 | なし(土台は 1: 結果の持ち主) | 順に足す・引く、1 つでも作れなければ何も作らない | 1 Feature | PC_TESTED | 26f00d3、boolean_input_tests、HP-BO |
| 移動・複写・鏡・回転 | 1 | 任意 | なし | 部品は元の形に同じ変換(TransformPart、剛体なので体積が変われば断る) | 線・部品ごとに 1 つ、1 回で戻る | PC_TESTED(6314e46、線)/ PC_TESTED(c2a7b28、部品) | 自己試験「移動と複製が本当に効く」、HP-PL-01〜03 |
| 配列の元 | 1 | 任意(× 個数) | なし | 線と部品(部品は TransformPart の写し) | 1 回で戻る | TESTED(既存、線)/ PC_TESTED(c2a7b28、部品) | HP-AR-01、HP-PL-03 |
| 近似の元(面・立体の面) | 1 | 任意(1 モデル) | なし | — | 1 Feature | TESTED(既存) | HP-AP-01/02 |
| 部材の結合 | 隣り合う 2 つ | 任意(隣り合う範囲) | 離れた部材は 1 枚にできない(切り出しても組み立てられない) | 番号 1 つならその次と、隣り合う番号なら全部を 1 枚に。捨てる値は元の番号で言う | 1 回で戻る | PC_TESTED(c2a7b28) | F-07、a5c5ce3 |
| 部材の分割 | 部材 1 × 2 枚 | 部材は任意 × 2〜8 枚(等分)/ 2 枚なら位置 5〜95% | なし | 挙げた部材をそれぞれ等分、2 枚なら位置で。1 枚でも細すぎれば全部断る。候補境界は未(近似が候補を返さない) | 1 回で戻る | PC_TESTED(c2a7b28) | F-06、a5c5ce3、3fa1e09 |
| 切れ目・境界の役割の線 | 1 | 任意 | なし | 曲面部材の切れ目は核に無い | 1 回で戻る。もう役割を持つ線は入れ直さない | PC_TESTED(6314e46) | F-08 |
| 固定(現在状態・目標 100%・輪郭を線・展開 0%)の近似モデル | 1 | 任意(選んだ全部) | なし | — | 全部を 1 回で戻る | PC_TESTED(6314e46) | F-13/F-14 |
| 派生の固定 | 1 | 任意 | なし | — | ものごとに 1 本、1 回で戻る | PC_TESTED(6314e46) | — |
| かごから部品の線 | 3 | 任意 | 閉じたかごに 3 本以上 | 閉シェルごとに 1 部品(部品はそのシェルの線だけを記録) | 1 回で戻る | PC_TESTED(6314e46) | wire_cage_tests |
| 治具の面 | 1 | 任意 | なし | — | 面ごとに 1 組、1 回で戻る | PC_TESTED(6314e46) | surface_jig_tests |
| 書き出し(STEP/STL)・出力の検査の部品 | 1 | 任意 | なし | 検査は選んだ部品を全部 | — | PC_TESTED(6314e46) | — |
| 結合(wire.join)の線 | 2 | 任意 | なし(端でつながっていること) | 並べ替え・向きの反転はするが形は変えない | — | PC_TESTED(6314e46) | — |
| 端点一致・接線/曲率接続・2 線を交点まで | 2 | 2 | 2 本の線の間の関係(定義上 2) | — | — | TESTED(既存) | — |
| いまの作業平面 | 1 | 1 | いまの作業平面は 1 つ | — | — | TESTED(既存) | — |

## 統合(Integration)

| ID | 内容 | STATUS |
|---|---|---|
| I-01 | 既知の退行 15 件の回帰試験(指示書 known_regressions) | PC_VERIFIED(2026-09-20 / 36cbfb6。REGRESSIONS.md: 12 件は既存試験、RG-02/12/14 を追加。検証役の指摘で #4 の人の道が弱いこと・「人の道」と言いながら内部 setter を叩く試験があることが分かり、HP-DM-04/05/06(円・ベジェ・スプラインを実際に引く)と HP-PA-01 の combo 配線試験を足した。自己試験 296/296) |
| I-02 | 1280x720 / 1920x1080 / 2560x1440 × 100/125/150% の撮影と目視 | PARTIAL(2026-09-20 / 36cbfb6。`_claudeout/resp` に 3 サイズ × 5 場面 × 3 倍率 = 45 枚。1280x720 の 1.5 倍と 2560x1440 を目視し、文字切れ・ボタン重なり・右ペイン欠落・一覧の潰れが無いことを確認。**未達**: 倍率は `QT_SCALE_FACTOR` で代用しており、Windows の表示倍率そのものを 100%/125% に変えた確認はしていない(検証機が 150% 固定)。2560x1440 は画面の高さで 2560x1421 に切られる) |
| I-03 | 旧 UI の重複(形状ガイド(旧)表、旧 toolPalette、Shelf::Part 常設)の片づけ | PC_VERIFIED(2026-09-20 / 36cbfb6。窓 V2ArrayDialog と V2NumberDialog を削除、`guide.create`(形状ガイド(旧))を台帳・献立・説明書から削除、出力モードの右を 1 枚に。Part の2枚目〈GuideTable〉を廃止・guide.\* が自分でShowShelf、旧 toolPalette は既に単一のリボンのみで確認、詳細窓はV2SelfTestExtrudePromise.cppが直接使うため据え置き。PC未実行) |
| I-04 | fresh verifier による仕様照合 | DONE(2026-09-15 の UX 監査に加え、2026-09-20 に別文脈の検証役へ統合段を点検させた。出た指摘のうち本物だったもの〈人の道の穴 3 件・押しても何も起きないカード・台帳に無い id の黙り〉は同日中に手当て。残す指摘は製作モードの 2 枚組みと Windows 表示倍率の実測、どちらも上の行に未達として書いた) |

## 2026-09-24 追加: GPT版の面生成

| ID | 入口 | 操作 | 実装 | 受入試験 | 状態 |
|---|---|---|---|---|---|
| D-44 | 作図 → 面作成 → GPT版 / 形状メニュー | 外周＋内部線（近似）または複数断面、入力一覧/順序/反転、許容偏差・標本最大/RMS、面プレビュー → 確定 | GptSurface / OcctGptSurface / V2GptSurfaceTool、surface.gpt_create | AT-GPT-S001/002、HP-GPT-01〜04 | 実装・検証中、PC未検証 |

仕様と制約は [GPT_SURFACE.md](GPT_SURFACE.md)。既存のD-43の生成処理とは独立。
