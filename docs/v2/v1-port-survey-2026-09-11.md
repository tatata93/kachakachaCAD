# V1 → V2 移植の走査(2026-09-11)

オーナー指示「作業に入る前に、V1 から完全移植する部分と部分移植する部分を、
V1 の内容と現状の V2 の内容を走査して示せ。現状の V2 の不足もきちんと調べろ」への答え。

走査したもの:
- V1 画面: `src/apps/cad/MainWindow.h`(部品 252 個)、`MainWindow.cpp`(道具・上部バー・一覧)、
  `MainWindowPanels.cpp`(右パネル)、`MainWindowMeasure.cpp`、`CadViewport.cpp`(線の描き方)
- V1 保存形式: `src/core/kachakacha/io/ProjectScript.cpp`(.kcd の命令 58 種)、
  `examples/railway-nose-acceptance.kcd`
- V2 画面: `src/apps/cad_next/*`、V2 core: `src/next/kachakacha/{app,modeling,geometry}`
- PC の実画面(`_claudeout/v2-*.png`、作図・部品モードなど)

「完全移植」= V1 の操作・項目をそのまま V2 に持つ。「部分移植」= V2 の設計(Feature 履歴・
役割表・近似モデル)に合わせて置き換える。「V2 の不足」= V1 に有って V2 に無い、または
core に有るのに画面から使えないもの。

---

## 1. 完全移植する部分(作図の仕様は V1 で完成に近い ── そのまま持つ)

