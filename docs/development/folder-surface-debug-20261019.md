# フォルダ操作・面表示・ワイヤ選択と5視点のデバッグ（2026-10-19）

状態: 完了。基点dfc6d153f、WP-08。検証済み製品・試験ソースはdaa0ceaeb。既存の無関係なworktree・原本・配布物を保全した。

## 最初の修正

- グループをフォルダとして単クリック開閉、F2名前変更、同列/子作成、階層移動・表示保持で扱う。
- 面の表示用UV格子と実境界分割を区別し、正本の形状・公差を落とさず表示負荷と選択を改善する。
- 面にする/面作成中は、表示中の面に重なるWireを入力として選べる。支持面選択は別の役割として扱う。
- ユーザーと同じマウス/キーボードの入口を通し、操作数や無反応を検出する。

## 続く5回の確認

| 回 | 視点 | 確認範囲 |
| --- | --- | --- |
| 1 | 人の操作 | 全コマンド入口、右ペイン、選択役割、数値、プレビュー、確定と取消。追加修正を重点試験 |
| 2 | 形状と種類 | 全幾何ツール群、2D/3D/曲線/穴/重なりと異常入力。元形状と精度を保持 |
| 3 | 履歴と文書 | 全ツールの保存/Undo/Redo/再開の既存回帰と新規経路、表示・階層保持 |
| 4 | 負荷と繰返し | 全UIケースの時間集計、実モデル、連続選択/表示/取消、資源寿命 |
| 5 | 配布と回帰 | 全CTest、ビルド/配布版全UI、旧自己試験、通常起動、画像と操作説明 |

各回で実行した対象・件数・結果・問題・修正とcommitを追記しpushする。
異なる視点は同一試験の根拠なき5重実行を意味しない。新変更の影響範囲は再確認し、最終候補は必須全体ゲートへ進める。
「すべての機能」はコマンド/試験一覧と照合し、未試験や対象漏れを隠さない。未知の全入力に不具合が無いとの保証はしない。

## 最初の修正と確認

- フォルダ単クリック開閉、矢印キー、F2共通名前変更。空作成は既存選択を移動しない。空分類は非表示。
- 面作成のWire入力では表示中のGuideSurfaceのedgeを拾わず元Wireを選択。支持面・元面欄はGuideSurfaceへ切替。
- 通常U/V格子は選択面だけ。グリッド棚で非表示/選択面/全てを切替。shape正本・公差・保存定義は変更しない。
- 近似固定のprivate panel-patch生成をSewingと同一ドメイン統合で整理。実折れ目/穴/島は保持。
- `initial/results.json`: 69件中68件成功。新規の名前確定試験はQt遅延commitを待たず失敗したため、実イベントループを15ms通すよう試験を修正。`retry/results.json`で新規3件成功。
- `initial/kernel.xml`: gpt_surface/gpt_fabricationの2 CTest成功。面積3.6、穴、曲げ位置、Face数（平坦1/曲げ2）を確認。
- OS操作用computer-use APIはこの接続に無いため、OS注入したとの主張はしない。実Qtウィンドウへのmouse/key/IMEイベントを使用し、内部の状態代入だけの試験と区別した。

## 第1回（操作）の途中で見つけた問題

- 共通F2がグループを受けず、作成時の案内と矛盾していた（初期修正）。
- グループ行Deleteが容器を残して形状だけを消していた。グループ＋中身/容器解除/取消を一覧と赤色プレビューから選択し、原子的にUndoできるようにした。
- 名前編集中のEnterがモデリングへ伝播し、renameが全場面更新を呼んでプレビューへ再入した。ツリー入力欄のキーを隔離し、group renameは一覧だけ更新する。
- pending2線コマンド中のフォルダクリックで、選択の同期処理が木を作り直す。生ポインターを使わず安定キーから再取得した。
- ツールはviewport選択を入力種類で絞るため、グループ削除の全子孫を文書から収集し、全要素削除として扱う。Wire+Point混在・面入力中・保存再読込を追加。
- `round1-new`の削除試験は成功、名前中ツール試験は失敗し、単独再現は戻らなくなったため自分の試験プロセスだけ停止した。`rename-fixed`で2件成功。
- `round1-operation`はレビューで問題を発見して停止（成功扱いなし）。新規2経路は`round1-extra-fixed`で2件成功。
- `round1-operation-fixed`で全539UIが成功（失敗0・約769.66秒）。構造チェックも成功（追加試験のQObject明示include不足を修正済み）。
- 後続回では明示的な`--self-test-order reverse|seed:N`を使用し、全件を欠落なく別順序で実行する。通常と正式全体入口の順序は変更しない。

