# HANDOVER — 次の担当へ(2026-09-19、2026-09-22 追記 2 回)

この文書は、次にこの作業を無人で続けるモデル(いまより能力の低いモデルかもしれない)向け。
読んだらまず `TASK_LEDGER.md` の一番下の単位と `REGRESSIONS.md` の一番下を見て、どこまで進んだかを確認すること。

## 2026-09-22 追記: 自由曲面と入力の数

- オーナーは 09-22 に指示書 `kachakachaCAD_Opus5_surface_multiselect_implementation_prompt.md`(自由曲面と任意個数入力)を
  出した。09-19 の「新機能は作らない」はこの指示書の範囲では上書きされている(面の作り方・面の編集・面の解析・おまかせ)。
- 進み具合は `TASK_LEDGER.md` の「自由曲面と入力の数」、各機能の状態と入力の数の表は `UI_FEATURE_MATRIX.md`
  (D-26〜D-42、「入力の数(CARDINALITY)」)。PC で ctest・自己試験を通したものは PC_TESTED(画面の目視はまだ)。
- 面の役割の数は `modeling/SurfaceCardinality` の 1 か所。`guides.size() == 2` のような決め打ちを足さない。
- 核(OCCT)のコードは雲ではコンパイルされない(雲の kernel 試験は空)。include と API の誤りは PC でしか出ない。
  足した OCCT の型は必ず `#include` する(不完全型で MSVC が落ちた)。
- MSVC は並び(std::vector)の伸ばし方が GCC と違う。**並びへ足したあとで、足す前に取った参照・ポインタを読まない**
  (PC だけで落ちる)。
- PC の回し方: `git bundle create /mnt/user-data/outputs/to-pc.bundle v2-wp01 --not origin/main` → 送る →
  `C:\Users\tak01\github\kachakachaCAD\to-pc.bundle` へ置く → Git CMD で `cmd /c .\_QUICK.cmd`(結果は
  `_claudeout\quick_*.txt`、1 行ずつ utf-8 → cp932 で読む)。全部緑なら `_GO.cmd` が GitHub
  (`codex/v2-wp01-build-scaffold`)へ送る。
- **試験が全部緑でも、絵を見る。**`cmd /c .\_SHOTS.cmd` が ui/11(おまかせ)・12(面の解析)・04・06・07・08 を撮り直す
  (`_claudeout\ui\*.png` を雲へ上げて見る)。09-22 は ui/12 で、両端のガイド 2 本 + 断面 3 本の面が断面の間で
  7 つに波打っているのを見つけた(ずれは 0、試験は全部通っていた)。ロフトは外側のガイドが両脇にあれば
  断面とガイドの網(Gordon、`LoftSolver::RailNetwork`)で作るようにした(1405941)。
- 閉じた断面にガイドを付けたロフトは作れない(検査が「まだ作れません」と断る)。作るなら、断面をガイドの
  交わりで開いた線に切り、ガイドの間の帯ごとに網を作って縫い合わせる作りが要る(未着手)。

## 2026-09-22 追記(続き): 残りの段 — 作図・部品・測定の道具を足した

- オーナーの「続けろ」で、matrix の BLOCKED_BACKEND のうち核か core で作れるものを本物にした:
  部品の配置 P-18、回転体・ロフト立体・スイープ P-08/P-09、3点の円・中心の円弧・通過点スプライン
  D-04/D-08/D-13、スケール D-22、辺のフィレット・面取り P-12、シェル・分割 P-13、閉じた線の面積 C-15。
  commit と PC の結果は `TASK_LEDGER.md` の「残りの段」、各行の状態は `UI_FEATURE_MATRIX.md`。
- **自分の棚を持つ道具**(立体を作る V2SolidTool・辺の丸め V2EdgeFinishTool・シェル分割 V2ShellSplitTool)は
  窓の友達で、棚(Dock)と入力の状態を自分で持つ。棚の出し方は `ShelvesFor(..., ownedShelf)` と
  `V2MainWindow::OwnedToolShelf()`、窓へのつなぎは 8 か所(V2MainWindow.h の friend と持ち物・
  HandleSelectionChanged・AdoptDocument で End・V2Shelves で作る・DockForShelf・OwnedToolShelf・
  V2ToolKeys・V2StatusLine・V2BooleanCommands の BeginToolFirstCommand・V2RebuildCommands の RebuildOneShape)。
  **1 つだけ構える**(BeginToolFirstCommand の endOwnedToolsBut)。構えたまま別の道具を押すと、
  押しが両方へ入って棚が前の道具のまま残っていた。
- 立体の辺は **辺の真ん中の点**、面は **面の上の点**(ほかの面から 0.4 mm 離した点)で文書に残す。
  番号は作り直しで並びが変わる。1 本・1 枚でも見つからなければ作らない(黙って残りだけ作らない)。
