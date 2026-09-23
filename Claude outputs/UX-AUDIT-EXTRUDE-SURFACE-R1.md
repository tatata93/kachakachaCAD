# UX-AUDIT-EXTRUDE-SURFACE-R1

AUDIT_ID: UX-AUDIT-EXTRUDE-SURFACE-R1
HEAD: 9b39417 (`docs:` のみ。挙動は 0acea2d と同一。PC が最後に組んだのは 0acea2d)
PC_BUILD: PASS (`build_rc=0`, 2026-09-15T06:06Z の `_claudeout/run.txt`)
PC_SELF_TEST: PASS (ctest 141/141、cad_next self-test 230/230、review pipeline self-test 106/106)

## この監査でできたこと・できなかったこと

**できなかった**: 実機でのマウス駆動。`kachakacha_cad_next.exe` はスタートメニューに登録
されていないため、computer-use が対象アプリとして解決できない
(`computer_resolve_access` → `notInstalled`)。当セッションに PC のシェル(device_bash)も
無い。許可リスト外のウィンドウはスクリーンショットでも黒塗りされるため、
起動しても画面が取れない。

**できたこと**:
- PC が自分で生成した**実スクリーンショット4枚**を回収して読んだ(下記 SCREENSHOTS)。
- GUI → Core の実経路を、実在のファイル・関数・状態で追跡した。
- 操作列とクリック数は、コード経路(コマンド → 述語 → 状態遷移)から確定した。
  **実際に指で押して確かめた数ではない**ので、そのように読んでほしい。

以下、**[コード]** は実装から確定した事実、**[画面]** は実スクリーンショットからの観察、
**[導出]** はコード経路から導いた操作列である。推測は書いていない。

---

## EXTRUDE_CURRENT_WORKFLOW

前提: 押し出しは **部品モードでしか出ない**。`part.extrude` の QAction は
`RefreshCommandVisibility()`(V2MainWindow.cpp:467)で `CommandVisibleInMode` により
作図モードでは `setVisible(false)` される。**ショートカット Shift+E も効かない**
(不可視の QAction はショートカットが発火しない)。[コード]

### EX-A 閉じた輪郭だけ → 新規Solid

[導出] 矩形を描いた直後(作図モード・選択道具)から:

1. モードバーの「部品」をクリック → 右の棚が「部品」+「形状ガイドの役割」の2枚に替わる [画面 v2-mode-part.png]
2. 3Dで矩形の線をクリック → `CollectCandidatesAt` の優先順は **点 → 線 → 形**
   (V2ViewportInput.cpp:929)。線が取れる。選択は `ApplySelection`(Selection.cpp:386)で Replace。
3. 「押し出し」を押す(上帯 または 部品の棚のボタン)
4. `RunExtrude`(V2PartCommands.cpp:160) → `PlanExtrudeFromSelection` → `ProfileOnly` →
   `BeginExtrudePreview`
5. 3D に矢印と破線が出る。**距離は 0.5mm**(`ExtrudeChoice::distanceMm = 0.5`、
   かつ `ParameterId::ExtrudeDistance` の既定が 0.5)
