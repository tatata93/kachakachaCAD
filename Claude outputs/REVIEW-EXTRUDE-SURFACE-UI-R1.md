# Extrude / Surface Human Interaction Layer — 実装レビュー

判定対象 HEAD: `9b39417`
UI仕様正本: `kachakachaCAD_extrude_surface_UI_final_mock.html`(受領済み)
スクリーンショット `01_..`〜`07_..`: **提供されていない**(PC・会話のどこにも無い)。
モックとの比較は HTML と実装コード、および PC が生成した実画面
(`_claudeout/v2-mode-part.png` ほか)で行った。

## 結果: **FAIL**

重点BLOCKER 16件のうち **PASS 1件 / FAIL 15件**。
根本理由は単純で、**監査(UX-AUDIT-EXTRUDE-SURFACE-R1)以降、修正コミットが1つも無い**。
HEAD は監査対象と同一である。唯一 PASS の項目は、監査より前(R5〜R7)に直っていたもの。

新規の出力プリセット仕様も **未実装**(下記 O-1〜O-5)。

---

## 重点BLOCKER 判定表

| # | 項目 | 判定 | 根拠(実在のファイル:行) |
|---|---|---|---|
| 1 | Extrude panel が visible/raised か | **FAIL** | `ShelfLayout.cpp:38 ShelvesFor()` は `Shelf::Extrude` を返さない。`AllShelves()`(:30)には含まれるので `V2ViewCommands.cpp:811 RefreshRightShelves()` が毎回 `setVisible(false)`。`ShowExtrudeShelf()` は最終行で `RefreshRightShelves()` を呼び、**埋めた棚を自分で隠す**。`setVisible(true)`/`raise()` は全コードに無い |
| 2 | ExtrudeDistance と板厚の分離 | **FAIL** | `CommandParameters.cpp:21` に `ExtrudeDistance` が1つ、名称が「板厚(押し出しの距離)」。`V2PartDock.cpp:125-126` が2欄とも同じ id へ通知 |
| 3 | 20mm 制約が Extrude へ漏れていないか | **FAIL** | `CommandParameters.cpp:21` 最大 20.0。`V2ExtrudeInteractive.cpp UpdateExtrudePreview()` が毎ドラッグフレームで `parameterDock_->Apply(ExtrudeDistance,…)` を通す → `CommandParameters.cpp:122` が UI-P002 で拒否。戻り値を見ずに先へ進むので**形は出来るがエラーが積まれる** |
| 4 | tool-first で Ctrl 必須でないか | **FAIL** | 構え(`V2PendingCommand.cpp:28`)はある。しかし2つ目の入力を足すには Ctrl が要る(`V2ViewportInput.cpp:786 SelectionModeFor`: 非Ctrl は Replace)。モックの TARGET/PROFILE スロット+「選び直す」に相当する**役割スロット充填が無い** |
| 5 | Tool Input State と画面の Target/Profile 一致 | **FAIL** | 内部は一致(`ExtrudePlan` → `V2ExtrudeDock::ShowPlan`)。しかし棚が不可視なので**画面に出ない**。またモックは対象/輪郭の2スロット独立表示、実装は1本の文字列 `input_` |
| 6 | selection filter が tool context を考慮するか | **FAIL** | `V2ViewportInput.cpp:929 CollectCandidatesAt` の優先順は 点→線→形 で固定。実行中のコマンドを見ていない。押し出し中に面を優先する仕組みは無い |
| 7 | Preview と Commit が同一 Input Snapshot か | **PASS** | 距離は両方 `viewport_->ExtrudeHandleDistanceMm()`、向きは両方 `ExtrudeDirectionForMode()`。`PreparedExtrudeChoice{remembered,resolved}` で覚える形と渡す形を分離済み(R6/R7 で対応) |
| 8 | Surface 入口が「面を作る」に統合されたか | **FAIL** | モックは上帯に「面を作る」1つ。実装は `UiMode.cpp:111` で `guide.create`/`guide.add_row`/`guide.build` の**3つ**、さらに部品の棚に `guide.revolve` ほか10ボタン |
| 9 | closed planar Wire 1本 → Planar Surface | **FAIL** | `V2GuideCommands.cpp:272` が `made.sections < 2` で拒否。`GuideTableBuild.cpp:56 AutoSectionTable` は役割を常に Section、方式は 2本=Ruled/3本=Loft のみで **PlanarBoundary に到達しない** |
| 10 | Surface method/roles/order が常時可視か | **FAIL** | 方式の常設表示が無い(表の列は 役割/番号/線数/接続/方向/元ワイヤー、`V2MainWindow.cpp:617`)。役割表は `ShelfLayout.cpp:85` により部品モードで**後ろの札**。モックの6方式カードに相当するものが無い |
| 11 | Surface non-destructive Preview | **FAIL** | 存在しない。`guide.create`/`guide.build` は直接文書を変える。モックの「プレビューはまだDocumentへ保存されていません」に相当する状態が無い |
| 12 | AUTO order の実採用順が可視か | **FAIL** | 表の番号列は**投入順**。自動/手動固定の切替自体が無い |
| 13 | MANUAL LOCK 順が kernel request へ反映されるか | **FAIL** | 機能が存在しない。`GuideTable` に固定フラグが無く、`ToGuideSurfaceRequest` は `NumberRows` で採番するだけ |
| 14 | Enter/Esc が dock focus に依存しないか | **FAIL** | `V2Viewport.cpp:1319/1389` の `keyPressEvent` のみ。`setFocus()` は `V2ViewportInput.cpp:1135` の mousePress だけ。**窓レベルの QShortcut/QAction が1つも無い**(grep: QShortcut 0件)。右欄に数値を打った直後の Enter は効かない |
| 15 | Human-path tests が実 selection/pick 経路を通るか | **FAIL** | self-test 全体で `SetSelection(` **151回** に対し `SelectAt(` **21回**。その21回は Basics/Pointer/Screen/Semantics のみで、**押し出し・面生成の試験には0回** |
| 16 | invisible widget 直接操作だけで PASS していないか | **FAIL** | している。`window.ExtrudeDock().TypeDistanceMm(…)` / `ChooseBoolean(…)` は**不可視 widget を直接叩く**(Qt は不可視でも値とシグナルが動く)。面選択は4か所で `SelectionRef` を手組みし `pickedFaceIndex = 0`(`ExtrudePromise.cpp:89`/`ExtrudeGraph.cpp:150`/`Facing.cpp:121`/`Modeling.cpp:260`)。窓は `SetExtrudeChooser`/`SetWorkPlaneChooser`/`SetGuideChoiceChooser` で注入。**棚が表示されているかを見る試験は0件** |