- 分割は 1 回で 2 つの Feature(side ±1)。平面は「いまの作業平面 + ずらす」。平面が通らなければ KER-H005。
- **名前を決める前に grep する。**`PartEdit` は製作の部材編集(F-05〜F-07、V2SelfTestPartEdit.cpp)で
  既に使われていて、同じ名前のファイルを上書きしかけた(git の元から戻した)。シェル・分割は `ShellSplit`。
- OCCT の枝は雲でコンパイルできない。`sh tools/occtstub/check.sh <file>` で **自分の C++ の誤り**だけは拾える
  (Result に operator! は無い、など)。OCCT の API そのものは PC のビルドでしか分からない。
- 新しい道具の絵は `cmd /c .\_SHOTS_TOOLS.cmd`(PC、_QUICK の後)で `_claudeout\ui\13〜17_ui-tool-*.png`。
  場面は `V2UiShotStatesTools.cpp`(試験ではなく場面づくり)。
- まだ作れないもの(押せない形 + 理由のまま): 楕円・テキスト(文書に種類が無い)、面オフセット・面削除・
  面置換(核に道が無い)、表裏反転(製作契約 §9 が型紙の鏡映を禁じている。オーナーに要確認)、
  押し出しの開始面 From・テーパー、平均誤差・誤差の色(近似が点ごとの外れを返さない)。

## 目的と方針(オーナー)

- 新機能は作らない。**実用に耐える UI にする**ことが今回のゴール。
- 「できないことを、できたことにしない」。backend に無い機能は disabled + 日本語の理由をつける。見た目だけの実装済みにしない。
- 禁止事項:
  - `git stash` / `git reset --hard` / `git rebase` / force push
  - テストの削除・skip・assertion を弱める
  - 他の AI が作業しているブランチの上書き

## 毎回の門(コマンドをそのまま実行)

```
cd /root/kachakachaCAD && timeout 590 sh tools/qtstub/typecheck.sh . g++
```
→ 期待する出力: `qt stub typecheck: OK (N files)`

```
cmake --build build-core --parallel 8
```

```
(cd build-core && ctest -E qt_stub)
```
→ 期待する出力: `100% tests passed`(100% でなければ何が落ちたか読んで直す)

新しい `tests_v2` を追加したときは `kachakacha_add_v2_test(...)` で登録し、`cmake -S . -B build-core` を通す。

門(守ること):
- 関数は 100 行まで、ファイルは 1500 行まで(超えそうなら別ファイルへ分ける)。
- `cad_next` の `.cpp` は使う Qt 型ごとに `#include <QType>` を書く(暗黙の伝播に頼らない)。
- 新しいコマンドを足すときは catalog + `V2Menus.cpp` + manual の 3 箇所を揃える門がある(いずれかが漏れると typecheck か ctest で落ちる)。
- Qt stub に型や関数が足りないときは `tools/qtstub/QtStubWidgets.h`(`include/` はその symlink)に最小限の宣言を足す。

## コミット

コミットメッセージの末尾に必ずこの 2 行をつける:
```
Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01PhuCKP4297isBKknmQumJ3
```
雲の環境からは push できない(proxy が 403 を返す)。PC へは下記の bundle 経由で渡す。

## PC への渡し方

1. `git bundle create /mnt/user-data/outputs/to-pc.bundle v2-wp01 --not origin/main`
2. `SendUserFile` でこの bundle をユーザーに渡す。
3. `mcp__remote-devices__device_commit_files` で `C:\Users\tak01\github\kachakachaCAD\to-pc.bundle` へ書き込む(既存があれば force)。
4. PC 側で `_GO.cmd` を実行する。基本はオーナーが自分で押す。Claude が押す場合は計算機操作(コンピュータ操作)で「Git CMD」を開き、
   - `cd /d C:\Users\tak01\github\kachakachaCAD` → Enter
   - `cmd /c .\_GO.cmd` → Enter
   の 2 行に分けて打つこと。`&&` でつないで 1 行にすると動かない。

## 結果の読み方

- `mcp__remote-devices__device_stage_files` で次の 2 ファイルを取ってくる:
  - `C:\Users\tak01\github\kachakachaCAD\_claudeout\ctest.txt`
  - `C:\Users\tak01\github\kachakachaCAD\_claudeout\run.txt`
- 読み方:
  - `grep -a -B8 "^FAIL" ctest.txt` → 期待が外れたところの直前 8 行を見る
  - `grep -a "cad_next self-test:" ctest.txt` → 自己試験の合計(例: 291/291)
  - `grep -a "Timeout\|Failed" ctest.txt` → タイムアウトや失敗の有無
  - `RUN <ケース名>` の後に対応する PASS/FAIL の行が無ければ、そのケースでハングしたか落ちたということ。
- 絵(スクリーンショット)は `_claudeout\ui\*.png` と `_claudeout\resp\v2-<state>-<WxH>-<scale>.png`。`device_stage_files` で取り込んで `Read` で見る。