6. **右には押し出しの棚が出ない**(下記 BUGS #1)。距離・方向・範囲・演算・確定・
   詳細...・選び直す は**どれも画面に出ない**
7. 距離を変える手は2つだけ:
   - 矢印をドラッグ(掴める太さ 12px、V2ExtrudeHandle.cpp)
   - 部品の棚の「押し出しの距離」欄に打つ → **20mm を超えると UI-P002 で断られる**(下記 BUGS #2)
8. Enter で確定。ただし Enter は `V2Viewport::keyPressEvent`(V2Viewport.cpp:1389)なので
   **3D画面にキーボード焦点が要る**。距離欄に打った直後は焦点が欄にあるので Enter は効かない。
   3Dを1回クリックして戻す必要があるが、そのクリックで選択が変わる。
   代替として「押し出し」をもう一度押すと確定になる(V2PartCommands.cpp:186)
9. 立体ができる

**迷う場所**: 5 と 6。押した結果が 0.5mm の破線なので、100mm の矩形では**線が太くなった
ようにしか見えない**。棚も出ないので「何も起きなかった」と読める。

### EX-B Solid + Wire/輪郭 → Cut

[導出]
1. 立体をクリック(形の候補、`SelectionElementKind::Object` または `Face`)
2. **Ctrl+クリック**で輪郭を足す(Ctrl だけが追加。Shift は作図拘束、Alt は奥候補。
   V2ViewportInput.cpp:786 `SelectionModeFor`)
3. 「押し出し」 → `SolidAndProfile` → `defaultOperation = SubtractFromPart`
   (ExtrudePlan.cpp:99)。**既定で切削になる**
4. 矢印 + 破線。距離 0.5mm
5. Enter

**問題**: 演算(足す/引く/新しい部品)は押し出しの棚の `boolean_` コンボにしかない。
その棚が出ないので、**利用者は「いま引くのか足すのか」を見ることも変えることもできない**。
上帯の「足す」「引く」は別コマンド(`part.boolean_add` / `part.boolean_cut`、述語 `TwoParts`)で、
押し出しの演算ではない。

### EX-C SolidのFace → Push/Pull相当

[コード] 面は拾える。`CollectShapeCandidatesAt`(V2ViewportInput.cpp:714)が
`CollectFaceHits` を呼び、`hit.faceIndex != kNoFaceIndex` なら
`SelectionElementKind::Face` + `pickedFaceIndex` を持つ候補を作る。面番号は
`OcctTessellate.cpp:104` が三角形ごとに付けている。

[導出]
1. 立体の平らな面をクリック。ただし **点 → 線 → 形** の順なので、
   その面の上に線(例: その立体を作った元の矩形)が載っていると**線が先に取れる**。
   奥へ送るには Tab、または右クリックで候補一覧。
2. 「押し出し」 → `SolidAndFace` → `PickFaceProfile()`(V2FacePushPull.cpp:64)が
   面の縁を**その場限りの輪郭**として取り出す。文書はまだ変えない
3. `facePushPull_ = true`。向きは面の外向き法線に固定。押し出しの棚では
   方向欄が隠される(`ApplyRows`)— **もっとも棚自体が出ない**
4. 距離 0.5mm → Enter

**注意**: `V2ExtrudePlanCommand.cpp:69-72` のコメントは
「いまの画面はまだ面を拾えない(拾えるのは物体まで)」と書いてあるが、**これは古い**。
拾える。コメントと実装が食い違っている。

### EX-D Solidだけ選択 → 押し出し開始 → 後から輪郭/Faceを選択

[コード] これは**動く。ただし説明されない。**

- 立体だけの選択では述語 `ClosedProfilesOrSolidFace`
  (`closedProfiles >= 1 || (parts == 1 && solidFaces >= 1)`、CommandAvailability.cpp:43)が
  **偽**になる。
- しかし `RunCommand` は `ArmCommandIfUnsatisfied`(V2PendingCommand.cpp:28)で**構えて待つ**。
  帯に「押し出し: 閉じた輪郭を1つ以上、または立体の平らな面を選んでください。
  (選ぶと続きます。Esc でやめます)」と出る。
- 輪郭を Ctrl+クリックで足す → `RefreshPendingCommand` → 述語は「1つ以上」型なので
  `PendingAction::NeedsConfirm` → 「押し出し: 2個選びました。Enter で実行、
  続けて選んでも構いません。」
- **Enter** で初めて `RunCommand("part.extrude")` が走る → 下見が出る
- **もう一度 Enter** で確定

つまり Enter を2回押す。1回目は「構えの実行」、2回目は「下見の確定」。
帯の文言はどちらも違うが、**同じキーで意味が変わる**ことは画面のどこにも書かれていない。

なお `ExtrudePlan` には `SolidOnly` という段(「押し出す面または輪郭を選んでください。」)が
実装されているが、述語が先に偽を返して構え文言が出るため、**この `needsJa` は
GUI からは決して表示されない**。同じ意味の文が2か所にあり、片方は死んでいる。

---

## EXTRUDE_CODE_PATH

Selection:
- `V2Viewport::mousePressEvent`(V2ViewportInput.cpp:1133、先頭で `setFocus()`)
- → `RefreshPickCycle` → `CollectCandidatesAt`(V2ViewportInput.cpp:929)
  = `app::CollectPickCandidates`(点・線) + `CollectShapeCandidatesAt`(形)。**順は 点 → 線 → 形**
- → `app::ApplySelection`(Selection.cpp:386) / `SelectionMode` は
  `SelectionModeFor`(Ctrl=Toggle、それ以外=Replace)
- → `app::SelectionFromRefs`(Selection.cpp:67)。`ordered` が押した順、
  `entityIds` は重複を除いた物体の並び
- 面は `ordered` にしか出ない。`RefFromCandidate` が `pickedFaceIndex` を持ち越す

Interpretation:
- `V2MainWindow::PlanExtrudeFromSelection`(V2ExtrudePlanCommand.cpp:25)
  — `entityIds` から Part / GuideSurface / Wire を数え、Wire は
  `geometry::SegmentsFormClosedLoop` で閉/開を判定。面は `ordered` から数える
- → `app::PlanExtrude`(ExtrudePlan.cpp:35) が A〜E を決める
- 押せるかどうかは**別経路**: `app::SelectionFactsFrom`(CommandAvailability.cpp:110)
  → `SelectionPredicate::ClosedProfilesOrSolidFace`。
  **同じ選択を2つの関数が別々に数えている**(`ExtrudeSelectionFacts` と `SelectionFacts`)

Plan:
- `V2MainWindow::RunExtrude`(V2PartCommands.cpp:160)
- 面のときだけ `PickFaceProfile()` → `kernel::FaceBoundaryOf` で縁を取り、`facePushPull_ = true`
- `BeginExtrudePreview()`(V2ExtrudeInteractive.cpp:57)
- → `ShowExtrudeShelf(plan)`(V2ExtrudeInteractive.cpp:250 付近) → **`RefreshRightShelves()` が
  直後にその棚を隠す**(BUGS #1)

Preview:
- `ExtrudeDirectionNow()` → `ExtrudeBaseDirectionNow()` → `ExtrudeDirectionForMode()`
  (V2ExtrudeInteractive.cpp:133)。**7通りの向きを1か所で解く**(単位長で返す)
- `ExtrudePreviewLoops(distanceMm)`(V2ExtrudeInteractive.cpp:199)。
  loops[0]=いまの輪郭、loops[1]=押し出した先、以降=側面の線(12本に間引き)
- 矢印ドラッグ → `SetExtrudeDistanceCallback` → `UpdateExtrudePreview`
  → `parameterDock_->Apply(ExtrudeDistance, ...)`(ここで 0.05〜20mm の検査)

Commit:
- `ConfirmExtrude`(V2PartCommands.cpp:287)
- → `PrepareExtrudeChoice`(V2PartCommands.cpp:212) → `PreparedExtrudeChoice{remembered, resolved}`
- → `app::ValidateExtrudeChoice`(EXT-U001〜U006)
- → `app::ToExtrudeRequest` → `modeling::AnalyzeExtrudeRequest` → カーネル
- **Preview入力と Commit入力は一致する**: 距離は両方 `viewport_->ExtrudeHandleDistanceMm()`、
  向きは両方 `ExtrudeDirectionForMode()`(「輪郭に垂直」だけ CustomXYZ へ畳んで渡す)。
  ここは直近の R5〜R7 で揃えた。**いまは食い違っていない。**

---

## SURFACE_CURRENT_WORKFLOW

入口は2つある。**片方(おまかせ)しか上帯に出ていない。**

- `guide.create`「形状ガイド」= おまかせ。選んだ線を**全部 Section 役**にして並べる
- 役割表(`guide.set_method` → `guide.add_row` → `guide.build`)= 役割を自分で決める道

### SF-A 閉じた平面輪郭1つ → 平面Face/Surface

[コード] **「形状ガイド」では作れない。** `CreateGuideSurfaceFromSelection`
(V2GuideCommands.cpp:261)は `made.sections < 2` で
「断面が2つ以上要ります(いまは1つ)。線を2本以上選んでください。」と断る。
`AutoSectionTable`(GuideTableBuild.cpp:56)は役割を常に `Section` にし、
方式を 2本=Ruled / 3本以上=Loft に決める。**`PlanarBoundary` には決して到達しない。**

役割表の道なら作れる [導出]:
1. 右の棚の**「形状ガイドの役割」タブをクリック**(既定では「部品」タブが前面。
   `ShelfLayout.cpp:85` が `{Part, GuideTable}` を返し、先頭の Part を `raise()` する)
2. 「面の作り方」→ 一覧から「平面」を選ぶ(表の既定は `LoftSections`、
   GuideSurfaceTable.h:54)
3. 輪郭を選ぶ
4. 「選択を表へ」→ 役割の窓が出る(平面は役割が2つ=外形/穴 なので聞かれる)→「外形」
5. 「表から面を作る」

### SF-B 複数断面Wire → ThruSections Loft

[導出] **これは素直に通る唯一の道。**
1. 線を2本以上 Ctrl+クリック
2. 「形状ガイド」を押す
3. 面ができる(2本→Ruled、3本以上→Loft。**どちらになるかは選べない。断面の本数で決まる**)

### SF-C 複数断面Wire + Guide → Guided Loft

[導出] 役割表でしか作れない。
1. 「形状ガイドの役割」タブ
2. 「面の作り方」→「案内付きロフト」
3. 断面を選ぶ → 「選択を表へ」→ 役割の窓 →「断面」
4. 案内線を選ぶ → 「選択を表へ」→ 役割の窓 →「外形U」
5. 「表から面を作る」

注意: `AddSelectionToGuideTable`(V2GuideTableCommands.cpp:147)は**役割を1回だけ聞いて、
選択中の全部の線に同じ役割を当てる**。断面と案内を1回では入れられない。

### SF-D 開いた線/境界 → Fill/Boundary系Surface

[導出] 同じく役割表。「面の作り方」→「境界埋め」→ 役割は「境界辺」1種類なので
窓は出ない → 線を選んで「選択を表へ」→「表から面を作る」。

**カーネル側は8方式すべて実装されている**(OcctGuideSurface.cpp:726-750:
PlanarBoundary / RuledSections / LoftSections / GuidedLoft / GordonNetwork /
BoundaryFill / OffsetGuide / Revolve)。**壊れているのではなく、届かない。**

---

## SURFACE_CODE_PATH

Selection: `viewport_->Selection().entityIds` をそのまま読む。
`GuideSelectionOf`(GuideTableBuild.cpp)で Wire → `GuideTableSelection` に変換。
**Edge 単位の選択は使わない。ワイヤー1本=1行。**

Role assignment:
- おまかせ: `AutoSectionTable` が**無条件に `ChainRole::Section`**
- 表: `AddSelectionToGuideTable` が `RolesForMethod(guideTable_.method)`
  (GuideSurfaceTable.cpp:58)から選ばせる。役割が1つなら聞かない
- `AddSelectionAsNewRow`(GuideSurfaceTable.cpp:204)が UI-R003(方式が使わない役割)
  / UI-R004(線が無い)/ UI-R006(同じワイヤーが2行)を返す

Ordering: 行は `viewport_->Selection().entityIds` の並び順に追加される。
これは `SelectionFromRefs` が `ordered`(押した順)から作るので、**クリック順=断面順**。
ただし**矩形選択では `ApplyBoxSelection` が `shapeViews_`/scene の並びになる**ので、
クリック順の意味は無くなる。

Method selection: `SetGuideTableMethod`(GuideSurfaceTable.cpp:363)。
**その方式が使わない役割の行が1行でも残っていると UI-R003 で断る。**
= 方式は**行を入れる前に**決めなければならない。順番を間違えると
「先に消してから作り方を変えてください」に当たる。

Preview: **無い。** 面生成には下見が無い。`guide.build` / `guide.create` は
一発で文書を変える。押し出しにはある矢印+破線が、面には無い。

Commit: `BuildSurfaceFromTable` → `modeling::ToGuideSurfaceRequest`
(足りない役割は UI-R009)→ `kernel::BuildGuideSurface` → `AdoptGuideSurface`。

---

## SELECTION_PROBLEMS

* **面の上の線が面より先に取れる。** 拾う順が 点 → 線 → 形(V2ViewportInput.cpp:922 のコメント
  「形をいちばん後ろに置くのは、線の上を押したのに塗りが勝つと、面の内側にある線が
  永久に掴めなくなるため」)。押し出しで作った立体の底面には元の矩形が載っているので、
  そこをクリックすると**面ではなく線が選ばれる**。面を取るには Tab で送るか右クリック一覧。
  この送りは**帯にしか出ない**。
* **Alt は「奥候補へ予約」とコメントにあるが実装されていない。**
  `SelectionModeFor`(V2ViewportInput.cpp:786)は Ctrl しか見ない。奥へ行く手は Tab と右クリック。
* **同じ選択を2つの関数が別々に数える。** `PlanExtrudeFromSelection`(押し出しの読み取り)と
  `SelectionFactsFrom`(ボタンの可否)。閉判定はどちらも `SegmentsFormClosedLoop` を通すので
  いまは一致するが、一致は**偶然の同期**であって仕組みで保証されていない。
* **矩形選択は順序を持たない。** 面生成の断面順がクリック順に依存するのに、
  矩形で選ぶと順序が scene 並びになる。画面はそれを区別して見せない。
* **木(左の一覧)からの選択と 3D 選択の差**: `HighlightTreeForSelection` は
  3D→木の一方向。木で選んだときに `ordered` に面や押した順が入らない。

## RIGHT_PANEL_PROBLEMS

* **押し出しの棚が一度も表示されない**(BUGS #1)。距離・方向・範囲・演算・確定・
  詳細...・対象を選び直す・輪郭を選び直す・読み取り結果の文・「あと何が要るか」— 全部見えない。
* **役割表は既定で後ろの札。** 部品モードの先頭は `Part`(ShelfLayout.cpp:85)。
  `guide.add_row` を押しても `guideDock_->raise()` は呼ばれない。
  行を足したのに**表は前に出てこない**。
* **いまの「面の作り方」がどこにも常設表示されていない。** 表の列は
  役割/番号/線数/接続/方向/元ワイヤーの6つ(V2MainWindow.cpp:617)。方式は入っていない。
  「面の作り方」または「表を空にする」を押した直後の帯にしか出ない。
* **「あと何が足りないか」は下の「知らせ」に診断コード付きで出る**
  (`UI-R009 断面が1つも入っていません。`、V2MainWindow.cpp:1126)。表の隣ではない。
* **「押し出しの距離」と「板厚」が同じ数**(V2PartDock.cpp:125-126 がどちらも
  `ParameterId::ExtrudeDistance` へ通知)。2行あるが必ず連動する。
* [画面] **手順パネルの列が潰れて読めない**。「1 入力…」「2 方向 前の段…」のように
  6文字ほどで切れている(v2-mode-part.png 左下)。

## PREVIEW_PROBLEMS

* **既定の押し出し距離が 0.5mm。** `ParameterDefinitions()`(CommandParameters.cpp:21)の
  既定が 0.5、`ExtrudeChoice::distanceMm` も 0.5。100〜200mm の図に対して 0.5mm の
  破線は**見えない**。「押したのに何も起きない」の最有力候補。
* **0.05〜20mm の外へ出すと毎フレーム UI-P002 が「知らせ」に積まれる**(BUGS #2)。
  下見と確定形状は 20mm を超えて出来るのに、エラーが出続ける。
* **面生成には下見が無い。** 押し出しには矢印と破線があるが、
  `guide.create` / `guide.build` はいきなり文書を変える。失敗して初めて理由が出る。
* 押し出しの破線は輪郭2本+側面12本だけ(`ExtrudePreviewLoops`)。塗りは出ない。

## TOOL_STATE_PROBLEMS

* **Enter の意味が状況で変わる**: 構えた命令の実行 / 下見の確定。
  どちらになるかは `extrudeHandle_.shown` で決まる(V2Viewport.cpp:1389)が、画面に説明が無い。
* **Enter は 3D画面に焦点があるときだけ効く。** 右の欄に数字を打った直後は効かない。
  焦点を戻すには 3D をクリックするしかなく、そのクリックで選択が変わる。
  `setFocus()` は `mousePressEvent` にしかない(V2ViewportInput.cpp:1135)。
* **押し出しは選択先行が基本で、道具として構え続けない。** 構えの仕組み
  (`pendingCommandId_`)はあるが、下見が出た時点で構えは解けている。
* **部品の棚のボタンは常に押せる見た目**(`MakeRun` が素の QPushButton を作るだけ、
  V2PartDock.cpp:31)。これは「構えて待つ」設計と整合しており**バグではない**が、
  [画面] 何も選んでいない状態でも9個のボタンが同じ見た目で並ぶので、
  どれが今できるのかは画面から読めない。
* **`facePushPull_` は `EndExtrudePreview` でしか降りない。** 面の押し引きを
  途中でやめずに別の操作へ移ると、次のふつうの押し出しが前の面の向きへ進む余地がある
  (`RunExtrude` は `!viewport_->ExtrudeHandleShown()` のときだけ立て直す)。

## SELF_TEST_VS_HUMAN_UI_GAP

* **選択はほぼ全部迂回している。** self-test 全体で
  `SetSelection(` 151 回に対し、実際に拾う `SelectAt(` は **21 回**。
  そしてその21回は `V2SelfTestBasics` / `Pointer` / `Screen` / `Semantics` にしか無い。
  **押し出しと面生成の試験には `SelectAt` が1回も無い。**
  典型は `viewport.SetSelection(app::SelectAllOfKind(snapshot, EntityKind::Wire))` —
  **文書から種類で全部拾う**。人が3Dで線をクリックする経路(`CollectCandidatesAt` の
  点→線→形 の優先順)を一度も通らない。
* **面の選択は手で構造体を組んでいる。** 4か所
  (`V2SelfTestExtrudePromise.cpp:89` / `ExtrudeGraph.cpp:150` / `Facing.cpp:121` /
  `Modeling.cpp:260`)で `SelectionRef` を直接作り `face.pickedFaceIndex = 0;` と書いている。
  **`CollectFaceHits` も `CollectShapeCandidatesAt` も走らない。**
  面を指でクリックして押し引きできるかは、試験されていない。
* **窓の答えは注入している。** `SetExtrudeChooser` / `SetWorkPlaneChooser` /
  `SetGuideChoiceChooser` でダイアログを飛ばす。
* **棚は widget を直接叩いている。** `window.ExtrudeDock().TypeDistanceMm(4.0)` /
  `ChooseBoolean(...)`。**Qt の widget は不可視でも値を持ち、シグナルも出す。**
  これが「self-test 230件 PASS なのに、人には棚が1枚も見えない」の直接の理由である。
  棚が表示されているかを見る試験は1つも無い(`Shelf::Extrude` は
  `DockForShelf` の1か所にしか現れない)。
* **吸着の抑止も直接呼んでいる**(`viewport.SetSnapSuppressed(true)`)。
  コメントには「人が引くときも Ctrl を押して同じことをする」とあるが、その Ctrl 経路は試験していない。

## CLICK_COUNTS

[導出] いずれも**必要形状が既に描いてあり、部品モードに居る**状態から数えた。
モード切替が要るなら +1。

Simple Extrude (EX-A): **5**
 輪郭クリック1 + 押し出し1 + 矢印ドラッグ1 + 3Dへ焦点戻し1 + Enter1
 (距離を欄に打つ場合は クリック1+入力+Enter1 で +1、かつ 20mm 上限)

Solid+Wire Cut (EX-B): **5**
 立体クリック1 + Ctrl+輪郭1 + 押し出し1 + 距離1 + Enter1
 — ただし**演算を確認する手段が無い**

Face Push/Pull (EX-C): **4〜6**
 面クリック1(線が先に取れる場合 Tab で送り +1〜) + 押し出し1 + 距離1 + Enter1

Solid先行 (EX-D): **6**
 立体1 + 押し出し1(構え) + Ctrl+輪郭1 + Enter1(実行) + 距離1 + Enter1(確定)

ThruSections (SF-B): **3**
 線1 + Ctrl+線1 + 形状ガイド1  ← **いちばん短い。面生成で唯一素直な道**

Planar face (SF-A): **約10**
 タブ1 + 面の作り方1 + 窓で選ぶ2 + 輪郭1 + 選択を表へ1 + 役割窓2 + 表から面を作る1
 (先に「形状ガイド」を押して断られる回り道を含めると +2)

Guided Loft (SF-C): **約15**
 タブ1 + 面の作り方1 + 窓2 + 断面選択2 + 選択を表へ1 + 役割窓2 + 案内選択2 +
 選択を表へ1 + 役割窓2 + 表から面を作る1

**定量的な言い方**: 押し出しは 4〜6 操作で、そのうち**2操作(距離と確定)が
見えない棚か焦点の都合に依存している**。面生成は、方式を事前に知っていれば 3 操作、
知らなければ 10〜15 操作。**方式名(平面/ルールド/ロフト/案内付きロフト/曲線網/境界埋め/
離した面/回転体)を先に知らないと、SF-B 以外は始められない。**

## TOP_10_HUMAN_BLOCKERS

1. **押し出しの棚が一度も画面に出ない。** 距離・方向・範囲・演算・確定ボタン・
   「詳細...」・「選び直す」・読み取り結果の文が、すべて見えない。押し出し中に
   利用者が触れるのは「矢印」「部品の棚の距離欄」「Enter」の3つだけ。
2. **既定距離 0.5mm。** 押した瞬間の下見が、模型の寸法に対して見えない大きさ。
   「押したのに何も起きない」と読める。
3. **距離が 20mm で頭打ち**(欄に打つ場合)。名前が「板厚(押し出しの距離)」で、
   板厚の範囲が押し出し距離の範囲を兼ねている。車体1つ分を押せない。
4. **20mm を超えて矢印を引くと、成功しているのにエラーが積まれる。**
   下見も確定形状も出来るのに「知らせ」に UI-P002 が並ぶ。
5. **Enter が 3D に焦点が無いと効かない。** 距離を打った直後の Enter が無反応。
   確定ボタンは棚にあるが、その棚が出ない。
6. **面を選びたいのに線が取れる。** 拾う優先順が線優先。送りは Tab か右クリックで、
   帯にしか書かれていない。
7. **1枚の閉じた輪郭から平面を作れない**(おまかせでは)。「線を2本以上選んでください」と
   断られる。平面を作るには役割表と「面の作り方」を知っている必要がある。
8. **「面の作り方」を先に決めないと役割が選べず、後から変えると行が邪魔で断られる。**
   正しい順を知らないと UI-R003 に当たる。
9. **役割表が既定で後ろの札。** 行を足しても表は前に出ない。
   いまの方式も表に出ていない。
10. **面生成に下見が無い。** 通るか通らないかは、文書を変えてから分かる。

## BUGS

**#1 押し出しの棚が構造的に表示されない**(確実)
- `ShelfLayout.cpp:38` の `ShelvesFor(mode, tool)` は **`Shelf::Extrude` を決して返さない**。
  部品モードは `{Shelf::Part, Shelf::GuideTable}` を返す。
- `AllShelves()`(ShelfLayout.cpp:30)には `Shelf::Extrude` が入っている。
- `V2MainWindow::RefreshRightShelves()`(V2ViewCommands.cpp:811)は `AllShelves()` を回して
  `dock->setVisible(shelf ∈ wanted)` する。つまり **`extrudeDock_->setVisible(false)` を毎回呼ぶ**。
- `ShowExtrudeShelf()`(V2ExtrudeInteractive.cpp)は棚に値を入れたあと
  `extrudeShelfShown_ = true; RefreshRightShelves();` で終わる。
  **いま埋めた棚を、その場で自分で隠している。**
- `extrudeDock_` に対する `setVisible(true)` も `raise()` も**コード中に1つも無い**
  (grep: `V2Shelves.cpp` の生成・addDockWidget・tabify と `DockForShelf` のみ)。
- [画面] 裏付け: v2-mode-part.png / v2-guide-table.png の右下の札は
  「形状ガイドの役割」と「部品」の**2枚だけ**。押し出しの札は無い。
- なぜ試験が通るか: self-test は `window.ExtrudeDock().TypeDistanceMm(...)` のように
  **widget を直接叩く**。不可視の widget でも値とシグナルは動く。

**#2 押し出し距離の上限20mmと、そこを越えたときのエラー洪水**(確実)
- `ParameterDefinitions()`(CommandParameters.cpp:21):
  `{ExtrudeDistance, "extrude_distance", "板厚(押し出しの距離)", Length, 既定0.5, 最小0.05, 最大20.0}`
- `UpdateExtrudePreview(d)`(V2ExtrudeInteractive.cpp:222)は先頭で
  `parameterDock_->Apply(ExtrudeDistance, d)` を呼ぶ。
- `SetParameter`(CommandParameters.cpp:122)は範囲外を **UI-P002 で断り、寄せない**。
  `V2ParameterDock::Apply` は診断を sink へ流して `Refresh()` で前の値へ戻す。
- `UpdateExtrudePreview` は**戻り値を見ずに先へ進む**ので、矢印と下見と確定形状は
  20mm を超えて出来る。→ **成功しているのにエラーが出る**、かつ
  部品の棚の距離欄と矢印の距離が食い違ったままになる。
- ドラッグ中は毎フレーム呼ばれるので、「知らせ」に UI-P002 が積み上がる。

**#3 死んだ案内文**(軽微・確実)
- `ExtrudePlan` の `SolidOnly` 段(「押し出す面または輪郭を選んでください。」、
  ExtrudePlan.cpp:105)は、述語が先に偽を返して `ArmCommandIfUnsatisfied` の
  文言が出るため、**GUI からは到達しない**。

**#4 古いコメントが実装と食い違っている**(軽微・確実)
- `V2ExtrudePlanCommand.cpp:69-72`「いまの画面はまだ面を拾えない(拾えるのは物体まで)」。
  実際は `CollectShapeCandidatesAt` が面を拾う。
- `V2ViewportInput.cpp:786` のコメント「Alt は奥候補へ予約する」。Alt の実装は無い。
- `ExtrudeTargets()`(V2PartCommands.cpp:139)「部品の1つの面はまだ選べないので」。同様に古い。

## UX_DESIGN_PROBLEMS

バグではないが操作を難しくしているもの。

* 押し出しは**部品モード専用**。作図で矩形を描いた直後に押せない。ショートカットも死ぬ。
* **「板厚」と「押し出しの距離」が同じ数**。板材CADとしては筋が通っているが、
  「箱を作る」ときに板厚欄が一緒に動く。
* **面生成の入口が2つあり、能力が違う。** 上帯の「形状ガイド」は Section 役しか作れない。
  残り6方式は役割表からしか届かないのに、役割表は後ろの札。
* **方式を先に決める順序拘束**(UI-R003)。人は普通「線を入れてから方式を試す」。
* **断面順=クリック順**という決まりが画面に書かれていない。番号列に出るのは入れた後。
* **「あと何が要るか」が診断コード付きで下の「知らせ」に出る。** 表の隣ではない。
* **手順パネルが狭すぎて読めない**(列が6文字で切れる)。
  「1 入力 / 2 方向 / 3 終端 / 4 出力 / 5 部品演算 / 6 結果」が
  「入力軌」「方向」「終端」…としか読めない。
* **面生成に下見が無い**ので、押し出しとリズムが揃わない。
* **Enter の二重の意味**(構えの実行 / 下見の確定)。
* **右クリック候補一覧と Tab 送り**が、線と面の取り合いを解く唯一の手なのに、帯でしか案内されない。

## CURRENTLY_IMPOSSIBLE_OPERATIONS

**カーネルの制約で不可能なものは無い。** 8方式すべて `BuildGuideSurface` に実装がある。
GUI から**現実的に到達できない/完遂できない**のは次のとおり。

* **押し出し中に演算(足す/引く/新規)を見る・変える** — 棚が出ないため不可能。
  既定(立体+輪郭=引く、立体+面=足す)のまま作るしかない。
* **押し出しの終端を「ある面まで」「両方向に別々の距離」「全部貫く」にする** —
  棚の「範囲」と「詳細...」からしか選べず、どちらも出ない。
  `ExtrudeExtentMode` 5通りのうち GUI から届くのは Distance のみ。
* **押し出しの向きを X/Y/Z・選んだ線・数値で決める** — 同上。棚の方向欄と
  詳細ダイアログが出ないため、実際に使えるのは「輪郭に垂直」の既定だけ。
* **押し出しの出力(輪郭ワイヤー・側面ワイヤーを作るか)を選ぶ** — 詳細ダイアログのみ。到達不可。
* **「対象を選び直す」「輪郭を選び直す」** — 棚のボタンのみ。到達不可。
* **20mm を超える押し出しを、エラーを出さずに行う** — 不可能(矢印ドラッグなら形は出来る)。
* **矩形選択で断面順を意図どおりにする** — 順序が scene 並びになるため不可能。

## SCREENSHOTS

PC が `_FIX_AND_BUILD.cmd` 手順[5]で自分で撮ったもの。HEAD 0acea2d 時点。
新規撮影は不可だった(冒頭の理由)。

* `_claudeout/v2-mode-part.png` — 部品モード。右の札が「形状ガイドの役割」「部品」の
  2枚だけであること、押し出しの札が無いこと、9個のボタンが同じ見た目であること、
  「押し出しの距離 0.500 mm」「板厚 0.500 mm」が別行で同値であることが見える。
* `_claudeout/v2-guide-table.png` — 役割表に4行(外形U1/外形U2/断面1/断面2)入った状態。
  **それでも前面は「部品」の札**で、表は見えていない。3Dのラベルだけが出ている。
* `_claudeout/v2-guide.png` — 作図モード。上帯に押し出しが無いこと、
  手順パネルの列が潰れて読めないことが見える。
* `_claudeout/v2-steps-part.png` — 部品モードの手順。

**押し出し中のスクリーンショットは存在しない。** `V2ManualStates.cpp` の撮影状態一覧
(empty / curves / draw-line / select / snap / grid / tools / guide / guide-table /
mode-part / mode-fabrication / mode-output / steps-* / export / sample / railway-nose /
isometric / view-cube / active-group / cursor-input / win95 / 30deg)に
**押し出しの状態が1つも無い**。これ自体が、押し出しの画面が一度も目視確認されていない
ことの傍証である。

## CONCLUSION

オーナーが押し出し・面生成をテストできない理由。

1. **押し出しの操作盤が画面に出ていない。** `ShelvesFor` が `Shelf::Extrude` を返さず、
   `RefreshRightShelves` がそれを毎回隠す。距離・方向・範囲・演算・確定・詳細・選び直しが
   すべて不可視。利用者に残されているのは矢印と Enter だけである。
2. **押し出した結果が見えない。** 既定 0.5mm。模型の寸法に対して下見が線の太さと区別できない。
3. **距離を大きくしようとすると壁に当たる。** 欄は 20mm で断られ、矢印で越えると
   成功しながらエラーが積まれる。押し出し距離と板厚が同じ数であることが原因。
4. **確定キーが効かないことがある。** Enter は 3D に焦点があるときだけ。
   代わりの確定ボタンは、出ない棚の上にある。
5. **面を選ぼうとすると線が選ばれる。** 拾う順が線優先で、面へ送る手(Tab / 右クリック)は
   帯にしか書かれていない。
6. **面生成は、方式名を先に知らないと始まらない。** 素直に通るのは
   「線を2本以上選んで形状ガイド」の1本道だけ。残り6方式は、後ろの札にある役割表と
   「面の作り方 → 役割 → 表から面を作る」という順序拘束を知っている必要がある。
7. **1枚の閉じた輪郭が平面にならない。** おまかせは「2本以上」と断る。
   もっとも素朴な操作が、もっとも遠い道になっている。
8. **面生成には下見が無い。** 押し出しと操作のリズムが違い、失敗は作ってから分かる。
9. **self-test 230件が通るのは、人の経路を通っていないから。**
   選択は `SetSelection(SelectAllOfKind(...))` で文書から直接拾い(151 : 21)、
   面は `pickedFaceIndex = 0` を手で書き、棚は不可視のまま widget を直接叩いている。
   **「棚が見えているか」を見る試験が1つも無い。**
10. **画面に出ている情報が足りない。** いまの方式・断面順の決まり・次に何を選ぶべきか・
    あと何が足りないか(診断コード付きで下の「知らせ」)・確定すると何が起きるか、
    が右の棚に揃っていない。内部では全部決まっているのに、見えていない。