## 出力プリセット仕様(追加分)の判定

| # | 項目 | 判定 | 根拠 |
|---|---|---|---|
| O-1 | 出力プリセット5種 | **未実装** | UI・モデルとも存在しない |
| O-2 | 個別4項目 | **部分** | `ExtrudeInput.h:73 ExtrudeOutputSelection` は `part` / `endProfileWire` / `sideBoundaryWires` の3つ。**「開始側の輪郭ワイヤー」が無い** |
| O-3 | 全OFF禁止 + 理由表示 | **部分** | `ExtrudeOptions.cpp:116` EXT-U001 が3項目で判定。開始側を足すと判定漏れになる |
| O-4 | body OFF のとき Boolean を実行しない / 演算欄を無効化 | **未実装** | `booleanMode` は `makePart` と独立。`CommitExtrudeAtomically` は `makePart` に関係なく boolean 分岐へ入る |
| O-5 | Preview が選択中の出力を反映 | **未実装** | `ExtrudePreviewLoops` は常に 輪郭+終端+側面 を描く |

なお **文書オブジェクト生成の土台は既にある**。`AdoptExtrudeResult`(`V2PartCommands.cpp:527-542`)が
`AddPlainWire` で終端輪郭と側面を文書ワイヤーにしており、同じ Transaction の中なので
**Undo は既に atomic**。足りないのは開始側ワイヤーと、プリセット/排他制御/Preview 反映である。

## モックとの構造比較(色・数pxの差は対象外)

**押し出しパネル**
- モック: ヘッダカード(タイトル+状態バッジ+説明) / 1.入力(対象・輪郭の2スロット+選び直す) /
  2.結果(演算・範囲・距離・方向・出力プリセット・出力4チェック) / 3.状態(statusbox) /
  actions(キャンセル・再プレビュー・確定)