## 自己試験の場所と名前の付け方

- 名前は `HP-XX-NN` 形式(例: HP-AR-01)。
- 各カテゴリのファイル(`V2SelfTestHumanPath.cpp` など)の `*Cases()` 関数に登録する。
- 登録は `V2SelfTest.h` / `V2SelfTest.cpp` と `CMakeLists.txt` の両方に反映されているか確認する。
- 「人の道」を守る: 実際の 3D クリック(`Viewport().ClickAt` / `SelectAt`)や見えているボタンを使う。hidden widget を直接操作したり `SelectionRef` を手で組んで注入するような近道は禁止。
- Preview と confirm(確定)は同じ snapshot から行う。Preview の後に選択を読み直さない。

## 未了の一覧(優先順)

1. 4 回目の PC 結果(`_GO.cmd`、5e4c27f 以降)の確認と手当て。
2. 右ペインを 2 ページに整理する(製作ページ: 製作+数、出力ページ: 書き出し+型紙)。
3. C-10 道具パネルの共通枠(各節の並び順)を整える。
4. 0%/100% の重ね比較表示。
5. `guide.create`(形状ガイド(旧))の献立整理。
6. `V2MainWindow.h` の分割(現在 1499 行、上限 1500 行に迫っている)。

## 参照

- `docs/v2/ui-redesign/UI_FEATURE_MATRIX.md`(正本)
- `docs/v2/ui-redesign/TASK_LEDGER.md`
- `docs/v2/ui-redesign/UI_LESSONS.md`
- `docs/v2/ui-redesign/REGRESSIONS.md`
- `docs/v2/ui-redesign/HOWTO-実用手順.md`
- `docs/v2/ui-redesign/mocks/`
- `docs/v2/core-ui-wiring.md`
- project doc `claude/報告書-2026-09-19-PC検証と実用UI.md`

## 追記 2026-09-19 夜(PC 4 回目・5 回目のあと)
- PC 4 回目(5e4c27f): 自己試験 292/293。残り HP-RS-01 を 8005c20 で手当て(面の道具をやめたら欄を空に)。5 回目(8005c20)は Claude が Git CMD から起動した(20:48 頃)。結果は `_claudeout\ctest.txt` の `cad_next self-test:` 行を読む。
- 計算機操作の癖: `_GO.cmd` の終わりは `pause`(「続行するには何かキーを押してください」)で止まっている。次を打つ前に Enter を 1 回押して促しを消す(打った先頭の 1 文字が食われる)。計算機操作の許可は 30 分操作が無いと切れる(再度 resolve → request)。`cd /d ... && _GO.cmd` の 1 行は動かない。`cmd /c .\_GO.cmd` と打つ。
- Fable(上位モデル)の利用上限に近い(98%、9/24 21:00 にリセット)。下位モデルで続けるときは、このファイルの手順と `TASK_LEDGER.md` の最新節だけ読めば足りるように書いてある。判断が要ることは増やさず、(1) 5 回目の結果の読み取り → (2) 落ちた試験の「期待が外れた」行から原因を 1 つずつ → (3) 門 → commit → bundle → PC、の輪を回す。
- 1.5 倍の絵(`resp/*-1.5.png`)は 1920x1080。作図の棚の作り方カード(3 枚ごとに折り返し)と状態行の右端が切れない絵になっているかを見る。

## 追記 2026-09-20 — 統合段(Integration)は完了
- PC 8 回目(36cbfb6)で ctest 157/157・自己試験 296/296。統合段の 4 つ(人の道の回帰 / 画面寸法・拡大率 / 古い重複 UI の片づけ / PC 検証)は TASK_LEDGER の「2026-09-20 PC 検証 8 回目」に現在地を書いた。
- **残している未達は 2 つだけ**(どちらも台帳と matrix に書いてある):
  1. Windows の表示倍率そのものを 100%/125% に変えた確認(検証機が 150% 固定。いまは QT_SCALE_FACTOR で代用)。
  2. 製作モードの右が `{Fabrication, Parameter}` の 2 枚。数の棚(板厚・許すずれ・型紙の余白・縮尺)を単独で出す命令が無いため。直すなら「数の棚を出す命令を足す」か「残りの数を製作の棚と書き出しの棚へ畳む」のどちらか。
- 次にやるなら(優先順): (1) 上の 2 つ (2) 右ペインの C-10(道具パネルの節の並びを共通の枠に) (3) 0%/100% の重ね比較 (4) V2MainWindow.h / V2Viewport.cpp の分割の続き。
- 計算機操作で `_GO.cmd` を回す手順は上の追記のとおり。オーナーから Git CMD の使用許可は出ている(2026-09-20)。許可は 30 分操作が無いと切れるので、`computer_resolve_access` → `computer_request_access` をやり直してから使う。