| # | V1 の内容(場所) | 現状の V2 | 判定 |
| --- | --- | --- | --- |
| 1-1 | **原点ノード**: 一覧の最上部に「原点」を固定表示し、`top_XY / front_XZ / side_YZ` の3平面を置く。削除・グループ移動・改名は不可(`MainWindow.cpp:6388`) | 無い。既定の作業平面は文書のもの(Entity)ではなく画面の枠だけ。だから「平面から離す」「2平面の中央面」などの相手を最初は選べず、**作業平面が作れないように見える** | **完全移植** + 原点の3軸(X/Y/Z)も一覧に足す(オーナー指示) |
| 1-2 | **作業平面の作り方 11 通り**(`MainWindowPanels.cpp:375`): XYZ基準面 / 位置と向きを数値指定 / 3点 / 平面から平行距離 / 直線を軸に傾ける / 点を通る平行面 / 2平面の中央面 / 2直線を含む面 / 点を通る直線直角面 / 線上位置の直角面 / 曲面近傍の接平面。右パネルに作り方ごとの入力欄(数値3つ組、基準平面コンボ、線上位置) | core は 11 通りある(`WorkPlaneOptions`)。画面は1つの窓で、選択から材料を取る。**数値で指定する欄(通過点・法線・横方向・3点の座標・回転軸)が無い**。名前欄も無い | **完全移植**: V1 と同じ右パネル(作り方ごとの入力欄 + 名前) |
| 1-3 | **作図面コンボ**(上部バー「作図面」`activePlaneCombo_`)と「正対」 | `workplane.set_active` は選択からしか効かない。コンボ無し | **完全移植** |
| 1-4 | **道具**: 選択・作図点・直線・ポリライン・矩形・円・円弧・ベジェ・スプライン・移動・コピー・ミラー複製・回転・分割・トリム・延長・端点一致・接線接続・曲率接続・測定・結合・2線を交点まで・C面取り・R面取り・角の加工・交点に点・2点を線で結ぶ・オフセット・基準線に設定/解除・一致解除・滑らか解除(`MainWindow.cpp:755-`) | 有る: 選択・作図点・直線・ポリライン・矩形・円・円弧・ベジェ・スプライン・移動・コピー・ミラー複製・回転・分割・トリム・延長・端点一致・接線接続・曲率接続・測定・結合・C面取り・R丸め・2点を線で結ぶ。**無い: オフセット・2線を交点まで(core `WireTransformMethod::Offset / MeetLines` はある)・交点に点・角の加工・基準線に設定/解除(`Entity.datum` はある)・一致解除・滑らか解除** | **完全移植**(無いものを足す)。2026-09-11: オフセット・2線を交点まで・交点に点・基準線に設定/解除 を足した。一致解除・滑らか解除は V2 に拘束が無いので **対象外**(手順書に明記)。角の加工も足した(`wire.corner_chamfer / corner_fillet`) |
| 1-5 | **ショートカット**: V/D/L/P/R/C/A/B/S/I/T/Shift+T/M/X/E、Ctrl+H 隠す、Ctrl+Shift+H 全て表示、Ctrl+1/2/3 設計/完成形/選択だけ(`MainWindow.cpp:832-`) | V/D/L/P/R/C/A/B/S/I/T/Shift+T/M/X/E、Ctrl+H、Ctrl+Shift+H、Ctrl+1 は同じ。**Ctrl+2(完成形)/Ctrl+3(選択だけ)が無い**(1-14 と同じ理由) | **完全移植**(残り2つ) |
| 1-6 | **円弧の作り方**: 3点 / 両端+半径(膨らむ側) / 始点+接線・法線方向(方向角、中心角 or 円弧長、曲がる側)(`MainWindowPanels.cpp:254-`) | core の `ToolSettings.arcMode` に3方式ある。**画面から切り替える手段が無い**(`SetToolSettings` を呼ぶ画面コードが 0 行)。常に3点 | **完全移植**(右パネルの円弧欄) |
| 1-7 | **実寸で確定**の欄: 線=長さ・角度、矩形=幅・高さ、円=半径、円弧=上記 + 「寸法で確定」ボタン。式が書ける(`ExpressionDoubleSpinBox`) | `CursorInput`(カーソル脇の入力列)が core に有り、線・円・円弧の欄も定義済み。**しかし画面が開くのは手順書の撮影のときだけ**(`OpenCursorInput` の呼び出し元は `V2ManualStates.cpp` のみ)。**作図中には出ない**。矩形の幅・高さの欄も無い | **完全移植**: V1 の欄を右パネルに持ち、加えて core の CursorInput を作図中に本当に開く |
| 1-8 | **数値で線を作る**欄: 3D直線 / 3Dベジェ / 平面上の直線・円・円弧・ベジェ を座標で(`wireKind_`、`MainWindowPanels.cpp:528`) | 無い | **完全移植** |
| 1-9 | **補助線として作図**、**指定した点を作図点として残す**(チェック) | core に `ToolSettings.construction` はあるが画面から切り替えられない。点を残す機能は無い | **完全移植** |
| 1-10 | **点グリッド**: 主点間隔(式可)、副点(主点のみ/1/2/1/3/1/4)、基準 X/Y、基準を 0,0 に戻す、グリッド原点ツール、表示、作図モード以外でも表示、作図面以外の線を常に薄く、主点色/副点色/背景色(`MainWindowPanels.cpp:1790-1860`) | `grid.edit` は間隔を 1→2→5→10→20 と回すだけ。副点は core にある(`GridDefinition.subdivision`)が画面から変えられない。色・基準 X/Y 欄・表示条件無し。原点ツールはある | **完全移植** |
| 1-11 | **上部バー**: 1段目=モード4つ + 正対 + **選択** + **測定** + 作図面コンボ + グループコンボ + スナップ。2段目=モードごとの道具列(`MainWindow.cpp:1340-1420`) | 1段目=モード4つ。2段目=道具列だが、**部品・製作・出力モードでは「選択」「測定」しか並ばない**(形・製作の命令はメニューだけ。実画面 `v2-mode-part.png` で確認)。作図面コンボ・グループコンボ・スナップ・正対は列に無い | **完全移植**(V1 と同じ並び) |
| 1-12 | **測定**: 2点間 / 3点角度 / 要素(接線・法線)、測る量の選択、寸法名を付けて「寸法を残す」、測定を消去(`MainWindowMeasure.cpp`) | 測定欄はある(距離・角度・参照寸法)。V1 の3モード切替と「要素」の扱いを合わせる必要 | **完全移植**(モードと欄を V1 と同じに) |
| 1-13 | **表示設定**: 線の色/幅/様式(実線・破線・点線)、補助線の色/幅/様式、面と板の色・不透明度・縁、背景色(`MainWindowPanels.cpp:1700-1980`) | `DisplaySettings` はグリッドと補助線の表示/非表示だけ。色・幅・様式は無い | **完全移植** |
| 1-14 | **表示の切替**: 隠す / 全て表示 / 設計 / 完成形 / 選択だけ | 隠す・全て表示・段(全部/グリッド無し/補助線無し)はある。設計/完成形/選択だけ は無い | **完全移植** |
| 1-15 | **編集タブ**(選んだものの数値編集): 平面の原点/法線/U軸、線の平面拘束・長さ/角度ロック、円弧の中心/軸/半径/角度(`editPlane*`, `editWire*`, `editArc*`) | 無い(制御点のつまみ移動はある) | **完全移植** ── 2026-09-11 済: `edit.numeric` + `V2EditDock` + core `app/EntityEdit`(平面は PointNormal に置換、線は種類を保って差し替え、直線の長さ/角度は「置き直す」。拘束は V2 に無いので固定はしない) |
| 1-16 | **面取り/角の加工の欄**: 対象2線・分岐・距離1/2・R半径・ポリラインの角番号 | 面取り量は数の棚1つ。2線の指定は選択順 | **完全移植** ── 2026-09-11 済: `V2CornerDock`(面取りの棚: 加工種類・直線 A/B(選んだ順)・A/B の残す側・A/B の切戻し(非対称)・半径・頂点番号)。量は数の棚と同じ値を映す。core: `CornerOptions`、`ProcessPolylineCorners(vertexIndex)`、`TransformWireDefinition.secondScalarMm/firstKeepSide/secondKeepSide/cornerIndex`(kcd2 に保存) |

