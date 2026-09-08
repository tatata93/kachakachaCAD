# 受入試験の台帳

`acceptance-tests.md` の全IDについて、いまどこまで確かめられているかを1行ずつ書く。
**この表に無いIDがあること、この表に有るのに `acceptance-tests.md` に無いIDがあることは、
どちらも `tests_v2/coverage_tests.cpp` が失敗させる。**
「書き忘れて未達成のまま完成にする」ことを機械で防ぐための表である。

状態は3つだけ。

- `済` … 自動試験がある。根拠の欄に試験ファイルを書く。
- `部分` … 一部だけ確かめている。何が残っているかを書く。
- `未` … まだ。理由(どのWPか、何が要るか)を書く。

クラウドには OCCT も Qt も入れられない(配布元が拒否する)ため、
それらを要する項目は PC でしか確かめられない。その旨も理由に書く。

| ID | 状態 | 根拠 / 残り |
| --- | --- | --- |
| AT-ARC-001 | 済 | tests_v2/architecture_tests.cpp(core が Qt/OCCT に依存しない) |
| AT-ARC-002 | 済 | tests_v2/foundation_tests.cpp(TypedId。表示名参照なし) |
| AT-ARC-003 | 済 | tests_v2/document_tests.cpp(幾何は Feature が正本) |
| AT-ARC-004 | 済 | docs/v2/diagnostic-catalog.md と tests_v2/diagnostic_catalog_tests.cpp(154件のコードが一意・非空・種別つき。契約が定めたコードは未実装でも消せない。日本語本文での分岐を検索で禁じる) |
| AT-ARC-005 | 済 | tests_v2/architecture_tests.cpp(コード衛生) |
| AT-ARC-006 | 済 | tests_v2/fabrication_tests.cpp(製作層が core だけに依存) |
| AT-DOC-001 | 済 | tests_v2/document_tests.cpp |
| AT-DOC-002 | 済 | tests_v2/document_tests.cpp(位相順の再計算) |
| AT-DOC-003 | 済 | tests_v2/document_tests.cpp(循環を拒否) |
| AT-DOC-004 | 済 | tests_v2/document_tests.cpp(失敗したら一切変わらない) |
| AT-DOC-005 | 済 | tests_v2/document_tests.cpp(Undo/Redo、まとめ単位) |
| AT-EXP-001 | 済 | tests_v2/document_file_tests.cpp(保存して読み直す) |
| AT-EXP-002 | 済 | tests_v2/document_file_tests.cpp、tests_v2/zip_tests.cpp、tests_v2/robustness_tests.cpp(壊れた保存を断る) |
| AT-EXP-003 | 済 | tests_v2/document_file_tests.cpp(知らない版を断る、古い項目欠落を許す) |
| AT-EXP-004 | 済 | tests_v2/atomic_file_tests.cpp(途中失敗を注入しても既存が読める。成功時は本体と .bak が両方読み直せる) |
| AT-WIR-001 | 済 | tests_v2/curve_tests.cpp(5種の曲線が値を保つ) |
| AT-WIR-002 | 済 | tests_v2/wire_chain_tests.cpp |
| AT-WIR-003 | 済 | tests_v2/wire_chain_tests.cpp |
| AT-WIR-004 | 済 | tests_v2/wire_chain_tests.cpp(分岐と隙間) |
| AT-WIR-005 | 部分 | 端点接続は済。Segment内部での接続は初回切替の対象外(矛盾16) |
| AT-WIR-006 | 済 | tests_v2/wire_edit_tests.cpp |
| AT-WIR-007 | 済 | tests_v2/arc_builder_tests.cpp、tool_tests.cpp(3モード) |
| AT-WIR-008 | 済 | tests_v2/expression_tests.cpp |
| AT-WPL-001 | 済 | tests_v2/work_plane_tests.cpp(11方式すべて。原点・基底・法線・距離・角度を数値検査) |
| AT-WPL-002 | 済 | tests_v2/work_plane_tests.cpp(一直線3点・非平行2面・円筒でない面・長さ0の辺・直線の曲率法線を固定コードで拒否) |
| AT-WPL-003 | 済 | tests_v2/plane_follow_tests.cpp(Free3D / ReferenceOnly / LockedToPlane の3通り。縛られた点だけが平面内 UV と面からの浮きを保って追従し、ほかの2つは3D座標が変わらない。動かして戻せば元の位置へ戻る。曲線は種類を保ったまま追従し、円弧の半径・掃引角・長さが変わらない。平面をずらしても法線と基準方向は傾かず、回した平面では基準方向も同じだけ回る。2Dでも3Dでも同じ CurveSegment で出る) + snap_tests.cpp(平面へ投影するスナップ)+ work_plane_tests.cpp(uv⇔3D往復) |
| AT-GEO-001 | 済 | tests_v2/guide_surface_tests.cpp(入力検査)+ tests_v2/kernel_surface_tests.cpp(面積・穴・境界曲線種類) |
| AT-GEO-002 | 済 | tests_v2/guide_surface_tests.cpp + tests_v2/kernel_surface_tests.cpp(断面をつなぐ面の側面積) |
| AT-GEO-003 | 済 | tests_v2/guide_surface_tests.cpp + tests_v2/kernel_surface_tests.cpp(なめらかにつなぐ面が断面を通る) |
| AT-GEO-004 | 済 | tests_v2/guide_surface_tests.cpp(両端接続ガイド。オーナー提示ケース) |
| AT-GEO-005 | 済 | tests_v2/guide_surface_tests.cpp(欠損・2重・順序逆転を固定コードで拒否) |
| AT-GEO-006 | 済 | tests_v2/guide_surface_tests.cpp(5辺は自動分割せず拒否) |
| AT-GEO-007 | 部分 | 壊れた参照の判定は WP-06。入力の欠落は済 |
| AT-GEO-010 | 済 | tests_v2/wire_cage_tests.cpp(12辺順不同→6面1体) |
| AT-GEO-011 | 済 | tests_v2/wire_cage_tests.cpp(欠損・重複・T字・平板) |
| AT-GEO-012 | 済 | tests_v2/wire_cage_tests.cpp(選んだ線だけを使う) |
| AT-GEO-013 | 部分 | 非連結の検出は済(wire_cage_tests.cpp)。引いて分かれる場合は kernel_extrude_tests.cpp。ワイヤーかごの複数Part分割は WP-08 |
| AT-EXT-001 | 済 | tests_v2/extrude_tests.cpp(予測)+ tests_v2/kernel_extrude_tests.cpp(実形状の体積24000mm3・面6枚・意味的キー) |
| AT-EXT-002 | 済 | tests_v2/extrude_tests.cpp(開いた輪郭を EXT-002 で拒否。ワイヤー出力なら許す) |
| AT-EXT-003 | 済 | tests_v2/extrude_tests.cpp + tests_v2/kernel_extrude_tests.cpp(同じ押し出しからワイヤーと部品を取り出し、ワイヤー上の標本点すべてが部品の表面から modelLinearMm 以内にあることを DistanceToShapeSurface で測る。直線の輪郭と円の輪郭の両方で見る。表にない形の距離は測らずに断る) |
| AT-EXT-004 | 済 | tests_v2/extrude_tests.cpp(断面積を円弧のまま厳密に計算)+ tests_v2/kernel_extrude_tests.cpp(貫通穴つきの体積が厳密に合う=円が多角形へ化けていない) |
| AT-EXT-005 | 済 | tests_v2/extrude_tests.cpp(事前の個数)+ tests_v2/kernel_extrude_tests.cpp(実際に2部品。個数が違えば KER-E002 で拒否) |
| AT-EXT-006 | 済 | tests_v2/extrude_tests.cpp(5方式と到達判定)+ tests_v2/kernel_extrude_tests.cpp(平面・傾いた平面・円筒・球まで厳密に切る。トーラスは拒否) |
| AT-EXT-007 | 済 | tests_v2/extrude_tests.cpp(相手未選択を拒否)+ tests_v2/kernel_extrude_tests.cpp(足す・穴を引く・2つへ分離する引き。非連結は EXT-005 で拒否) |
| AT-EXT-008 | 済 | tests_v2/feature_reevaluation_tests.cpp(輪郭の寸法・移動距離・点の位置を変えても、出力の EntityId と安定キーは変わらず形だけが変わる。25回続けて編集してもIDは動かない。上流を変えれば下流へ伝わり、計算し直す順番は上流から。途中を変えたら上流は計算し直さない。種類の違う定義への差し替えは DOC-C006 で断る。core で形を作れない立体は「核が要る」と印をつけて返し、作ったふりをしない) |
| AT-MEA-001 | 済 | tests_v2/measurement_tests.cpp(dX/dY/dZ、投影距離、軸との角度) |
| AT-MEA-002 | 済 | tests_v2/measurement_tests.cpp |
| AT-MEA-003 | 済 | tests_v2/measurement_tests.cpp |
| AT-MEA-004 | 済 | tests_v2/measurement_tests.cpp |
| AT-MEA-005 | 済 | tests_v2/point_source_tests.cpp(測った2点の両端と中点、円・円弧の中心、始点・終点・中点、ベジェとB-splineの制御点、2曲線の最接近から作図点を作れる。円は始点と終点を二重に出さず、直線は中心も制御点も出さない。同じ入力なら同じ並びで出る。数値でない位置からは作らない。「円1の中心」のように由来が分かる名前になり、作った点は作業中グループへ入る) + measurement_tests.cpp(測定が位置を返すところ) |
| AT-FAB-001 | 済 | tests_v2/fabrication_tests.cpp(円筒の厳密展開) |
| AT-FAB-002 | 済 | tests_v2/fabrication_tests.cpp(円錐の厳密展開) |
| AT-FAB-003 | 済 | tests_v2/fabrication_tests.cpp(二重曲率を展開できないと言う) |
| AT-FAB-004 | 部分 | 切れ目の制約検査は済(pattern_tests.cpp)。向きの自動選択は WP-09後半 |
| AT-FAB-005 | 済 | tests_v2/panel_strategy_tests.cpp(同じ面から4通りを作り、1枚は1連結・別部材は各面別・少数分割は目標内で最小・混合は強曲率部だけ分離。作れない戦略は理由つきで残す) |
| AT-FAB-006 | 済 | tests_v2/panel_strategy_tests.cpp(7役割の割り当て、矛盾の拒否、壊れた参照を黙って動かさず付け直す/無効にする/消すを選ばせる) |
| AT-FAB-007 | 済 | tests_v2/opening_clip_tests.cpp(開口またぎ) |
| AT-FAB-008 | 部分 | 対応辺のIDは済(opening_clip_tests.cpp)。切れ目側は WP-09後半 |
| AT-FAB-009 | 済 | tests_v2/assembly_tests.cpp(0/30/100%で寸法不変) |
| AT-FAB-010 | 済 | tests_v2/closed_loop_tests.cpp(貼り合わせを含む輪を折り角の最小二乗で解く。解けない型紙は板を歪めず、いちばん開いている貼り合わせを名指しして断る) |
| AT-FAB-011 | 部分 | tests_v2/freeze_state_tests.cpp(0/30/100%で対応辺長が一致し、形の外接箱は異なる)。厚みを付けた立体そのものは WP-10 |
| AT-FAB-012 | 未 | 選択部材出力は WP-10 |
| AT-FAB-013 | 未 | ER1/ER2 の受入モデルは WP-12。OCCT が要る |
| AT-FAB-014 | 部分 | tests_v2/freeze_state_tests.cpp(ワイヤーのみ/部品のみ/両方。両方は同じ評価の束から作られ、部品の境界がワイヤーとぴったり一致する)。Entity 化は WP-10 |
| AT-UIX-001 | 済 | tests_v2/ui_mode_tests.cpp(4モードだけ。旧「面/板材」が無い。台帳の全コマンドがどこかのモードに出る)+ cad_next --self-test(モードを変えても選択・文書が変わらず、出るコマンドだけが変わる) |
| AT-UIX-002 | 済 | tests_v2/operation_guide_tests.cpp(全52コマンドで道具名・いまの手順・次の手順・決め方・やめ方・選択数の6つがそろい、仮の文字が混ざらない)+ cad_next --self-test |
| AT-UIX-003 | 済 | tests_v2/cursor_input_tests.cpp(道具ごとに主要欄がちょうど1つで、そこへ自動で焦点が合う。Tab/Shift+Tabで回る。式は全角でも通り「(180/2)*3 = 270 mm」の形で式と値を並べて出す。確定した欄はマウスで動かず、未確定の欄だけ追随する。長さ+角度・du+dv・長さ+du の各組で解け、短すぎる長さは必要な値を言って断り、矛盾すれば最後の変更だけ確定しない。画面の右下・角・入力列より狭い画面のどれでも画面外へ出ない) + cad_next --self-test(焦点と式、Tab/Enter/Esc、画面端での再配置) |
| AT-UIX-004 | 済 | tests_v2/snap_tests.cpp(8種+2種、優先順位、抑止キー) |
| AT-UIX-005 | 済 | tests_v2/grid_tests.cpp(1/2・1/3・1/4の点数、主副の間隔、細かすぎる副点の省略、UV での原点保持、作業平面が動いても付いていく、壊れた平面参照を別平面へ付け替えない) |
| AT-UIX-006 | 済 | tests_v2/active_group_tests.cpp(切り替えたあとに作った作図点・ワイヤー・作業平面・形状ガイド・部品が全部そこへ入る。切り替える前に作ったものは動かない。派生物は Feature の派生グループへ入り、作業中グループを切り替えても動かない。無いグループは作業中にできず文書も変わらない。元に戻せば作業中グループも戻る。保存して読み直しても、作業中グループも各Entityの所属も Feature の派生グループの指定も保たれる) + cad_next --self-test(帯に出て、一覧がまとまりで束ねられ、作業中に印が付く) |
| AT-UIX-007 | 済 | tests_v2/guide_surface_table_tests.cpp(複数行と役割ごとの1始まり番号、1行への複数ワイヤー追加とつながらない線の拒否、同じワイヤーの二重登録の拒否、同じ役割の中だけでの順序変更、方向反転で並びも各線の向きも逆になり種類は変わらない、両端が外形へ届いたときだけ有効と出る接続列、役割と番号だけで決まる色) + cad_next --self-test(表の色が core の式と一致し、同じ行が3Dへ色と進行矢印つきで出る=3D色同期。行の追加・移動・反転・断り。役割がそろえば案内が消える) |
| AT-UIX-008 | 済 | tests_v2/view_orientation_tests.cpp(26区画すべてで正対でき、視線と上向きが直交する。ドラッグ量と回転量が比例し、刻んでも一気でも同じ姿勢になる。89度まで回しても90度へ寄らない。離した瞬間も1時間後も変化0。感度0.25/0.05/1.0deg-px、クリック15度、相対軸は選択が無ければ断る) + cad_next --self-test(キューブの連続回転・非吸着・クリック正対・回転矢印がカメラだけを回す) |
| AT-UIX-009 | 済 | tests_v2/document_tests.cpp と robustness_tests.cpp(文書が変わらない)+ cad_next --self-test(失敗する操作を3回ずつ繰り返してもアプリが続き、文書も選んだ道具も変わらない) |
| AT-UIX-010 | 済 | tests_v2/theme_layout_tests.cpp(2画面サイズ x 4拡大率で、入力列が画面の外へ出ず、カーソルを覆い隠さず、欄が10個あっても縦にはみ出さない。細かすぎるグリッドは閾値どおりに消える) + cad_next --self-test(通常とWindows 95の両方で viewport が0の大きさにならず、ビューキューブが画面内に収まり、案内が空にならない。1366x768 と 1920x1080 の両方で部品がはみ出さず、道具箱が空にならない) + _FIX_AND_BUILD.cmd が `--size` と QT_SCALE_FACTOR で2画面サイズ x 4拡大率(100/125/150/200%)+ Win95 の絵を `_claudeout/dpi/` へ撮る |
| AT-UIX-011 | 済 | src/next/kachakacha/app/CommandCatalog.cpp と tests_v2/command_catalog_tests.cpp(52件を双方向で突き合わせ。表示名・記号・案内・受入IDの有無、ショートカットの重複、camera操作が文書を変えないことを見る) |
| AT-EXP-010 | 済 | tests_v2/kernel_export_tests.cpp(同じ部品の STEP と STL で体積・外接箱が出力精度内で一致。精度を上げると近づく) |
| AT-EXP-011 | 済 | tests_v2/export_tests.cpp(潰れた三角形)+ tests_v2/kernel_export_tests.cpp(開いた殻・体積0・自己交差を拒否し、0バイトのファイルを残さない) |
| AT-EXP-012 | 済 | tests_v2/export_tests.cpp(SVGはA、DXFはARC/SPLINE。折れ線にしない) |
| AT-EXP-013 | 済 | tests_v2/pdf_tests.cpp(原寸。座標変換を使わない) |
| AT-PER-001 | 済 | tests_v2/evaluation_queue_tests.cpp(100ms を超えたところで取消を出し、心拍が250ms以内なら応答していると見る。5秒かかる評価でも200ms毎に心拍を刻めばずっと応答している。心拍は時刻が戻らない) + robustness_tests.cpp(遅すぎる処理を時間で暴く。ExtendCurveToBoundary を直した) |
| AT-PER-002 | 済 | tests_v2/evaluation_queue_tests.cpp(長い評価の途中で文書が変わると前の評価に取消の合図が立ち、遅れて返った結果は PER-002 で捨てる。20回続けて差し替えても生きているのは最後の1つだけで、19件を捨てる。番号が合っていても版が違えば捨て、どの版を待っていたかを言う。取消の合図は別スレッドから見える) |
| AT-PER-003 | 済 | tests_v2/scale_tests.cpp(1000ワイヤーの保存・読込・検証。1万本でも動く)。画面側は WP-08後半 |
| AT-PER-004 | 済 | tests_v2/scale_tests.cpp(500操作。Entity増殖・ID衝突・Undo崩れが無いこと) |