- 実装: 同等の欄は `V2ExtrudeDock` に**概ね存在する**(距離・方向・範囲・演算・確定・詳細・選び直す2種)。
  ただし **①パネルが表示されない ②出力プリセット/出力4項目が無い ③「再プレビュー」が無い
  ④入力が2スロットでなく1行の文字列 ⑤状態が statusbox でなく `result_` の1行**。
  → **配置構造が別物ではない。表示されないことと、結果セクションの欠落が Blocking。**

**面パネル**
- モック: 1.作り方(6方式カード・常時可視・推奨バッジ) / 2.入力(断面・ガイド・境界のスロット、
  不要な役割は「この方式では不要」+追加ボタン無効) / 3.断面順(自動/手動固定+番号付き並び) /
  4.状態 / actions
- 実装: 相当するのは「形状ガイドの役割」札の6列テーブル+10個のボタン列。
  **方式カードが無い、方式が常設表示されない、不要役割の明示が無い、順序モードが無い、
  Preview が無い、札が後ろに隠れている。**
  → **こちらは配置構造が別物。** 全4セクションのうち成立しているのは「入力の一覧」だけ。

---

## Blocking 項目(修正の作業指示)

優先順。上3つは他の全部の前提になる。

**BL-1 押し出しパネルを実際に出す**
`ShelfLayout.cpp` の `ShelvesFor()` に押し出し中の状態を渡し、`Shelf::Extrude` を先頭で返す。
`RefreshRightShelves()` が `raise()` する先もそこになる。
現在 `ShelvesFor(mode, tool)` は押し出し中かどうかを知らないので、引数を増やす必要がある。
併せて、棚が出ていることを見る自己試験(`ShelfShown(Shelf::Extrude)`)を足す。

**BL-2 押し出し距離を板厚から分離する**
`ParameterId::ExtrudeDistance`(0.05〜20mm、名称「板厚(押し出しの距離)」)を板厚専用に戻し、
押し出し距離は別パラメータにする。上限は板厚の20mmを引き継がない。
`V2PartDock.cpp:125-126` の二重通知を解く。

**BL-3 Enter/Esc を焦点から独立させる**
窓レベルの QAction(または QShortcut)で 確定/キャンセル を受ける。
いまは `V2Viewport::keyPressEvent` だけなので、右欄に入力した直後は無反応。

**BL-4 出力プリセットと個別出力(新仕様)**
`ExtrudeOutputSelection` に開始側ワイヤーを足し、`ExtrudeChoice` に4項目+プリセットを持たせ、
EXT-U001 を4項目で判定。`AdoptExtrudeResult` に開始側ワイヤーの生成を足す(**元の輪郭は改変しない。
新しい Document Wire を作る**)。`ExtrudePreviewLoops` を出力選択に従わせる。
body OFF のとき boolean を実行せず、演算欄を無効化して理由を出す。
HP-EX-OUTPUT-01〜08 を人の経路で足す。

**BL-5 面を作るの入口を1つにする**
`guide.create` / `guide.add_row` / `guide.build` の3入口を「面を作る」1つへ。
押したら面パネルが出て、方式カード6枚・役割スロット・順序・状態がそこに揃う。

**BL-6 閉じた平面輪郭1本で平面が作れるようにする**
`AutoSectionTable` が1本かつ閉じた平面輪郭のとき `PlanarBoundary` + `OuterBoundary` を選ぶ。
`sections < 2` の一律拒否をやめる。

**BL-7 面生成に non-destructive Preview を入れる**
`guide.build` の前に、文書を変えずに形だけ作って画面へ出す道を用意する
(押し出しの `ExtrudePreviewLoops` 相当)。状態欄に「まだDocumentへ保存されていません」を出す。

**BL-8 断面順の自動/手動固定**
`GuideTable` に順序モードを持たせ、手動固定なら表の並びを `ToGuideSurfaceRequest` の
生成順へそのまま渡す。自動のときは**実際に採用された順**を表の番号欄へ書き戻す。

**BL-9 選択フィルタがコマンド文脈を見る**
押し出し中は面を、面生成中は線を優先するよう `CollectCandidatesAt` の並べ替えに文脈を渡す。

**BL-10 Human-path test を実経路にする**
押し出し・面生成の試験を `viewport.SelectAt(...)` で選ぶよう書き換える。
面は `pickedFaceIndex` を手で入れず、`CollectShapeCandidatesAt` を通す。
棚が**見えているか**を見る確認を各試験に足す。
