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
| AT-EXP-001 | 済 | tests_v2/document_file_tests.cpp(保存して読み直す)+ tests_v2/sample_document_tests.cpp(配る見本 samples/v2-sample.kcd2 が、同じ版から毎回同じバイト列で出て、読み直せて、置き直し忘れると落ちる) |
| AT-EXP-002 | 済 | tests_v2/document_file_tests.cpp、tests_v2/zip_tests.cpp、tests_v2/robustness_tests.cpp(壊れた保存を断る)+ tests_v2/scene_builder_tests.cpp(壊れた文書は ResetTo が断り、いまの文書はそのまま残る) |
| AT-EXP-003 | 済 | tests_v2/document_file_tests.cpp(知らない版を断る、古い項目欠落を許す)+ tests_v2/scene_builder_tests.cpp(開くと文書が入れ替わり、Undo履歴は捨てられる。Segment の ID は開き直しても変わらない。開き直しても線が二重にならない)+ cad_next --self-test(配る見本が開ける) |
| AT-EXP-004 | 済 | tests_v2/atomic_file_tests.cpp(途中失敗を注入しても既存が読める。成功時は本体と .bak が両方読み直せる) + tests_v2/kcd_import_tests.cpp(V1 の .kcd: 平面4通り・線7種・点・紐づけ・表示が読める、受入例 railway-nose-acceptance.kcd が平面6・線18・ロフト1・板2で読め、読めない命令は KCD1-I002 で名前を挙げる、壊れた行と無い名前と一様でない節は KCD1-E001)+ 自己試験(受入例を「開く」で V2 の文書にする) |
| AT-WIR-001 | 済 | tests_v2/curve_tests.cpp(5種の曲線が値を保つ)+ tests_v2/entity_edit_tests.cpp(数値編集: 直線の点と線の ID が残る、長さ・平面内角度で終点を置き直す、長さ 0 は UI-E004、折れ線は点の並びで円弧が混ざると UI-E003、円弧は中心・軸・角度で出て半径を変えても円弧のまま、平面は PointNormal に置き換わり原点面は UI-E002)+ cad_next --self-test(編集の棚で直線・円・作業平面を直せる) |
| AT-WIR-002 | 済 | tests_v2/wire_chain_tests.cpp |
| AT-WIR-003 | 済 | tests_v2/wire_chain_tests.cpp |
| AT-WIR-004 | 済 | tests_v2/wire_chain_tests.cpp(分岐と隙間) |
| AT-WIR-005 | 部分 | 端点接続は済。Segment内部での接続は初回切替の対象外(矛盾16) |
| AT-WIR-006 | 済 | tests_v2/wire_edit_tests.cpp + tests_v2/intersection_points_tests.cpp(交点に点: 十字は1つ、三角と井桁は組ごと、平行・1本は UI-X101)+ cad_next --self-test(オフセットは元を残し 5mm 離れる、2線を交点まで で端が合う、交点に点は一度で戻る、基準線の印は付いて外れ形は同じ) + tests_v2/polyline_corners_tests.cpp(角の加工: L字・コ字・閉じた正方形、短くなった辺が次の角へ渡る、直線でない角は触らず角が無ければ GEO-E021、辺より大きい量は断る)+ 自己試験(ポリラインの角が落ちて丸まる)+ 面取りの欄: wire_edit_tests(非対称の切戻し、残す側の指定、残す側に長さが無ければ断る)、polyline_corners_tests(頂点番号で 1 つの角だけ、開いた並びの両端と無い番号は GEO-E021、閉じた並びの頂点 0)、document_file_tests(scalar2 / keepFirst / keepSecond / corner が往復)+ 自己試験(面取りの棚で A 4・B 8・残す側 始点/終点、頂点 1 だけ) + tests_v2/array_plan_tests.cpp(配列: 間隔でも端から端まででも並べられ、斜めも可。2個が最小で、200 個を超える打ち間違いは断る。長さ0・数値でない間隔は断る。円は両端に置き、360度のときだけ最後を元に重ねない。逆回りも可。角度0と長さ0の軸は断る) + cad_next --self-test(直線に並べて一度で戻せる / 一周に並べても最後が元に重ならない / 並べる数の打ち間違いを断り、やめたときは何も変わらない) |
| AT-WIR-007 | 済 | tests_v2/arc_builder_tests.cpp、tool_tests.cpp(3モード、指定した点を作図点として残す)+ tests_v2/direct_wire_entry_tests.cpp(数値で線を作る: 平面上の直線は作業平面の uv、3D 直線は世界座標、円・円弧・ベジェ、足りない点 UI-D001・半径 0 は UI-D002・一直線の3点は断る、作った線は文書へ入り名前が付き吸着の相手になる)+ tests_v2/session_tests.cpp(指定点は線とひとまとまりで戻る)+ cad_next --self-test(作図の棚で「両端+半径」にすると2点で半径どおりの円弧になり、補助線・指定点が効き、数値で線が作れて半径 0 は断られる) |
| AT-WIR-008 | 済 | tests_v2/expression_tests.cpp |
| AT-WPL-001 | 済 | tests_v2/work_plane_tests.cpp(12方式すべて。原点・基底・法線・距離・角度を数値検査。位置と向きの数値指定は法線0・横方向が平行を拒否)+ tests_v2/work_plane_options_tests.cpp(棚の材料集め: 3点と回転軸は数の欄で代える、コンボの基準平面)+ tests_v2/origin_planes_tests.cpp(原点の3面は一度だけ足され、消せず改名できず(DOC-C009)、保存しても原点のまま)+ 自己試験 V2SelfTestPlanes.cpp(一覧の原点ノード6行、軸のチェック、棚で数値指定/コンボの基準平面/足りないと押せない) |
| AT-WPL-002 | 済 | tests_v2/work_plane_tests.cpp(一直線3点・非平行2面・円筒でない面・長さ0の辺・直線の曲率法線を固定コードで拒否) |
| AT-WPL-003 | 済 | tests_v2/plane_follow_tests.cpp(Free3D / ReferenceOnly / LockedToPlane の3通り。縛られた点だけが平面内 UV と面からの浮きを保って追従し、ほかの2つは3D座標が変わらない。動かして戻せば元の位置へ戻る。曲線は種類を保ったまま追従し、円弧の半径・掃引角・長さが変わらない。平面をずらしても法線と基準方向は傾かず、回した平面では基準方向も同じだけ回る。2Dでも3Dでも同じ CurveSegment で出る) + snap_tests.cpp(平面へ投影するスナップ)+ work_plane_tests.cpp(uv⇔3D往復) |
| AT-GEO-001 | 済 | tests_v2/guide_surface_tests.cpp(入力検査)+ tests_v2/kernel_surface_tests.cpp(面積・穴・境界曲線種類) |
| AT-GEO-002 | 済 | tests_v2/guide_surface_tests.cpp + tests_v2/kernel_surface_tests.cpp(断面をつなぐ面の側面積) |
| AT-GEO-003 | 済 | tests_v2/guide_surface_tests.cpp + tests_v2/kernel_surface_tests.cpp(なめらかにつなぐ面が断面を通る)+ 回転体: guide_surface_tests(作り方 Revolve は断面 1 本と軸と角度で通り、断面 2 本・角度 0・一周超え・軸なし・軸上の断面は断る)、guide_table_build_tests / feature_definition_tests(軸と角度が表・保存の形へ渡る)、revolve_surface_tests(選んだ線と欄の検査 REV-E001〜E005、回した写しの並び)+ 自己試験(回転体: 線は増えず形状ガイドが 1 つできて作り方に残る) |
| AT-GEO-004 | 済 | tests_v2/guide_surface_tests.cpp(両端接続ガイド。オーナー提示ケース) |
| AT-GEO-005 | 済 | tests_v2/guide_surface_tests.cpp(欠損・2重・順序逆転を固定コードで拒否) |
| AT-GEO-006 | 済 | tests_v2/guide_surface_tests.cpp(5辺は自動分割せず拒否) |
| AT-GEO-007 | 済 | tests_v2/broken_reference_tests.cpp(指していた断面が消えると壊れた参照になり、近くの断面1や断面3へ移らない。候補は近い順に挙げるが選ばない。相手を選ばずに指し直そうとすると DOC-C008 で断り、候補が1つしかなくても自動では選ばない。選んだ相手へは指し直せる。効かなくする・消すも選べる。並びは毎回同じ) + panel_strategy_tests.cpp(製作側の壊れた参照)|
| AT-GEO-008 | 済 | tests_v2/guide_table_build_tests.cpp(おまかせは2本でルールド3本でロフト。役割・向き・作り方が保存の形から作り直しで戻る。元の線が消えていれば GEO-R002 で断る。既存行への追加は逆向きの線も向きを直して受ける。離した面は元の面の行と距離を持ち、2枚目は UI-R010 で断る)+ tests_v2/command_availability_tests.cpp(行を選んだときだけ行に効くコマンドが押せる)+ cad_next --self-test(作り方を選び、外形Uと断面を表へ入れ、表から案内付きロフトの面を作り、保存して開き直すと表と面が戻る。使わない役割の行が残っていると作り方を変えられない) |
| AT-GEO-010 | 済 | tests_v2/wire_cage_tests.cpp(12辺順不同→6面1体) |
| AT-GEO-011 | 済 | tests_v2/wire_cage_tests.cpp(欠損・重複・T字・平板) |
| AT-GEO-012 | 済 | tests_v2/wire_cage_tests.cpp(選んだ線だけを使う) |
| AT-GEO-013 | 済 | tests_v2/wire_cage_tests.cpp(離れた2つの箱は、面12枚の1つの候補ではなく、6面ずつの2つの立体に分かれる。余る線は無く、いくつになるかを GEO-S007 で先に知らせる) + tests_v2/kernel_wire_cage_tests.cpp(2つ同時に確定すると別々の形として2つ作られ、体積もそれぞれのもの。面の意味的キーは core が決めたものと同じ。選ばない・2度選ぶ・候補にない番号は断る) + kernel_extrude_tests.cpp(引いて分かれる場合) |
| AT-EXT-001 | 済 | tests_v2/extrude_tests.cpp(予測)+ tests_v2/kernel_extrude_tests.cpp(実形状の体積24000mm3・面6枚・意味的キー)+ 治具(surface_jig_tests: 表側は外側へ、厚みが負なら裏側へ内側で、すき間 0 なら離さない。負のすき間 JIG-E001・厚み 0 JIG-E002・面の数 JIG-E003)+ 自己試験(当たり面と当て板ができて一度で戻る) |
| AT-EXT-002 | 済 | tests_v2/extrude_tests.cpp(開いた輪郭を EXT-002 で拒否。ワイヤー出力なら許す) |
| AT-EXT-003 | 済 | tests_v2/extrude_tests.cpp + tests_v2/kernel_extrude_tests.cpp(同じ押し出しからワイヤーと部品を取り出し、ワイヤー上の標本点すべてが部品の表面から modelLinearMm 以内にあることを DistanceToShapeSurface で測る。直線の輪郭と円の輪郭の両方で見る。表にない形の距離は測らずに断る) |
| AT-EXT-004 | 済 | tests_v2/extrude_tests.cpp(断面積を円弧のまま厳密に計算)+ tests_v2/kernel_extrude_tests.cpp(貫通穴つきの体積が厳密に合う=円が多角形へ化けていない) |
| AT-EXT-005 | 済 | tests_v2/extrude_tests.cpp(事前の個数)+ tests_v2/kernel_extrude_tests.cpp(実際に2部品。個数が違えば KER-E002 で拒否) |
| AT-EXT-006 | 済 | tests_v2/extrude_tests.cpp(5方式と到達判定)+ tests_v2/kernel_extrude_tests.cpp(平面・傾いた平面・円筒・球まで厳密に切る。トーラスは拒否) |
| AT-EXT-007 | 済 | tests_v2/extrude_tests.cpp(相手未選択を拒否)+ tests_v2/kernel_extrude_tests.cpp(足す・穴を引く・2つへ分離する引き。非連結は EXT-005 で拒否) |
| AT-EXT-008 | 済 | tests_v2/feature_reevaluation_tests.cpp(輪郭の寸法・移動距離・点の位置を変えても、出力の EntityId と安定キーは変わらず形だけが変わる。25回続けて編集してもIDは動かない。上流を変えれば下流へ伝わり、計算し直す順番は上流から。途中を変えたら上流は計算し直さない。種類の違う定義への差し替えは DOC-C006 で断る。core で形を作れない立体は「核が要る」と印をつけて返し、作ったふりをしない) |
| AT-MEA-001 | 済 | tests_v2/measurement_tests.cpp(dX/dY/dZ、投影距離、軸との角度) + tests_v2/measure_panel_tests.cpp(2点間モード: 押した2点で距離 13・dZ 12・軸との角度、残す値は距離)+ 自己試験(2点間・3点角度・寸法を残す・消去・右クリック) |
| AT-MEA-002 | 済 | tests_v2/measurement_tests.cpp + tests_v2/measure_panel_tests.cpp(3点角度モードは2点目が頂点で 90 度) |
| AT-MEA-003 | 済 | tests_v2/measurement_tests.cpp + tests_v2/measure_panel_tests.cpp(要素モード: 線1本と点で接線・法線・半径、2本で接線どうしの角度) |
| AT-MEA-004 | 済 | tests_v2/measurement_tests.cpp |
| AT-MEA-005 | 済 | tests_v2/point_source_tests.cpp(測った2点の両端と中点、円・円弧の中心、始点・終点・中点、ベジェとB-splineの制御点、2曲線の最接近から作図点を作れる。円は始点と終点を二重に出さず、直線は中心も制御点も出さない。同じ入力なら同じ並びで出る。数値でない位置からは作らない。「円1の中心」のように由来が分かる名前になり、作った点は作業中グループへ入る) + measurement_tests.cpp(測定が位置を返すところ) |
| AT-FAB-001 | 済 | tests_v2/fabrication_tests.cpp(円筒の厳密展開) |
| AT-FAB-002 | 済 | tests_v2/fabrication_tests.cpp(円錐の厳密展開) |
| AT-FAB-003 | 済 | tests_v2/fabrication_tests.cpp(二重曲率を展開できないと言う) |
| AT-FAB-004 | 済 | tests_v2/pattern_tests.cpp(U / V / Both / Auto で候補が変わる。Auto は曲率の大きい向きへ直交して切り、理由を日本語で言う。二重曲率が同じくらいなら Both にして片方へ寄せない。曲率の比を9通り変えて U と V と Both のどれもが出ることを見る=縦だけに固定していない。Both では進行方向が2本出る。平らな部材では幅の広い方向。壊れた曲率と、自動のままの向きは FAB-C007 で断る) + 切れ目の制約検査(交差/ligament)も同ファイル |
| AT-FAB-005 | 済 | tests_v2/panel_strategy_tests.cpp(同じ面から4通りを作り、1枚は1連結・別部材は各面別・少数分割は目標内で最小・混合は強曲率部だけ分離。作れない戦略は理由つきで残す) |
| AT-FAB-006 | 済 | tests_v2/panel_strategy_tests.cpp(7役割の割り当て、矛盾の拒否、壊れた参照を黙って動かさず付け直す/無効にする/消すを選ばせる)+ 切れ目(planar_panel_tests: 切れ目が型紙に載り RELIEF 層の切る線になる、平面から外れれば断る。fabrication_evaluate_tests: 平らな部材に入り、閉じた線は FAB-M005、載っていなければ FAB-M003)+ 自己試験(開いた線を切れ目にできる)+ 回り込み投影(wrap_projection_tests: 段違いの2枚で区間が分かれ境目が真ん中に詰まる、区間は自分の面の内側で終わる、1枚に載るなら閉じたまま1区間、面1枚 FAB-J003・線なし/向きなし FAB-J002・当たらない FAB-J001)+ 自己試験(面が1枚なら押せない) |
| AT-FAB-007 | 済 | tests_v2/opening_clip_tests.cpp(開口またぎ) |
| AT-FAB-008 | 済 | tests_v2/pattern_tests.cpp(Straight / VNotch / CurvedVNotch の3つとも、正規化弧長で左右の辺を対応させる。同じ長さなら組立100%で隙間0。長さが違えばその差がそのまま隙間として出て、左右どちらの長さも変えない。目標を超えたら FAB-C006 で断り、「片方の辺だけを縮めて合わせることはしません」と言う。点の数が左右で違っても同じ決め方で対応が取れる) + opening_clip_tests.cpp(対応辺のID) |
| AT-FAB-009 | 済 | tests_v2/assembly_tests.cpp(0/30/100%で寸法不変) |
| AT-FAB-010 | 済 | tests_v2/closed_loop_tests.cpp(貼り合わせを含む輪を折り角の最小二乗で解く。解けない型紙は板を歪めず、いちばん開いている貼り合わせを名指しして断る) |
| AT-FAB-011 | 済 | tests_v2/freeze_state_tests.cpp(0/30/100%で対応辺長が一致し、形の外接箱は異なる。30%で固定すると Frozen のEntity になり、名前に割合が入る。固定前の派生物は直接編集できない。線の無いワイヤー・輪郭の足りない部材・厚み0の部材は FAB-E002 で断り、もとが無い・名前が無いときは FAB-E003 で断る)+ 部材ごとの曲げ(fabrication_options_tests: 挙げた帯だけが変わり、挙げていない帯は保ち、空なら全体で個別値を捨てる。範囲外と 120% は UI-F006 / FAB-M002。部材番号の欄の読み)+ 自己試験(1 番だけ曲げが変わり、無い番号は断る) |
| AT-FAB-012 | 済 | tests_v2/kernel_export_tests.cpp(10部材のうち2つを選ぶと塊は2つだけ出て、体積も選んだ2つぶんだけになる=選ばなかった8つが混ざらない。STL でも同じ。1つも選ばない・同じ部材を2度選ぶ・表にない部材は EXP-013 で断る。選ぶ数を1〜6と変えれば出る数も同じだけ変わる)+ tests_v2/selection_tests.cpp(画面から拾えるのは許容差の内側だけ。素で押すと入れ替え、Shiftで足し、Ctrlで入切、Altで外す。何も無いところを素で押したときだけ空になる。選んだ順は変わらない。消えたものは選択から外れる)+ tests_v2/export_panel_tests.cpp(対象の数は選択から来る。数が0の対象は選べないが並びからは消えない)+ cad_next --self-test(線を選べて足せて消せる。書き出しの棚が選択に従う) |
| AT-FAB-013 | 済 | tests_v2/acceptance_er_tests.cpp(ER の前頭部を 1/87 の実寸で面として標本化し、近似・展開・開口・曲げ状態まで実際の道を通す。幅基準 3520/87 = 40.4598 mm。V2方式は肩の二重曲率を理由に断り、V1方式は帯へ切る。再現度3では少数の幅広い帯になり、部材は帯1本ぶんの連続した外周を持つ=全面三角形にしない。肩を丸めると帯が増え、丸めなければ V2方式でも通る。再現度3/6/9で目標も実際の最大偏差も単調非増加、帯は減らない。部材数の上限を超えず、超えて届かなければ一文で言う。面の上に描いた6枚窓と前照灯が帯の型紙へ切り出され、またぐ窓は複数の帯に開き、前照灯は円のまま残り、面に載っていない窓は FAB-M003 で断る。30% でも各帯の辺長が変わらない=しわも縮尺変化も無く、開口も消えない。100% は完成形に一致し、0% は各帯が平ら)+ tests_v2/surface_projection_tests.cpp(線を向きに沿って曲面へ落とし、当たらなければ FAB-J001 で断り、閉じた四角は閉じた折れ線として落ちる)+ cad_next --self-test(前面に描いた四角を曲面へ投影し、開口にすると帯の型紙に窓の取り分が開く)+ tests_v2/kernel_er_export_tests.cpp(1:1 PDF が出る。部材に厚みを付けて立体にでき、外側・中央・内側で体積は同じ。30% STEP は選んだ数だけの塊で体積が合い、100% STL は三角形にした体積がB-Rep と5%以内で合う。30%と100%で板の量が変わらない) |
| AT-FAB-014 | 済 | tests_v2/freeze_state_tests.cpp(ワイヤーのみ/部品のみ/両方で出来る Entity の種類と数が変わり、両方のときの数はそれぞれ単独のときと一致する。両方は同じ評価の束から作られ、部品の境界がワイヤーとぴったり一致する。実体にしても曲線の種類が変わらない。固定したあとに元の製作モデルを30%から90%へ動かしても、固定したものは変わらない=値のコピーであって参照ではない) |
| AT-FAB-015 | 済 | tests_v2/band_approximation_tests.cpp(貪欲な帯分割、最小幅、上限部材数、手動境界、展開の辺長一致、曲げ具合0/1/途中の等長、折り線ごと・帯ごとの進行度、点の対応付け、帯をまたぐ窓の切り出し)+ tests_v2/fabrication_evaluate_tests.cpp(同じ球で V2方式は断り V1方式は切る。接続スコープの線は載っている点だけ寄り、離れた点は残る)+ tests_v2/feature_definition_tests.cpp(方式と曲げ状態と接続スコープが保存して読み直せる)+ cad_next --self-test(近似モデルは文書に入り開き直しても戻る。組立率を変えると本当に曲がる。曲げ状態で固定すると線と面と部品になる)+ tests_v2/fabrication_options_tests.cpp(製作の棚の欄: 手動境界の読み、範囲の外は UI-F002〜F004、作り方との往復)+ 自己試験(製作の棚が欄を持ち数の棚と方式を映す)+ 範囲(fabrication_evaluate_tests: u 0.5〜1 で標本が範囲の中だけになり、壊れた範囲は FAB-M004)+ 材料・積層(document_tests: 部品に付き線には DOC-C010、戻せる)+ 自己試験(製作の棚の範囲と材料・積層) |
| AT-UIX-001 | 部分 | 旧4モード試験とtool切替試験はある。WP-14で固定モード廃止、重複棚除去、旧ToolSession状態が残らない画面試験へ改訂する。 |
| AT-UIX-002 | 部分 | 旧操作ガイドの項目試験はある。WP-14で共通ToolSession、右コンテキストプロパティ、tool-first/事前選択同値、Esc/Backspace/Ctrl+Zを追加する。 |
| AT-UIX-003 | 部分 | 式、focus、Tab、画面端試験はある。WP-14で右プロパティとカーソル近傍入力の双方向同期を追加する。 |
| AT-UIX-004 | 部分 | SelectionRef、同一Entity内の複数Segment/Subshape、Ctrl toggle、Shift拘束、S解除、命中metadataを試験済み。意味状態と部分要素表示は tests_v2/semantic_state_tests.cpp(通常/Hover/選択の決め方、選んだ線分だけが選択、選択がHoverより強い)+ cad_next --self-test(同じワイヤーの選んだ線分だけ、Hoverと選択の描き分け、テーマを替えても色が潰れない、途中経過・掴んだ影・折り曲げの帯を描くペンは細い線の設定でも確定線より細い半透明の破線、途中経過は拾えない、締められなかったときは途中経過を残す)。残りは矩形、Tab/Alt候補巡回、Face pick、Input/Warning/Error/Locked/Hidden の意味状態(ToolSession待ち)と画像確認。 |
| AT-UIX-005 | 済 | tests_v2/grid_tests.cpp と cad_next --self-test。WP-14ではモード依存表示だけを新shellへ接続し直す。 |
| AT-UIX-006 | 済 | tests_v2/active_group_tests.cpp と cad_next --self-test。作業中グループと派生グループの分離、保存再読込を検査済み。 |
| AT-UIX-007 | 済 | tests_v2/guide_surface_table_tests.cpp と cad_next --self-test。複数役割、複数Wire、順序、反転、両端接続、3D色同期を検査済み。 |
| AT-UIX-008 | 部分 | view_orientation/facing/shape mesh試験はある。WP-14でwheel/middle/Shift+middle/right click契約と右drag非回転を追加する。 |
| AT-UIX-009 | 部分 | 失敗反復とDocument不変は検査済み。WP-14で理由番号、問題位置、修正案、ToolSession入力保持をUI試験へ追加する。 |
| AT-UIX-010 | 部分 | 2画面サイズx4 DPIと2themeの試験はある。WP-14でdock復元、monitor消失、overflow、初期配置を追加する。 |
| AT-UIX-011 | 部分 | 52 CommandIdの双方向台帳試験はある。WP-14で全入口の同一ToolSession、F1、未実装空command不在を追加する。 |
| AT-UIX-012 | 未 | WP-14でWorkPlane切替/削除後のWire EntityId、SegmentId、3D座標不変とtree非所有を追加する。 |
| AT-UIX-013 | 未 | WP-14で押し出し4出力、同一評価bundle、入力保持、表示だけ隠す、Undoを追加する。 |
| AT-EXP-010 | 済 | tests_v2/kernel_export_tests.cpp(同じ部品の STEP と STL で体積・外接箱が出力精度内で一致。精度を上げると近づく) |
| AT-EXP-011 | 済 | tests_v2/export_tests.cpp(潰れた三角形)+ tests_v2/kernel_export_tests.cpp(開いた殻・体積0・自己交差を拒否し、0バイトのファイルを残さない)+ tests_v2/export_panel_tests.cpp(対象x形式が成り立たなければ押す前に断り、選んでいた形式も動かさない。出力先が空、既にあるファイルへ承諾なし、対象0のどれでも押せない。出力先を変えたら上書きの承諾は消える)+ tests_v2/export_content_tests.cpp(中身が空にならない。広がりの無い形は紙にしない)+ cad_next --self-test(出せない形式は押す前に断る。出す先を決めればファイルが出る) |
| AT-EXP-012 | 済 | tests_v2/export_tests.cpp(SVGはA、DXFはARC/SPLINE。折れ線にしない)+ tests_v2/export_content_tests.cpp(選んだワイヤーを紙へ置くとき、円弧は円弧のまま残り、辺長は実寸のまま変わらない。原点から離れていても紙の左下へ寄せる。同じ入力からは同じバイト列が出る) |
| AT-EXP-013 | 済 | tests_v2/pdf_tests.cpp(原寸。座標変換を使わない)+ tests_v2/export_content_tests.cpp(選んだワイヤーから 1:1 PDF が出て、中身が空にならない) + tests_v2/pattern_view_tests.cpp(型紙を画面へ収める: 縦長は高さ・横長は幅で決まり、縦横は必ず同じ倍率(歪ませない)、紙は真ん中、左上と右下が合う。枠が余白より狭い・寸法が0以下・数値でない値なら倍率1で返して落ちない) + cad_next --self-test(型紙を作ると下見の棚に出る。枚数・紙の大きさ・線の数を言う。端でページが止まる) |
| AT-EXP-014 | 済 | tests_v2/sub_document_tests.cpp(部品を選ぶと面・線・作業平面が残り無関係な線は入らない。残した依存は名前で知らせる。出来た文書は保存して読み直せる。空と無いものは断る。作り方の中身が指す id を全部拾う)+ cad_next --self-test(選んだものだけを別の文書にでき、開き直すと部品が作り直され、無関係な線は入らない) |
| AT-PER-001 | 済 | tests_v2/evaluation_queue_tests.cpp(100ms を超えたところで取消を出し、心拍が250ms以内なら応答していると見る。5秒かかる評価でも200ms毎に心拍を刻めばずっと応答している。心拍は時刻が戻らない) + robustness_tests.cpp(遅すぎる処理を時間で暴く。ExtendCurveToBoundary を直した) |
| AT-PER-002 | 済 | tests_v2/evaluation_queue_tests.cpp(長い評価の途中で文書が変わると前の評価に取消の合図が立ち、遅れて返った結果は PER-002 で捨てる。20回続けて差し替えても生きているのは最後の1つだけで、19件を捨てる。番号が合っていても版が違えば捨て、どの版を待っていたかを言う。取消の合図は別スレッドから見える) |
| AT-PER-003 | 済 | tests_v2/scale_tests.cpp(1000ワイヤーの保存・読込・検証。1万本でも動く)。画面側は WP-08後半 |
| AT-PER-004 | 済 | tests_v2/scale_tests.cpp(500操作。Entity増殖・ID衝突・Undo崩れが無いこと) |
