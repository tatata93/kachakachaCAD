# Wire-first V2 コマンド台帳

## 1. 目的

この台帳は、利用者が実行するコマンドの安定ID、表示名、入力、出力、受入試験を固定する。
メニュー、ツールバー、右パネル、ショートカット、操作ガイドは同じ `CommandDescriptor` を参照する。
同じ処理へ別IDのコマンドや別controllerを作ってはならない。

各コマンドは次を必ず定義する。

```text
id / labelJa / mode / icon / defaultShortcut / selectionPredicate /
parameterSchema / controllerFactory / operationGuide / acceptanceIds
```

- IDは保存データのFeature typeとは別であり、UI command registry内で安定させる。
- 選択不足時は非表示にせずdisableし、`selectionPredicate` の不成立理由を日本語で表示する。
- Documentを変えるコマンドは必ず `DocumentCommand` を1つ返す。直接変更しない。
- Previewだけの操作、カメラ操作、選択変更はDocumentのUndoへ積まない。
- 作成されるSource Entityはactive groupへ、Derived EntityはFeature用派生groupへ入れる。
- 表中の `AT-X-001から004` は文書上の省略記法である。実装する `acceptanceIds` には範囲内のIDを
  1件ずつ展開して登録する。

## 2. 共通コマンド

| ID | 表示名 | 入力/有効条件 | 結果 | 試験 |
| --- | --- | --- | --- | --- |
| `file.new` | 新規 | 常時。未保存変更は確認 | 空Document | AT-DOC-001 |
| `file.open` | 開く | `.kcd2`、切替後`.kcd` | 検証後にDocument置換 | AT-EXP-001から003 |
| `file.save` | 保存 | 有効Document | 原子的保存 | AT-EXP-004 |
| `file.save_as` | 名前を付けて保存 | 有効Document | 新しいパスへ原子的保存 | AT-EXP-004 |
| `edit.undo` | 元に戻す | Undoあり | 直前transactionを戻す | AT-DOC-005 |
| `edit.redo` | やり直す | Redoあり | 直前Undoを戻す | AT-DOC-005 |
| `edit.delete` | 削除 | ワイヤー1つ以上 | 選んだものを消す。下流があれば断る | AT-DOC-005 |
| `view.hide_selected` | 選択を隠す | ワイヤー1つ以上 | 画面から隠す。形は消えない | AT-UIX-010 |
| `view.show_all` | すべて表示 | 有効Document | 隠したものを全部出す | AT-UIX-010 |
| `selection.activate` | 選択 | 常時 | 選択toolへ戻る | AT-UIX-001 |
| `measure.open` | 測定 | 常時 | 非モーダル測定窓 | AT-MEA-001から005 |
| `view.fit_all` | 全体表示 | 可視幾何あり | cameraだけ変更 | AT-UIX-008 |
| `view.align_selection` | 選択に正対 | 平面/平面Face/作業平面1つ | cameraだけ変更 | AT-UIX-008 |
| `view.align_workplane` | 正対 | 常時(作業中の作図面) | cameraだけ変更 | AT-UIX-008 |
| `view.display_settings` | 表示設定 | 常時 | 表示属性だけ変更 | AT-UIX-010 |
| `view.stage_all` | 設計(すべて出す) | なし | (表示のみ)Ctrl+1 | AT-UIX-010 |
| `view.stage_no_grid` | グリッドを消す | なし | (表示のみ) | AT-UIX-010 |
| `view.stage_no_construction` | 完成形(補助線も消す) | なし | (表示のみ)Ctrl+2 | AT-UIX-010 |
| `view.stage_selection_only` | 選択だけ | なし | (表示のみ)Ctrl+3 | AT-UIX-010 |
| `entity.rename` | 名前を変える | エンティティ、新しい名前 | RenameEntity | AT-DOC-005 |
| `snap.toggle` | スナップ | 常時 | 通常snap ON/OFF | AT-UIX-004 |
| `group.set_active` | 作業中グループ | group 0または1 | active group更新 | AT-UIX-006 |
| `workplane.set_active` | 作業平面を使用 | WorkPlane 1つ | active work plane更新 | AT-WPL-003 |

