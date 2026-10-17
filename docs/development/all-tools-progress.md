# 全ツール改善 2026-10-17 進行記録

基点2c9efb09e。依頼: 全ツールの操作不能・UI更新停止・遅延を修正し、重複整理、一般語と区別できる絵、全ツールの具体例別画像付き手順書を作る。最終Windows全体ゲート・配布・commit/push必須。未完了で完成扱いしない。

## 実施済み（未コミット）
- 読取専用tool_audit/manual_audit調査終了。168コマンド、個別記事168だが番号手順/個別画像なし、15群共通画像のみ。面7方式/測定5/作業面12等は別途必要。
- EndArmedTools/EndOwnedToolsButをV2ToolLifecycle.cppへ移動。EndArmedに押し出し・reference pick・temporary page終了、選択保全を追加。SelectToolもこの共通終了を呼び、所有ツール間切替でもlegacy状態を片付ける。Thicken開始もEndArmed。
- RefreshCommandVisibility/BuildFacts/CommandEnabledをV2CommandState.cppへ分離し、1更新でfactsを共用（旧168回）。Drawing QActionのtoolAvailableを明示し、Ribbonのproxyも論理可用性を見る。
- 別ダイアログのEnter/Escを背後ツールが取らないようwindow/modal条件追加。
- Solid/EdgeFinishのbuilt_解放、成功時の明示所有権移譲。SolidのBoolean中間made handleの解放は未対応、要確認。
- ToolPick中の詳しいstatusがReportSelectionCountで上書きされないようにした。
- 新V2SelfTestToolContracts.cpp: 21ツールの全441組切替+各モード+L shortcut、modal、preview native cache lifetime。登録はUsabilityCases()から。
- V2ToolEvidence(.cpp/.h): KACHACAD_TOOL_AUDIT_DIRが指定された自己試験で、全commandの前後PNGとテストごとのcommandログを出力する。画像はcommand returned時点で確定成功の証拠とは表示しない。V2SelfTest RunOneCaseとRunCommandへ接続。
- 重複Ribbon入口3個を削除（押し引き=押し出し、結合=足す、半径編集=近似部品編集）。予約Blocked6個はデータには残し、Ribbon/Chooserでは表示しない。対応テスト更新。
- 名称: GPT版→曲線から近似面、ルールド→2本を直線でつなぐ、離した面→オフセット面、回転面と回転体区別、中空化、面の縦横線。保存enum/IDsは変更なし。
- 新V2ToolIllustrationsでBoolean3種、surface7種、measure5種、shell、回転面/立体、曲面投影/巻付け/押出しの絵を描き分け。measure variantも渡すよう変更。Ribbonは32px絵/文字下、ツール54px、カテゴリ32pxへ。

## 現在の実行
- build session52240、out/all-tools-contract5-build.log（ただしその後V2Ribbon category minheight54→32を書換えたので次buildで反映必要）。
- 他のexec sessionsは完了済み。
- 最後のHP-TOOLS focused3: 切替441自体は通過。part moveが無効→toolAvailable修正済。preview revolveは成功、filletは同じwindow内に前のprofile/axisが残り撮影helperが誤ピック→file.newで分離済（未再試験）。初回エラーはfixtureではなくスクリーンhelperが選択成功を検証しないため。旧Status「選択1件」は原因情報ではなかった。
- HP-TOOLS最初のmode failureはInstructionsのtemporary page/hidden shelfを旧toolと誤集計した試験側問題で修正。shortcut failureは前のmode loopの最後InstructionsでL無効だったため各caseをDrawingへ戻すよう修正。

## 残作業
1. 新実装のbuildとHP-TOOLS再試験、旧tool suitesで退行修正。新アイコンの違い/予約非表示/重複排除のUI試験とPNG書出しを追加。
2. 168の具体例レシピと共有ID方式別例（Workplane12/Circle/Arc/Spline/Scale/Surface/Measure/Dimension/Approx等）を作成。まだ未着手。
3. docs/manual/build_manual.py再利用でtoolごとの番号手順・入力/結果図・設定/取消/失敗対策・実画面と模式図の出典を生成。個別ページと索引を作る。手書き追記が消えないよう生成元へsolid-trim/section-deleteリンクを保全。olddate2026-10-05/source d767ab58d固定を解消。edit.delete旧拒否説明も更新。
4. Windows全UI試験をKACHACAD_TOOL_AUDIT_DIR付きで実行し、command別スクリーンと対応試験を収集。未選択到達試験と有効入力の成功を混同しない。手順図は模式図なら明記。
5. 説明書の具体例・方式・全コマンド網羅・ローカルリンク/画像等を厳しく検査する新テストと短いpreflight。必要な範囲をWindowsで確認。
6. 全体194 CTest+増分(UI現状507+3=510予定)、app/package/legacy。全体後publish、レポート更新、所有ファイルだけcommit/push。

## 守ること
ユーザーCADが旧out/releases/quick-display-full2-20261017で起動中。止めない。untracked .worktrees/、Claude outputs/、self-test-progress.txtは他の物なので触らない。元115面.kcd2は読取のみ。