## 2. 部分移植する部分(V2 の設計に合わせて置き換える)

| # | V1 の内容 | 現状の V2 | 判定 |
| --- | --- | --- | --- |
| 2-1 | 面の作り方: おまかせ(`BuildAutoSurface`)・ルールド・ロフト・案内付きロフト・Gordon・平面・パッチ、**回転体**、**押し出し面**、**オフセット面**、投影、**回り込み投影**、ライトケース(`MainWindowSurface.cpp`) | 役割表から7通り(平面/ルールド/ロフト/案内付き/曲線網/境界埋め/離した面)、おまかせ、押し出し(ワイヤーだけ=押し出し面相当)、投影(平面へ/曲面へ) | **部分**: 回転体・回り込み投影・ライトケースは V2 に無い。回転体は「断面を回した線を並べてロフト」で V1 と同じ近似で足せる ── 2026-09-11 済(`guide.revolve`、形状ガイドの作り方 `Revolve` を核に足した(BRepPrimAPI_MakeRevol)。写しのロフトは断面の並びが重心で決められ一周で重なるので使わない。数の棚に角度、軸は直線を選ぶ)。回り込み投影(複数面へ)は要検討 |
| 2-2 | 板材(plate): 面 + 厚み + 付け方 + 材料 + 範囲(u/v)+ 開口 + 切れ目 + 分割線 + 積層 | 部品(厚み付け・平面まで)+ 近似モデル(開口・折り線・接続範囲) | **部分**: 材料・範囲(面の一部だけ)・分割線・積層・切れ目は V2 の近似モデルの項目として置く ── 2026-09-11: 材料・積層(`SetManufacturingCommand`、Entity.manufacturing、製作の棚)と範囲(`rangeU/V`、`CropSamples`、FAB-M004)を入れた。分割線は手動境界(0〜1)で受ける。切れ目は `fabrication.assign_relief_cut`(reliefCutWires → 平らな部材の型紙の RELIEF 層、FAB-M005)。曲がった面の切れ目と自動の切れ目(fabrication/ReliefCut の検査)は未接続 |
| 2-3 | 近似モデル画面(`PartModelPanel.cpp`): 分割軸/境界/上限/最小幅/再現度/曲げスライダー/部材ごとの曲げ/接続範囲/固定/型紙 | 命令として全部ある(`fabrication.*`)が **パネルではなく順ぐりの切替**(方式・出力の切替は押すたびに回る) | **部分**: V1 と同じ1枚のパネルにまとめる ── 2026-09-11 済: `V2FabricationDock`(製作の棚: 方式・分割軸・境界(自動/手動)・上限・最小幅・再現度・板厚・許すずれ・組立率・固定で作るもの・各ボタン)。core `app/FabricationOptions`(UI-F001〜F004)。部材ごとの曲げは製作の棚の「曲げる部材」(空なら全体、番号を挙げるとその帯だけ。core `UpdateBandProgress`、UI-F006)|
| 2-4 | 出力: 出力表(UUID ではなく名前)、STL/STEP/PDF、出力面、範囲、治具 | 書き出し欄(対象×形式)、選んだものだけの文書 | **部分**: 治具(`body_surface_jig`)── 2026-09-11 済: `part.surface_jig`。専用の立体は作らず「離した面(すき間)+ 厚み」の2手で同じものを出す(core `app/SurfaceJig`、JIG-E001〜E003)。V1 の u/v 範囲は付けていない(元の面を範囲どおりに作る) |
| 2-5 | 保存形式 .kcd(58 命令のスクリプト) | .kcd2(JSON + zip) | **部分 = 読み込み器を作る**(下記 §4) |