`file.new/open` の確認dialogで破棄を選ばない限り、現在Documentを変更してはならない。

## 3. 作図コマンド

| ID | 表示名 | 入力/主要パラメータ | 出力 | 試験 |
| --- | --- | --- | --- | --- |
| `draw.point` | 作図点 | 3D位置または平面UV | Point | AT-WIR-001, AT-MEA-005 |
| `draw.line` | 直線 | 始点、終点/長さ/方向 | Wire(Line) | AT-WIR-001, AT-UIX-003 |
| `draw.polyline` | ポリライン | 2点以上、Backspace対応 | 1 Wire(複数Line) | AT-WIR-002 |
| `draw.rectangle` | 矩形 | 2隅または幅/高さ | closed Wire | AT-EXT-001 |
| `draw.circle` | 円 | 中心、半径 | closed Wire(Circle) | AT-WIR-001 |
| `draw.arc` | 円弧 | 方式+方式別入力 | Wire(Arc) | AT-WIR-007 |
| `draw.bezier` | ベジェ | 4制御点 | Wire(Bezier) | AT-WIR-001 |
| `draw.spline` | スプライン | 通過点2以上、degree | Wire(B-spline) | AT-WIR-001 |
| `wire.trim` | トリム | 対象segment、境界、残す側 | TransformWire | AT-WIR-006 |
| `wire.extend` | 延長 | 対象端、境界/長さ | TransformWire | AT-WIR-006 |
| `wire.split` | 分割 | segment、parameter/交点 | TransformWire | AT-WIR-001,006 |
| `wire.move` | 移動 | ワイヤー、2点 | TransformWire | AT-WIR-006 |
| `wire.copy` | コピー | ワイヤー、2点 | TransformWire | AT-WIR-006 |
| `wire.mirror` | ミラー複製 | ワイヤー、鏡の線2点 | TransformWire | AT-WIR-006 |
| `wire.rotate` | 回転 | ワイヤー、中心と向き2点 | TransformWire | AT-WIR-006 |
| `wire.join` | 結合 | 2 chain、明示許容/拘束 | TransformWire | AT-WIR-004,006 |
| `wire.coincident` | 端点一致 | 2端点、動かす側 | TransformWire | AT-WIR-006 |
| `wire.tangent` | 接線接続 | 2 segment端、動かす側 | TransformWire | AT-WIR-006 |
| `wire.curvature` | 曲率接続 | 2曲線端、動かす側 | TransformWire | AT-WIR-006 |
| `wire.chamfer` | C面取り | 2辺(選んだ順に A・B)、A の切戻し(数の棚の面取り量)、面取りの棚の B の切戻し(0 なら対称)・A/B の残す側 | TransformWire(secondScalarMm / firstKeepSide / secondKeepSide) | AT-WIR-006 |
| `wire.fillet` | R丸め | 2辺、半径、面取りの棚の A/B の残す側 | TransformWire | AT-WIR-006 |
| `wire.offset` | オフセット | ワイヤー1以上、オフセット距離(数の棚) | TransformWire(Offset)。元は残す | AT-WIR-006 |
| `wire.meet_lines` | 2線を交点まで | 直線2本 | TransformWire(MeetLines) | AT-WIR-006 |
| `wire.intersection_points` | 交点に点 | ワイヤー2以上 | 交点ごとに CreatePoint。線は変えない | AT-MEA-005 |
| `wire.corner_chamfer` | 角の加工(落とす) | ワイヤー1以上、面取り量、面取りの棚の「この頂点の角だけ」+ 頂点番号 | TransformWire(CornerChamfer、cornerIndex)。直線どうしの角を全部、または 1 つ | AT-WIR-006 |
| `wire.corner_fillet` | 角の加工(丸める) | ワイヤー1以上、丸め半径、同上 | TransformWire(CornerFillet、cornerIndex) | AT-WIR-006 |
| `wire.set_datum` | 基準線に設定 | ワイヤー1以上 | SetDatum(true) | AT-DOC-005 |
| `edit.numeric` | 数値で編集 | 作業平面か線を1つ | 編集の棚を出す。「変更を適用」で UpdateFeatureDefinition(平面は PointNormal に、線は種類を保って点/中心/半径/角度を差し替え)。原点面は UI-E002、種類が混ざった線は UI-E003 | AT-WIR-001, AT-UIX-001 |
| `wire.clear_datum` | 基準解除 | ワイヤー1以上 | SetDatum(false) | AT-DOC-005 |
| `workplane.create` | 作業平面を作る | 方式+方式別Entity | WorkPlane | AT-WPL-001,002 |
| `grid.edit` | グリッド | 間隔、副点、原点 | Document settings | AT-UIX-005 |
| `grid.move_origin` | グリッド原点を移動 | 原点handle、snap先 | Document settings | AT-UIX-005 |