### Git実行の外部障害

最初のcommit/pushとstageは、自動承認レビューのmodel capacityまたはdeadlineエラーで未実行。
安全性を否認されたのではなく、承認レビューが完了しない。無承認の別経路でGitを書き換えず、
最初の検証済み差分を`out/folder-surface-20261019/initial.patch`に保存してデバッグを続けた。

第1回完了: 通常描画/通常順序で全539件の名前と件数を照合し、入力ソース不変を確認。最初の修正は649f25d40としてpush済み。承認サービス混雑の中で無承認経路は使用しなかった。


## 第2回（形状・描画）

- 25断面点・2窓（1つは折れ目を跨ぐ）の面を検査。実折れ3面／平坦1面に整理し、各パッチの全頂点と内部点、面積、穴、境界数、BRep保存復元を確認。
- 大小0.01/1/100倍、移動と軸変更でも同じ実境界を保持。公差や面積検査は緩和しない。
- 整理した90度の折れ面への厚み化がSimple offsetで失敗した。全Faceが平面の折れシートだけ、Intersection接続の厚み化を使う。滑面経路は従来どおり。
- 離れた2島を一括オフセットすると失敗したため、Compoundの構成要素ごとに独立処理する。島を接合せず、両方の体積を保持。
- 表/裏/中央、平坦/37%/100%曲げ、反転・軸変更した法線側の両スキン位置、閉鎖性・体積・窓、元shapeの不変を確認。押し出しの解析体積も確認。
- `round2-native/islands.log`およびXMLの3 CTest成功（約33.34秒）。gpt_fabricationは12件成功。先行のgpt_surface/extrudeを含む5 CTestも成功。`folder-round2-preflight-20261019`は9件成功。
- 読取担当がSDKの引数順・元shapeのCopy・滑面経路を確認。指摘された法線側と独立島を回帰へ追加した。
- 全UIは明示的ソフトウェア描画で実行。GPU parityのGPU側は対象外と記録し、通常描画の第1回・最終ゲートと区別する。

第2回完了: `round2-geometry`の全539 UI成功（失敗0、721.41秒）。予定ケースとの完全照合・入力ソース不変を確認。


## 第3回（履歴・文書）の修正

- 親と子・その中身を同時に移すと、各要素が移動先へ直接入り階層を平坦化していた。選択された祖先が運ぶ子孫を追加移動から除き、1回のUndoで全体を戻す。
- 右クリック移動先は表示名だけでなく所属パスを表示。同一パスまで重複する場合だけUUIDを補足する。パス出現数は1回集計し、各候補ごとの全グループ走査を避ける。
- メニューは生の行ポインターを捕捉せず、UUID由来のキーから実行時に取得。消えた項目があれば部分実行せず理由を表示。
- Explorer+Group重点28件成功。親子・中身の同時移動、最上位への移動、個別非表示の保持、Undo/Redo/保存再読込、同名移動先、取消、行の再生成後の実メニュークリックを含む。
- 最初の28件ではQtのnative InternalMoveセッションが無い合成Dropが受理されず27成功1失敗。OS drag開始を偽装せず、実装のdrop通知以降を直接検査へ切替。マウス・キー・IMEとメニュークリックの試験は実Qtイベントのまま維持。
- preflightではQPoint明示include不足の構造チェックだけ失敗（他8成功）。明示includeを追加後に構造チェックと重点28件を再確認して成功。
- 6枚の実画面をmanualの画像台帳へ登録し、フォルダ作成/移動/削除、面の下のWire、形状確認線の説明へ掲載。画像の登録と参照の検査も成功。
- 全UIは`seed:185`の明示順序。通常順・第2回のソフトウェア描画とは別の観点とする。


第3回完了: `round3-history`で全541件成功、失敗0、778.75秒。
ローカル集計が通常順の一覧を使い、seed順の実績との順序不一致でいったん失敗した。
別プロセスの`--self-test-list --self-test-order seed:185`で予定を取得し、通常一覧と同じ541名の集合、
実績と同じ順序、全成功、終了0、ソース不変を確認して再照合した。初期メタデータも保全。
製品の失敗や未実行を隠して成功扱いしていない。第4回用の集計は先に同じ順序の予定を取得する。


## 第4回（負荷・繰返し）の修正と実測