## 更新 2026-10-17（継続作業）
- 旧pendingCommandが選択復元で再起動する退行を修正。EndArmed/EndOwnedの先頭でClearPending、point/reference pickも解除。
- QScopedValueRollback(mirroring_)でSolid/Edge確定中のselection callback再下見を抑止。Boolean中間handleはRAII、tool destructorはnative previewだけ解放。確定後STEP検査・保存再読込・窓破棄・Boolean再下見cache一定を追加検証。
- HPTOOLS切替は441通りについてActive==1も確認（同じoffsetはトグル解除が正しい）。最終4件pass。HPFA単独pass/順序ありfailは起動前に固定した画面座標が原因候補、起動後レイアウトを処理し再投影へ修正。再試験pass。
- 直線の法線作業面がcurvature normal要求で拒否される問題を修正。core受入試験pass。支持面入力が未接続の作業面3方式は理由付き選択不可、制限を文書化。
- 初回Windows511件は504pass/7fail。失敗はClearPendingと画面座標の修正で再試験通過。後の試験改善によるoffsetトグル偽failも修正。全511通し直し実行中。
- 図付き手順は168コマンド＋68方式例を実装。tool_recipes/variantsはreadonlymanual担当と照合しUIにない入力欄・未対応複数面・即時実行順等の誤記を修正。tool_proceduresがページ/SVGを生成、生成物とlocal links/タグ/全件/方式数を厳密check。
- 全コマンドの実アイコン180PNGをtool-iconsへコピー。実画面は最終Windows全試験終了後import_tool_evidence.pyで取り込み予定。表示手順図は模式図と明記。
- Browserのfile URL表示はセキュリティ規則で拒否。回避はしない。HTML整合・SVG XML・リンク/画像の静的検査は実施済。
- preflight初回はarchitectureの101行関数・Qt直接include不足で失敗。OpenTemporaryToolへ分離＋include2件追加後architecture単体pass。最終full前に再度preflightが必要。
- 出力reportは失敗時short-circuitでSaveされなかったため、失敗時にも必ずJSONを保存するよう修正。


## 最終ゲート開始（全体ゲート4）
- Windows実画面の全511件通過、スクリーンショット168件を試験名・日時・exeSHA付きで取り込み済。
- その後の実装変更はアイコンの描き分け追加、近似preview_updateのmetadata訂正のみ。predicateをHasDocument、案内を未確定プレビュー更新へ整合。実行処理は元からBeginToolFirstCommandで先取りされるので変更なし。HPTOOLS切替＋更新が文書を変えない追加確認pass。
- 説明書の追加訂正: key_pointsは直線の端点・中点の例、preview_updateは既存モデル更新ではなく未確定下見の更新、STEP出力対象表を現実装へ。最終readonlyreview後は内容を固定。
- 最初の全体ゲートは説明書の誤記修正で中断（成功扱いしない）。全体ゲート3もmetadata誤案内を合わせて直すため早期中断。ロックの所有pid/runを検査して自分の検証プロセス木だけ停止、終了済みの自分のロックのみ除去。ユーザーCADには触れない。
- 現行最終コマンド: python scripts/verify.py full --parallel 2 --run-id all-tools-full4-20261017
- log out/all-tools-full4.log、exec session32062。完了後result.jsonを確認→publish→auditレポート最終結果へ更新→所有ファイルcommit/pushが残る。


## 全体ゲート5
- 全体ゲート4の配布版511件はHP-AP-06が1件失敗。全体成功として扱わず停止した。
- 面のネイティブ境界と元ワイヤが画面上で重なるため、旧試験helperが元ワイヤの代わりに面を拾っていた。HoverAt→候補切替(Tabと同じ経路)→SelectAtで元ワイヤを区別し、拒否理由の厳しい確認を維持。offscreen/WindowsでHP-AP-06両方pass。
- 通常の右ペイン更新で入力拒否理由が消えないよう保持し、明示的な再計算/終了で解除する。readonly差分レビュー追加指摘なし。
- python scripts/verify.py full --parallel 2 --run-id all-tools-full5-20261017 を実行中。source/manualは固定し、以後は試験結果と開発報告だけ更新する。


## 全体ゲート6
- 全体ゲート5は本体UI511/511、配布版UI511/511、CTest193/194。唯一の失敗はShellSplitMethodNameJaとfooterの期待文字列が旧「シェル」のまま（製品は新「中空化」）。tests_v2/shell_split_input_state_tests.cppの2文字列を仕様に合わせ、単独3/3pass。
- 完了した193件の成功と全体成功を混同しない。全体ゲート6を新しいソース同一性で開始。製品コード・マニュアルの追加変更なし。
- python scripts/verify.py full --parallel 2 --run-id all-tools-full6-20261017、out/all-tools-full6.log、exec session7333。


## 完了検証
- 全体ゲート6 passed。preflight9/9、CTest194/194、本体UI511/511、配布版UI511/511、旧版--self-test成功。CTest実時間1288.00秒、本体607.54秒、配布632.44秒。過去条件との速度削減率には用いない。
- 検証記録 out/verification/all-tools-full6-20261017/result.json。全体ゲート5は失敗記録のまま保持。
- publishはソース・設定・環境・成果物ハッシュと試験範囲を検査し、out/releases/all-tools-full6-20261017へ複製する。
- 全168項目の具体例付き手順、68方式例、236模式図、180実アイコン、168操作直後のWindows画面。図と実画像、操作直後と確定結果の証拠を区別。
- 既存追跡ファイルの削除なし。3重複入口と6予約入口のみ画面から整理。未対応の作業平面3方式は理由と代替案を表示。
- 残る制限：任意の全形状での無不具合保証ではない。手順書のブラウザ目視はfile URL制約により未実施。数値の速度削減率は未測定。