## 3. 現状の V2 の不足(走査で見つけたもの)

core にあるのに画面から使えないものは、結線の門(include の到達)では捕まらなかった
── **関数単位では見ていない** ── ので、ここで名前を挙げる。

| # | 不足 | 根拠 |
| --- | --- | --- |
| 3-1 | 円弧の作り方(3方式)・半径・掃引角・補助線トグルを画面から変えられない | `DrawingSession::SetToolSettings` を呼ぶ画面コードが無い |
| 3-2 | 作図中の数値入力(カーソル脇)が開かない | `OpenCursorInput` は `V2ManualStates.cpp` からしか呼ばれない |
| 3-3 | 矩形に幅・高さの欄が無い | `CursorFieldsFor(Rectangle)` が空 |
| 3-4 | グリッドの副点・色・基準 X/Y・表示条件を画面から変えられない | `grid.edit` は間隔の順ぐり切替のみ |
| 3-5 | 原点の3平面・3軸が一覧に無く、既定平面が文書のものでない | `AdoptDocument` は原点平面を作らない |
| 3-6 | 作業平面を数値(通過点・法線・3点・回転軸)で指定できない、名前を付けられない | `V2WorkPlaneDialog` は方法・標準面・距離・角度だけ |
| 3-7 | 部品・製作・出力モードの道具列が「選択・測定」だけ | 実画面 `v2-mode-part.png`。命令はメニューにある |
| 3-8 | オフセット・2線を交点まで・交点に点・基準線・一致解除・滑らか解除・角の加工が画面に無い | core: `WireTransformMethod::Offset / MeetLines`、`PointSources.h`、`Entity.datum` |
| 3-9 | 線の色/幅/様式、背景色などの表示設定が無い | `DisplaySettings` は2つの bool |
| 3-10 | 数値で線を作る欄(座標指定)が無い | 該当コード無し |
| 3-11 | 作図面コンボ・グループコンボ・スナップが上部バーに無い | `BuildModeBar` はモード4つだけ |
| 3-12 | **「線が引いている途中も引いた後も点線」**: PC のスクリーンショット(`v2-curves.png`、`v2-draw-line.png`)では確定した線は実線、下見(途中)は破線。確定後も点線になるのは `construction` が真のときだけ(`V2ViewportDraw.cpp:149`)。3-1 の通り画面から切り替えられないので、真になる道は本来無い。**オーナーの環境で再現する条件(テーマ・DPI・作業平面外の減光)を確かめてから直す** ── 作業の最初に PC で実画面を撮って確定する |
| 3-13 | `.kcd` を読めない | 2026-09-11: `io/KcdImport` を作り「開く」で読める(読めない命令は KCD1-I002 で名前を挙げる) |

## 4. `.kcd` → `.kcd2` の読み込み(railway-nose-acceptance.kcd を通す)