- 空グループ作成・移動のAdoptは幾何の全再生成ではないが、選択再通知で面編集の有効プレビューを再計算していた。単純な全通知抑制は採用せず、面編集側で入力面・縁・native元形状・作業面・幾何公差が一致する場合に限りプレビューを保持する。
- wire入力を含む投影は形状がnativeハンドルだけで決まらないため、この省略対象にしない。元native形状が変わると従来どおり再計算する。
- 読取レビューでinteractiveJoinMmの比較漏れを発見し、数値epsilonとともに保存・比較を補強。接続公差だけを変えた場合の再評価も追加した。
- `HP-SE-07`: 空グループボタンへ実mouse event→元面を非表示グループへ移動→表示復帰で同じpreview handleを保持。元wire定義を変更してnative面を再生成した場合はpreviewも更新。
- `115投影.kcd2`（205 Wire、310 Entity）の実一覧へmouse eventで選択10回・checkbox50回。原本SHA-256は前後一致し、選択先・行・スクロールの保持成功、tree rebuild 0回。
- `round4-focused/owner/latency.json`: 選択中央値12.69ms（6.58〜19.09ms）、表示切替中央値16.41ms（14.70〜20.48ms）。初回open10.27秒。Qtイベント投入からイベント処理までの計測でOS入力量ではない。前後同条件の速度比較ではなく現状値であり、ロード高速化の達成率を主張しない。
- 初回ビルドは試験側で比較演算を持たないKernelShapeHandleのvector同士を比較して失敗。value比較へ直し、公開core APIの追加はしなかった。
- 接続公差追加前の面編集7件・実モデル1件は成功。補強後に関連試験と全UIを確認する。

補強後の面編集7件と構造検査は成功。明示試験順序の不正4種類を拒否し、通常/逆順/seedの全542件の名前集合と逆順を照合。

第4回完了: `round4-load`で逆順の全542 UI成功（失敗0、771.89秒）、予定順序・件数と照合し入力ソース不変。実モデルのmouse操作1件も成功し原本不変。


## 第5回（配布・製作と全回帰）の候補準備

- 第4回は787acac20としてcommit・push済み。
- 折れ面の体積検査を上下限の包絡から、幅・厚み・曲げ角のmiter補正を含む解析値との1e-6 mm³比較へ強化。表/裏/中央、平坦/37%/90度、反転した軸で一致。平坦時は7.8 mm³、90度は表7.6・裏8.0・中央7.8 mm³。
- `round5-native/tests.log`で全12件成功。穴・島・スキン位置・元形状不変の検査を保持。許容を緩和しない。
- ここから完成候補を固定し、正式`full`で全CTest・開発版全UI・配布版全UI・旧自己試験を確認する。完了前に完成配布版とは扱わない。

最終候補はdaa0ceaebとしてcommit・pushして固定。配布ZIPの全542 UIが成功（CTest v2_package_zip: 762.60秒）。全体ゲートの残りを確認中。


## 第5回完了・配布と通常起動

- `out/verification/folder-surfaces-final-20261019/result.json`: status passed、preflight 9件、CTest 195件（skip 0）、開発版全542 UI、配布版全542 UI、旧CAD `--self-test`が全て成功。
- CTest経過1491.81秒、通常の全体入口の経過1581.30秒（26分21秒）。配布版762.60秒・開発版675.60秒は各CTestの経過であり、並列区間を足してユーザー待ち時間としていない。旧自己試験9.95秒。
- ソース候補daa0ceaeba7ef248f767220ba91bc534d9f45b50、入力SHA-256 `b31908f545aabf9a2b25ac1c70def06ca7d822f74deb4467e39bee2555fd7a53`。
- `verify.py publish`がソース・設定・環境・XML・両UI予定/実績・ZIP・コピー後バイト列を再照合して成功。
- 配布先 `out/releases/folder-surfaces-final-20261019/package/kachakacha_cad_next.exe`。ZIP `out/releases/folder-surfaces-final-20261019/package.zip`、SHA-256 `b520e502dd9bee4f512554c319cbff6ac7c940e9cbf08f62d68a2063ef19599e`。
- `scripts/run-verified.py out/releases/folder-surfaces-final-20261019`で通常起動。`out/folder-surface-20261019/normal-startup/startup.json`でready/rendered/event_loop/visible/exposed/on_screenが全てtrue。Windows、1180×945。自動終了フラグを付けず、利用可能なウィンドウを残した。編集中の旧版は終了させない。
- WP-08の完了欄は、配布後の記録更新として別コミットへ入れる。この記録更新を製品の再ビルドと混同しない。配布物は上記候補の不変な検証済みバイト列。