`draw.arc` の方式は `3点 / 両端と半径 / 始点と接線 / 始点と法線`。別コマンドIDへ分裂させず、
parameter schemaのdiscriminatorにする。

## 4. 部品コマンド

| ID | 表示名 | 入力/主要パラメータ | 出力 | 試験 |
| --- | --- | --- | --- | --- |
| `guide.create` | 形状ガイド | method、役割別WireChain表 | GuideSurface | AT-GEO-001から006, AT-UIX-007 |
| `guide.revolve` | 回転体 | 断面の線1 + 軸の直線1(この順)、回転体の角度(度) | GuideSurface(作り方 Revolve: 核が断面を軸のまわりに回した面を作る。写しのロフトではない)。REV-E001〜E003、GEO-G009 | AT-GEO-003 |
| `guide.set_method` | 面の作り方 | 常時。使わない役割の行が残っていれば断る | 表の method | AT-GEO-008 |
| `guide.add_row` | 選択を表へ | ワイヤーか形状ガイドの面1以上。役割は作り方で使うものから選ぶ | 表の行 | AT-GEO-008, AT-UIX-007 |
| `guide.append_row` | 選択を既存行へ追加 | 表の行1、ワイヤー1以上 | 表の行 | AT-GEO-008, AT-UIX-007 |
| `guide.row_up` | 行を上へ | 表の行1 | 表の並び | AT-GEO-008, AT-UIX-007 |
| `guide.row_down` | 行を下へ | 表の行1 | 表の並び | AT-GEO-008, AT-UIX-007 |
| `guide.row_remove` | 行を削除 | 表の行1 | 表の行 | AT-GEO-008, AT-UIX-007 |
| `guide.row_reverse` | 向きを反転 | 表の行1 | 表の行 | AT-GEO-008, AT-UIX-007 |
| `guide.build` | 表から面を作る | 表の行1以上。離した面は右の距離欄を使う | GuideSurface | AT-GEO-008 |
| `guide.clear` | 表を空にする | 表の行1以上 | 表 | AT-GEO-008 |
| `wire.project` | 面へ投影 | WireChain、対象、方向、hit policy | derived Wire | AT-FAB-007 |
| `wire.project_surface` | 曲面へ投影 | ワイヤー1以上と形状ガイド1。作業平面の向きに沿って落とす | derived Wire(折れ線) | AT-FAB-013 |
| `part.extrude` | 押し出し | profile、方向、終端、出力、演算 | Wire/Part/両方 | AT-EXT-001から008 |
| `part.thicken` | 面に厚みを付ける | 形状ガイド、厚み、付け方 | Part | AT-EXT-001 |
| `part.thickness_placement` | 厚みの付け方 | 常時。外側→中央→内側の順に切り替える | 次の厚み付けの付け方 | AT-EXT-001 |
| `part.thicken_to_plane` | 面を平面まで立体に | 形状ガイド1と作業平面1。面が平面をまたげば断る | Part | AT-EXT-001 |
| `part.from_wire_cage` | ワイヤー群から部品 | scope、patch候補、採用候補 | 1以上のPart | AT-GEO-010から013 |
| `part.boolean_add` | 足す | target Part 1、tool Part 1以上 | Part | AT-EXT-007 |
| `part.boolean_cut` | 引く | target Part 1、tool Part 1以上 | 1以上のPart | AT-EXT-007 |
| `derived.freeze` | 現在状態を固定 | Derived Wire/Part 1以上 | Frozen Wire/Part | AT-DOC-004, AT-FAB-014 |