`examples/railway-nose-acceptance.kcd` の中身: `plane_point_normal` ×6、`bezier3d` ×10、
`bspline3d_knots` ×5、`circle3d` ×2、`polyline3d` ×1、`wire_meta` ×18、`surface_loft` ×1、
`wire_project` ×3、`plate` ×2、`plate_range` ×2、`plate_opening` ×3、`body_surface_jig` ×1、
`visibility` ×26。

| .kcd の命令 | V2 での受け方 | 判定 |
| --- | --- | --- |
| `plane_point_normal / plane_three / plane_offset / plane_rotate` | `CreateWorkPlane` Feature(方法つき) | 完全 |
| `point3d`、`line3d / polyline3d / circle3d / arc3d / bezier3d / bspline3d(_knots)`、`sketch_*` | `CreateWire` / `CreatePoint` Feature | 完全(B-spline の knot は V2 の CubicBSpline へ。合わなければ標本化して折れ線にし、そう言う) |
| `wire_meta`(平面・参照/補助) | `sourcePlaneId`、`Visibility::Reference`、`construction` | 完全 |
| `surface_loft / surface_ruled / surface_guided_loft / surface_gordon / surface_planar / surface_auto` | 役割表 → `CreateGuideSurface`(方法と役割を対応) | 完全(`surface_grouped` は要確認) |
| `wire_project`(面へ向きつき投影) | `wire.project_surface` と同じ折れ線 Feature | 完全 |
| `wire_extrude / wire_plate_offset` | 押し出し(ワイヤーだけ)/ 離した面 | 部分 |
| `plate`(面・厚み・付け方・材料)+ `plate_opening` + `plate_range` + `plate_split_line` + `plate_relief_cut` + `plate_laminate` | 近似モデル(V1方式)+ 開口・手動境界。材料・積層は項目を足す | 部分(範囲=手動境界で受ける) |
| `part_model*`(近似・折り・組立・部材ごと) | 近似モデルの定義(方式1・組立率・帯ごと進行度) | 完全(同じ意味の項目が既にある) |
| `object_set*`(グループ)、`visibility`、`reference_dimension` | Group、Visibility、ReferenceDimension | 完全 |
| `wire_constraint / wire_radius_constraint / wire_coincident / wire_tangent / wire_curvature / wire_role` | 端点一致・接線・曲率は TransformWire に、拘束は編集タブ(1-15)に | 部分 |
| `body_surface_jig` | V2 に無い | **読めないと言う**(黙って落とさない) |

入り口は「ファイル」→「開く」で `.kcd` を選んだら変換して開く。変換で受けられなかった命令は
知らせに名前と行番号で並べる。

## 5. 進め方(この順で、1つずつ PC で確かめて push)

1. **3-12 の確定**: PC の実画面(オーナーと同じ DPI・テーマ)で線を1本引いて撮る。点線なら原因を突き止めてから作図に手を付ける。
2. **原点ノード + 3平面 + 3軸**(1-1)と、**作業平面の右パネル**(1-2、1-3)。ここが無いと作図が始められない。
3. **作図の右パネル**(1-6, 1-7, 1-8, 1-9)+ CursorInput を作図中に開く(3-2)+ 矩形の欄(3-3)。
4. **点グリッド**(1-10)と**表示設定**(1-13, 1-14)。
5. **上部バー**(1-11)と**モードごとの道具列**(3-7)、ショートカット(1-5)。── **済(PC緑)**: 作図面・作業中グループのコンボ、正対・選択・測定・吸着を上段へ置き、2段目を4モードの実操作へ切り替える。
6. **無い道具**(1-4: オフセット・2線を交点まで・交点に点・基準線・一致解除・滑らか解除・角の加工)と**編集タブ**(1-15, 1-16)。
7. **測定**を V1 の3モードに(1-12)。
8. **.kcd 読み込み**(§4)── `railway-nose-acceptance.kcd` を開いて、面が出来て、近似モデルの窓が開くところまで自己試験にする。
9. 部分移植(§2: 回転体、近似パネルの1枚化、材料・範囲・積層)。

それぞれ、台帳(command-catalog / acceptance-coverage / 手順書)と自己試験を同時に足す。