## 各回のコミットと入口

| 段階 | commit | 主な確認 |
| --- | --- | --- |
| 最初の修正 | 649f25d40 | フォルダ操作・面入力・格子・不要境界整理 |
| 第1回 操作 | 3fa164153 | 通常順 全539 UI、名前・削除・再入・選択 |
| 第2回 形状/描画 | 96ad970bf | software 全539 UI、穴・折れ・厚み・島 |
| 第3回 履歴/文書 | cd2aff37c | seed:185 全541 UI、階層・メニュー・保存 |
| 第4回 負荷/反復 | 787acac20 | reverse 全542 UI、実モデル60 mouse操作、preview保持/失効 |
| 第5回 配布/製作 | daa0ceaeb | 解析体積、正式full全195＋UI542×2・旧試験・通常起動 |

実行入口:

```text
python scripts/run-verified.py out/releases/folder-surfaces-final-20261019
python scripts/verify.py full --parallel 2 --run-id <新しい実行ID>
```

操作説明は `docs/manual/explorer-selection.html`。写真6枚はmanual画像台帳へ登録済み。
OSのnative入力APIは利用不可であり、OSからの実ドラッグは未確認。実Qtのマウス・キー・IME、
実メニュークリックと本番drop通知以降を検査した。全登録ケースの成功を、未知の全形状への保証とはしていない。

## ファイル構成・削除と保全

削除した製品ファイル・元モデルは0。不要境界の整理は生成形状の処理であり、元Wireや旧版ファイルの削除ではない。
新規画像6枚、開発記録MD/HTMLを追加。公開core APIと保存形式は変更しない。既存の増分ビルド、
旧配布、ユーザーモデル、他worktree、無関係な未追跡ファイルを保全した。
基点から候補までの変更対象は次のとおり（全体の追加/変更、削除なし）。

```text
A	docs/development/folder-surface-debug-20261019.html
A	docs/development/folder-surface-debug-20261019.md
M	docs/manual/README.md
M	docs/manual/explorer-selection.html
A	docs/manual/images/v2-folder-delete.png
A	docs/manual/images/v2-folder-move-paths.png
A	docs/manual/images/v2-folder-open.png
A	docs/manual/images/v2-surface-grid-all.png
A	docs/manual/images/v2-surface-grid-off.png
A	docs/manual/images/v2-wire-under-face.png
M	docs/v2/implementation-work-packages.md
M	src/apps/cad_next/V2CommandState.cpp
M	src/apps/cad_next/V2DeleteCommands.cpp
M	src/apps/cad_next/V2EntityTree.cpp
M	src/apps/cad_next/V2ExplorerBuild.cpp
M	src/apps/cad_next/V2ExplorerMenu.cpp
M	src/apps/cad_next/V2ExplorerUpdates.cpp
M	src/apps/cad_next/V2GridDock.cpp
M	src/apps/cad_next/V2GridDock.h
M	src/apps/cad_next/V2GroupCommands.cpp
M	src/apps/cad_next/V2LoopFacesTool.cpp
M	src/apps/cad_next/V2PlaneCommands.cpp
M	src/apps/cad_next/V2SelfTest.cpp
M	src/apps/cad_next/V2SelfTestExplorer.cpp
M	src/apps/cad_next/V2SelfTestExplorerUpdates.cpp
M	src/apps/cad_next/V2SelfTestSurfaceEdit.cpp
M	src/apps/cad_next/V2Shelves.cpp
M	src/apps/cad_next/V2SurfaceEditTool.cpp
M	src/apps/cad_next/V2SurfaceEditTool.h
M	src/apps/cad_next/V2ToolKeys.cpp
M	src/apps/cad_next/V2ToolRoleLabels.cpp
M	src/apps/cad_next/V2ViewCommands.cpp
M	src/apps/cad_next/V2ViewportDraw.cpp
M	src/next_occt/kachakacha/kernel/OcctGptSurface.cpp
M	src/next_occt/kachakacha/kernel/OcctThicken.cpp
M	tests_v2/gpt_fabrication_tests.cpp
```

完了記録更新後の短い4検査も成功。検証候補との入力差分はWP-08完了欄の文書1ファイルだけで、製品・試験・manualの差分は0。公開済み配布フォルダの全バイト列は証拠と一致（completion-metadata.json）。