押し出し内の `足す/引く` と単独Booleanは同じ `BooleanFeature` 評価器を使う。UI入口が違っても
演算実装を複製しない。

## 5. 製作コマンド

| ID | 表示名 | 入力/主要パラメータ | 出力 | 試験 |
| --- | --- | --- | --- | --- |
| `fabrication.create` | 製作モデルを作る | Part/Subshape、strategy、settings | FabricationModel | AT-FAB-001から005 |
| `fabrication.assign_role` | 境界の役割 | WireChain、role | Fabrication Feature更新 | AT-FAB-006 |
| `fabrication.assign_relief_cut` | 切れ目にする | 開いたワイヤー1以上 | Fabrication Feature更新(reliefCutWires)。平らな部材の型紙に切れ目(RELIEF 層)。閉じた線は FAB-M005、部材に載っていなければ FAB-M003 | AT-FAB-006 |
| `fabrication.preview_update` | プレビュー更新 | 編集中設定 | Document外preview | AT-PER-001,002 |
| `fabrication.create_pattern` | 型紙を作る | panel選択、用紙設定 | Pattern | AT-FAB-001,002,007 |
| `fabrication.set_assembly` | 組立状態 | master%、個別Fold | Fabrication Feature更新 | AT-FAB-009から011 |
| `fabrication.set_method` | 近似の方式を切り替える | なし | (設定のみ) | AT-FAB-015 |
| `fabrication.freeze_output` | 固定で作るもの | なし | (設定のみ) | AT-FAB-011 |
| `fabrication.set_connection_scope` | 接続スコープ | ワイヤー | Fabrication Feature更新 + CreateWire | AT-FAB-015 |
| `fabrication.freeze_state` | 現在状態を固定 | panel、Wire種別、Part方式、板厚位置 | Wire/Part/両方 | AT-FAB-011,012,014 |

`fabrication.preview_update` だけはDocumentへcommitしない。`create` または設定更新の確定時に、
previewで使った同じDefinitionを再検証してcommitする。

## 6. 出力コマンド

| ID | 表示名 | 入力/有効条件 | 結果 | 試験 |
| --- | --- | --- | --- | --- |
| `export.validate` | 出力を検査 | 明示対象1以上 | ExportReport | AT-EXP-011 |
| `export.stl` | STLで保存 | valid Part 1以上 | `.stl` | AT-EXP-010,011 |
| `export.step` | STEPで保存 | valid Part 1以上 | `.step` | AT-EXP-010,011 |
| `export.svg` | SVGで保存 | Wire/Pattern 1以上 | `.svg` | AT-EXP-012 |
| `export.dxf` | DXFで保存 | Wire/Pattern 1以上 | `.dxf` | AT-EXP-012 |
| `export.pdf_1to1` | 1:1 PDFで保存 | Pattern 1つ | `.pdf` | AT-EXP-013 |

各保存コマンドは内部で `export.validate` と同じserviceを再実行する。画面の事前検査だけを信用しない。

## 7. ビュー操作ID

次はDocument commandではないが、入力割当を一意にするためregistryへ置く。

```text
view.orbit
view.pan
view.zoom
view.cube_drag
view.cube_face
view.rotate_world_x/y/z
view.rotate_local_x/y/z
```

`view.cube_drag` とXYZ回転は共通 `ViewController` のquaternion更新を使う。モデル変換コマンドを呼ばない。

## 8. 完全性ゲート

`UIX-011`: 通常UIの全コマンド入口はこの台帳の一意なID、controller、service、受入試験へ結び付ける。

WP-08は `AT-UIX-011` としてregistryの全IDについて次を自動検査する。

1. ID重複なし。
2. 日本語label、icon、selection predicate、operation guide、acceptance IDが空でない。
3. Document変更コマンドにcontrollerとCommand builderがある。
4. メニューとtoolbarが別controllerへ接続されていない。
5. 通常UIに登録された未台帳IDがない。
6. 台帳の必須IDがアプリregistryに全てある。
7. 無効状態の理由が空でない。
8. UIだけ存在しserviceを呼ばないcontrollerがない。
