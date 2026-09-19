# HANDOVER — 次の担当へ(2026-09-19)

この文書は、次にこの作業を無人で続けるモデル(いまより能力の低いモデルかもしれない)向け。
読んだらまず `TASK_LEDGER.md` の一番下の単位と `REGRESSIONS.md` の一番下を見て、どこまで進んだかを確認すること。

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
