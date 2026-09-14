# AI Handoff State (Claude が更新する。Codex は書き換えない)

このファイルは Claude だけが更新する。Codex の返答は `CODEX_REVIEW.md` に書く。
互いのファイルを書き換えないことで、同じファイルへの同時投入を避ける。

## 現在

**レビューの通し方が変わった。** Codex に10分ごとに仕事を探させるのはやめた。
PC が本当にビルドしてテストに通ったときだけ、PowerShell の常駐が Codex を1回起こす。
仕組みは `docs/ai/LOCAL_REVIEW_PIPELINE.md`、Codex 側の規約は
`docs/ai/CODEX_REVIEW_POLICY.md` にある。依頼は `tools/ai-local/next-review.json`
をコミットに含めて出す。**REQUEST_ID と BASE は Claude が決め、HEAD は機械が決める。**

REQUEST_ID: AI-REVIEW-PIPELINE-R8
  実際に固定されるのは区間ごとの依頼で、それぞれ別の REQUEST_ID・別の範囲を持つ。
  - AI-REVIEW-PIPELINE-QUEUE-R8   … review-dispatcher / review-enqueue /
      review-recover / stop-stale-dispatcher
  - AI-REVIEW-PIPELINE-PROCESS-R8 … review-common / review-runner(HIGH_RISK)
  - AI-REVIEW-PIPELINE-JUDGE-R8   … review-profile / review-ledger /
      review-precheck / queue-status / clear-hold
  - AI-REVIEW-PIPELINE-TESTS-R8   … review-selftest / tests_v2/architecture_tests.cpp
  - AI-REVIEW-PIPELINE-DOCS-R8    … docs/ai/ / AI_HANDOFF_STATE.md / AGENTS.md /
      .gitignore(QUICK)
  BASE は5区間とも f78d91b。HEAD は PC が固定する。
  連続 BLOCKING の数は区間ごとに別々に数える
TASK_ID: Claude/Codex レビューのローカル・イベント駆動基盤
PHASE: 基盤
STATUS: **HUMAN_DECISION_REQUIRED**(R5 で16件、R6 で17件、R7 で14件。
  合計47件すべて直したが、5区間とも3回連続 BLOCKING に達した。
  規約どおり基盤が受付を止めている。再開は人の判断で)
REVIEW_STATUS: PENDING_CODEX
BASE: f78d91b
HEAD: (PC が決める。ビルドして試験に通った commit)
REVIEW_SCOPE: 上の5区間が示す範囲。区間の合計が、この依頼の全体
REVIEW_FOCUS: 依頼が無いのに Codex が起きる経路が無いか。二重レビューが
  防げているか。レビュー中に HEAD が進んでも対象が動かないか。
  落ちた後に queue が戻るか。想像した CLI option が混じっていないか
BUILD: PC で確認する
TEST: 雲 core 134/134、Qt 当て木、静的検査(PowerShell 5.1 で動かない構文が無いこと)
  ＋ PC で `review-selftest.ps1`(35 の場面・86 の確認)
CREATED_AT: 2026-09-14
UPDATED_AT: 2026-09-14

### 次に出す(新しい経路で)

- P1-EXTRUDE-R5 … R4 の指摘を直した分。BASE は R4 の HEAD、HEAD は PC が決める
- Q1-Q5-R4 … R3 の指摘を直した分。同上

R1〜R4 は**レビュー不成立**の履歴として残す。判定は一度も出ていない。
**書き換えない。**不成立を FAIL と呼ばない(規約 §3 と同じ理由)。

### 解決済み: Codex CLI の場所(2026-09-14)

オーナーが Codex CLI を入れてくれた。基盤は自分で見つけた。

```
C:\Users\tak01\AppData\Local\Programs\OpenAI\Codex\bin\codex.exe
```

`.codex\.sandbox-bin`(殻)と `.codex\plugins\.plugin-appserver`(アプリの内部部品)は
候補の最後に回すので、こちらが選ばれる。`AI-REVIEW-PIPELINE-R4` から
**本物の Codex がレビューしている。**

以下は、そこへ至るまでの記録として残す。

### 経緯(CODEX_INSTALL_ATTENTION)

**この PC で Codex CLI が実際には動けない。**基盤は正しく動き、Codex も
正しく断った。ごまかしていない。事実だけ並べる。

- 見つかった実行ファイル: `C:\Users\tak01\.codex\.sandbox-bin\codex.exe`
  (`codex-cli 0.153.4`。`codex exec` の option は `--output-last-message`,
  `--ephemeral`, `--sandbox`, `-C`, `-c` すべて揃っている)
- 実際に起動した結果(AI-REVIEW-PIPELINE-R2、2分):
  「`codex-code-mode-host.exe` が無く、規約も packet も差分も読めない。
  推測で判定は作らない」
- つまりこれは**殻(shim)**で、本体が隣に無い。`.sandbox-bin` は最後に回し、
  隣に host が無いものは「使えない」と断るようにした。
- ほかに `codex` という名前の実行ファイルは、探した範囲(PATH・npm・WinGet・
  pnpm・yarn・cargo・bun・`.local\bin`・`Programs` の3段下)には無かった。
  全記録は `.ai-runtime\logs\reviewer-search.json`。

**この依頼は済んでいます。**オーナーが Codex CLI を入れ、基盤が
`...\AppData\Local\Programs\OpenAI\Codex\bin\codex.exe` を自分で見つけました。
R4 以降は本物の Codex がレビューしています。上は R1〜R3 当時の記録です。
`KACHA_CODEX_EXE` を指定すれば今でもその実行ファイルだけを使います。

### 人が見るところ

- いまの queue: `tools\ai-local\queue-status.cmd`
- 常駐が止まっていたら: `tools\ai-local\start-dispatcher.cmd`
- レビュー結果: `.ai-runtime\results\<REQUEST_ID>.md`
- 起きたこと全部(追記専用): `.ai-runtime\logs\review-ledger.jsonl`

## この固定範囲で、ほかに直したこと(Codex の指摘の外)

Codex の指摘を直す途中で、**同じ形の間違いがもう1か所** 見つかったので直した。

### 押し出しが選んだもの全部を輪郭にしていた(EXT-001)

`ExtrudeProfilesFor(selection.entityIds)` と書いてあり、足す・引くで
一緒に選ぶ立体まで輪郭に数えていた。だから
「輪郭が同じ平面に載っていません」で断られていた。
棚には「対象立体」と「輪郭」が別々に出ている。読み取りは正しく分けている。
**実行だけが選択を読み直していた。** R2 の B2 で直したのと同じ形である。
記録する `definition.profiles` も同じだったので直した。

### 押し出しの向きの決めどころが2つあった(EXT-007)

矢印と下見は輪郭の平面へ向くようにしたのに、確定だけが作業平面の法線を
使っていた。別の平面に引いた輪郭(HO の窓など)を選ぶと
「この向きでは厚みが出ません」で断られていた。確定も同じ1か所から取る。

### HO の見本の面が一度も作られていなかった

面の作り方が持つ「鎖」の指し先は **1本のワイヤー** であって曲線1本ではない。
曲線の数だけ指し先を並べていたので `UI-R006` で断られ続けていた。
V1 の見本(`RailwayNoseSample.cpp`)は最初から正しく書いてある。
これを捕まえる core 試験を足した(雲で止まるので PC の往復が要らない)。

作り方は PC で全部試して選んだ。断面を通すロフトだけが通る。
案内付きロフトは断面から 0.19mm 外れ、曲線網は面が張れない。
**使われない案内線は全部消した**(§44)。

### 雲で捕まえられなかった間違いを、雲で捕まえるようにした

往復が 12 分かかるので、PC でしか出ない間違いは高くつく。門を2つ足した。

- `tools/bounds-check.sh`: 並びの外読み。`_GLIBCXX_DEBUG` つきで core 全試験。
  Windows の Debug は `vector subscript out of range` で止まるが、
  雲の Release + g++ は黙って素通りする。**実際に1件出た**(`TrimCurve`)。
- `architecture_tests` の「画面の関数の中身がある」: 宣言だけ残して中身を消した
  関数。型検査も CMake も見ないので、PC の link で初めて分かる。
  **実際に1件出た**(`ApplyBandBoundaries`)。

## P1-EXTRUDE-R3 の指摘と、どう直したか

Codex は R3 を **FAIL** にした。3件とも本物だった。

### B1 内側の取りやめを外側が飲み込む

`AbortCompound()` は入れ子の深さが残っていると深さを1つ減らすだけで、
外側を失敗にしていなかった。だから
「外側で A を足す → 内側で B を足す → 内側が取りやめ → 外側が閉じる」で
A も B も残る。呼ぶ側が返り値を見落としただけで、原子的なはずの操作が
半分だけ保存される。

一度でも内側が取りやめたら印を立て、いちばん外側で閉じるときにまとめて戻す。
`Transaction::Commit()` は **「本当に残ったか」を返す**(`[[nodiscard]]`)。
偽が返ったら、呼んだ側は画面と覚えている形も戻さなければならない。

### B2 文書だけ戻して、覚えている形と場面が戻らない

`AddPartFeature()` は確定の前に `partShapes_` と `partEdges_` を更新し、
場面まで作り直していた。そのあとで失敗すると、文書だけが戻り、
文書に無い立体や辺が画面と書き出しに残る。

文書を変えるところを `CommitExtrudeAtomically()` に閉じこめ、
**どの道で抜けても** 呼ぶ側が `AdoptCurrentDocument()` と
`RebuildKernelShapes()` を通すようにした。戻ったあとの文書が正本である。

### B3 Windows の Debug が止まる

`TrimCurve` が並びの外を読んでいた。押した場所を挟む区切りを探す繰り返しは
「見つからなければ最後まで進む」ので、そのまま次を読むと外に出る。
押した場所が線の外(0〜1 の外、NaN)だと必ずそうなる。
先に断るようにした(GEO-E022)。

**雲側が素通りしていたのが本当の問題である。** Release + g++ には
並びの検査が無い。`tools/bounds-check.sh` を足した。
`_GLIBCXX_DEBUG` つきの Debug ビルドで core の試験を全部回す。
PC へ束を送る前に通す。その検査つきで 134/134 が通る。

## Q1-Q5-R2 の指摘と、どう直したか

### B1 無い部材番号が最後の部材へ丸められる

部材が3枚のときに「999」と書くと、黙って3枚目の半径が変わっていた。
人は「999 は無いから何も起きない」と思っている。**丸めない。**
番号はちょうど1つ、範囲の中だけを受ける。それ以外は理由を言って断る。
見るだけ(棚の表示)は1枚目を見せる。変えるときは番号が要る。

### B2 分ける前に見せた相手と、実際に変える境目が別物

見せるほうは架空の分け方の上で「後半を動かす」判断をし、
決めるほうは選んだ部材の真ん中に境目を足していた。別物である。

`fabrication/BandPartition` を足した。**見せる形と、決めたあとに使う形を、
同じ1つの候補から作る。** `PreviewBandSplit` / `PreviewBandMerge` は
「できるか」と「できたあとの境目の並び」を一緒に返し、画面はそれを
そのまま文書へ書く。書いたあとで、言ったとおりの枚数になったかを
その場で突き合わせる。細くなりすぎる分け方は断る(折るところが残らない)。

### B3 Windows の検証が完走しない

R3 B3 と同じ `TrimCurve` である。上を参照。

## PROCESSED_CODEX_REVIEWS(処理済みのレビュー。消さない)

- 自分のミス(Codex の指摘ではない)
  `.ps1` に日本語を直接書いたため、PC の PowerShell 5.1 が CP932 として読み、
  自己試験そのものが構文エラーで落ちた(review_selftest_rc=1)。雲では気づけない。
  **レビュー基盤の .ps1 は純 ASCII に保つ。**日本語が要るところは実行時に
  コードポイントから組む。雲側の関所(architecture_tests)で毎回見る。

- REQUEST_ID: AI-REVIEW-PIPELINE-{DOCS,JUDGE,PROCESS,QUEUE,TESTS}-R7
  REVIEWED_HEAD: e0dfeec
  ACTION: FIX_AND_REVIEW(5区間すべて BLOCKING、合計14件)
  FIX_COMMIT: (この commit)
  RESULT: 14件すべて直した。R7 は BLOCKING のまま残す。

  **これで DOCS / JUDGE / PROCESS / QUEUE / TESTS は3回連続 BLOCKING。**
  規約どおり、基盤が受付を止め、人の判断を待つ(AIR-E060)。
  再開するときは `tools\ai-local\clear-hold.cmd <区間名> "理由"`。

  DOCS 4件 … STOP と3回連続の扱いが規約間で矛盾 / 不成立の台帳イベント名が旧仕様 /
    「JSON は4つだけ」の範囲が不正確 / 進捗台帳が区間ごとの実依頼と一致しない
  JUDGE 2件 … 危険な置き場所が `src/next` だけで、`src/core` の模型・保存・幾何が
    QUICK に落ちた / 途中で切れた台帳の行に次の行を連結してしまう
  PROCESS 3件 … `.ps1` のレビューアーを起動できない(探索は受け付けるのに) /
    MALFORMED や時間切れが3回連続の数に入る / 桁あふれの BLOCKING_COUNT で落ちる
  QUEUE 2件 … 復旧が他の checkout の worktree を消し得る /
    pid の使い回しで無関係なプロセスを止め得る

- REQUEST_ID: AI-REVIEW-PIPELINE-{DOCS,JUDGE,PROCESS,QUEUE,TESTS}-R6
  REVIEWED_HEAD: 6fcd411
  ACTION: FIX_AND_REVIEW(5区間すべて BLOCKING、合計17件)
  FIX_COMMIT: (この commit)
  RESULT: 17件すべて直した。R6 は BLOCKING のまま残す。R7 で出し直す。

  **いちばん重い指摘(QUEUE B1): R5 の直しが新しい穴を作っていた。**
  dispatcher は鍵を `FileShare.None` で掴むので、その鍵を開いて所有者を読むことは
  **絶対にできない**。つまり「この runtime の鍵が記録した pid だけ止める」という
  R5 の直しは、常に「所有者なし」と答え、**古い常駐を一つも退役させなくなっていた**。
  → 所有者は鍵の隣の別ファイルに書く。鍵そのものは触らない。

  QUEUE 2件 … 上記 / `StaleMinutes` で生きた owner の claim を回収し得た(二重実行)
  PROCESS 5件 … 実行直前の再検証が無い / 途中で切れた回答を PASS と受理 /
    `.cmd` の引数を cmd 用にエスケープしていない / 終了未確認で ExitCode を読む /
    git が拒否した worktree を生で再帰削除
  JUDGE 3件 … override を変えても古い cache を使う / fallback を切っても
    古い cache が残る / 台帳の追記と読み取りの競合を「破損」と誤判定
  DOCS 4件 … outcome/verdict の列挙が実装と不一致 / 解決済みの依頼が現在形で残る /
    自己試験の件数が古い / read-only が任意対応のように読める
  TESTS 3件 … 展開文字列の `$()` の中を見ていない / 鍵の非破壊試験が
    失敗し得ない書き方 / 台帳イベントの走査が空白と引用符に依存

- REQUEST_ID: AI-REVIEW-PIPELINE-DOCS-R5 / JUDGE-R5 / PROCESS-R5 / QUEUE-R5 / TESTS-R5
  REVIEWED_HEAD: f024a28
  ACTION: FIX_AND_REVIEW(**本物の Codex による初めての成立したレビュー**。
    5区間すべて BLOCKING、合計16件)
  FIX_COMMIT: (この commit)
  RESULT: 16件すべて直した。R5 は BLOCKING のまま残す。R6 で出し直す。
    所要時間は 2分半〜5分半(区間あたり)。60分級は無くなった。

  DOCS-R5 (3件)
    1. **packet の差分が文字化けしていた。**git の出力をコンソールのコードページで
       読んでいたため、日本語が二重変換されていた。レビューアーはずっと壊れた
       差分を読んでいた。→ 標準出力を UTF-8 として読む。日本語の回帰試験を追加。
    2. 文書間で欄の名前が食い違っていた(review_effort / review_profile)、
       結果の reviewer 列挙に claude-fallback が無い。→ 統一した。
    3. R1〜R4 を「FAIL の履歴」と書いていた。**不成立は不合格ではない。**→ 訂正。

  JUDGE-R5 (7件)
    1. 3回連続 BLOCKING を数えるだけで、受付を止めていなかった。→ AIR-E060 で断る。
    2. read-only にできないレビューアーも「使える」と数えていた。
       → --sandbox / --permission-mode が無ければ使わない。
    3. 台帳の書き込み失敗を無視していた。→ 判定を決める行は失敗したら止まる。
       読めない行があるときは「履歴なし」と答えず AIR-E061 で断る。
    4. `+++history_` を見出しと誤認して、危険な変更行を見落としていた。
       → `@@` で hunk の中だけを数える。
    5. REQUEST_ID を経路として検証していなかった。→ 平たい名前だけ受け付ける。
    6. repo_path を照合せず、HEAD のような動く名前も受け付けていた。
       → 現在の checkout と照合し、40桁の commit id だけ受け付ける。
    7. 状態表示が dispatcher の鍵を書き換えていた。→ 触らずに見るだけの確認に。

  PROCESS-R5 (4件)
    1. worktree の削除境界が `..` で回避できた。→ 解決した絶対パスで直下のみ。
    2. 再試行で前回の返答を成功として使い回せた。→ 起動前に必ず消す。
    3. packet を作る git の失敗を無視していた。空の packet を「変更なし」として
       レビューさせ得た。→ INFRA_ERROR にして起動しない。
    4. 規約外・矛盾した判定を正常なレビューとして確定していた。
       → 厳密に検証し、外れたら MALFORMED / 人の判断へ。番号も使い切らない。

  QUEUE-R5 (2件)
    1. 取得の経過時間を、投入時のファイル更新時刻から測っていた。File.Move は
       時刻を変えないので、長く待った依頼が取得直後に「詰まり」と誤判定され得た。
       → 取得時に時刻を打ち、owner の claimed_utc を基準にする。
    2. コマンドラインだけで dispatcher を止めていた。別 checkout の正常な
       dispatcher まで巻き込み得た。→ この runtime の鍵が記録した pid だけ止める。

  TESTS-R5 (2件)
    1. 自己試験の -WorkRoot に既存のディレクトリを渡すと消していた。
       → 自分で作った一意の子だけを作り、それだけを消す。
    2. 5.1 互換の走査に抜けがあった(文字列の中の `#` を注釈と見なす、三項を見ない)。
       → 引用符の外だけを見るようにし、三項も見る。植えた違反で確かめる。

- REQUEST_ID: AI-REVIEW-PIPELINE-R4
  REVIEWED_HEAD: e91bfc8
  ACTION: (レビュー不成立。時間切れの打ち切りが**効かなかった**)
  FIX_COMMIT: -
  RESULT: **判定は出ていない。**本物の Codex が起動し、約180KB の差分を
    effort=high で読み始めたが、1時間の上限で止まらなかった。
    原因は親プロセスだけを殺していたこと。子が出力管を掴んだままなので、
    読み取りの待ちが永遠に返らない。木ごと殺す・待ちに上限を付ける、に直した。
    あわせて、そもそも「大きい差分を全部 High」をやめた(下記)。
- REQUEST_ID: AI-REVIEW-PIPELINE-R3
  REVIEWED_HEAD: 522d903
  ACTION: (レビュー不成立。殻を弾く直しは入っていたが、**古い判定が残っていた**)
  FIX_COMMIT: -
  RESULT: **判定は出ていない。**殻を「使えない」と断る検査は入れたのに、
    前に書いた `codex-interface.json`(使える、と書いてある)をそのまま信じたため、
    また殻を起動した。検査を変えたら前の答えは答えでなくなる。
    `probe_revision` を付けて、古い検査が書いた答えは捨てるようにした。
- REQUEST_ID: AI-REVIEW-PIPELINE-R2
  REVIEWED_HEAD: 9e7f7d1
  ACTION: (レビュー不成立。Codex は起動したが殻で、ファイルを読めなかった)
  FIX_COMMIT: -
  RESULT: **判定は出ていない。**Codex 自身が「読めないので推測で判定は作らない」と
    答えた。基盤はその答えを所定の形でないものとして ERROR にした。
    以後、殻は使わない・起動できても答えが無ければ番号を使い切らない、に直した。
    AI-REVIEW-PIPELINE-R3 として出し直す。
- REQUEST_ID: AI-REVIEW-PIPELINE-R1
  REVIEWED_HEAD: 4eee58f
  ACTION: (レビュー不成立。Codex の実行ファイルが PC で見つからず ERROR)
  FIX_COMMIT: -
  RESULT: **判定は出ていない。**基盤側は起動せず、理由を
    `.ai-runtime/results/AI-REVIEW-PIPELINE-R1.json` に残した。
    当時の版が誤って「レビュー済み」として台帳に書いたため、番号は使い切られた。
    台帳は追記専用なので書き換えず、AI-REVIEW-PIPELINE-R2 として出し直す。
    以後は `review_unavailable` として記録し、番号を使い切らない。

読んだだけでは「処理済み」にしない。**直して、build して、試験を通して、
commit して、新しい REQUEST_ID で再レビューを出す** まで未解決として扱う。
前の版を PASS 扱いへ書き換えない。

- REQUEST_ID: UI-P1-007-S1-R7
  REVIEWED_HEAD: 188ea47
  ACTION: FIX_AND_REVIEW
  FIX_COMMIT: 5f6ccbc
  RESULT: 再レビュー UI-P1-007-S1-R8 を提出(R7 は FAIL のまま)
- REQUEST_ID: UI-P1-007-S1-R8
  REVIEWED_HEAD: 5f6ccbc
  ACTION: FIX_AND_REVIEW
  FIX_COMMIT: 9b942b9
  RESULT: 再レビュー UI-P1-007-S1-R9 を提出(R8 は FAIL のまま)
- REQUEST_ID: UI-P1-007-S1-R9
  REVIEWED_HEAD: 9b942b9
  ACTION: FIX_AND_REVIEW
  FIX_COMMIT: c224d49
  RESULT: 再レビュー UI-P1-007-S1-R10 を提出(R9 は FAIL のまま)
- REQUEST_ID: UI-P1-007-S1-R10
  REVIEWED_HEAD: c224d49
  ACTION: PROCEED(PASS WITH FIXES。非阻害2件も入れた)
  FIX_COMMIT: ac82541
  RESULT: UI-P1-007 は受け入れ済み。R11 は不要と Codex 自身が書いている
- REQUEST_ID: P1-EXTRUDE-R1
  REVIEWED_HEAD: ce369eb
  ACTION: FIX_AND_REVIEW
  FIX_COMMIT: 51c1258
  RESULT: 再レビュー P1-EXTRUDE-R2 を提出(R1 は FAIL のまま)
- REQUEST_ID: P1-EXTRUDE-R2
  REVIEWED_HEAD: 253e446
  ACTION: FIX_AND_REVIEW
  FIX_COMMIT: 7e35fa3
  RESULT: 再レビュー P1-EXTRUDE-R3 を提出(R2 は FAIL のまま)
- REQUEST_ID: Q1-Q5(正対・まとまり・HO見本・総合試験・曲げ半径)
  REVIEWED_HEAD: 253e446
  ACTION: FIX_AND_REVIEW
  FIX_COMMIT: 18d6e2b, bd375c9
  RESULT: 再レビュー Q1-Q5-R2 を提出(元の Q1-Q5 は FAIL のまま)
- REQUEST_ID: P1-EXTRUDE-R3
  REVIEWED_HEAD: bd375c9
  ACTION: FIX_AND_REVIEW
  FIX_COMMIT: 12bfded, 77a359d(入れ子の取りやめ・後始末・並びの外読み)
  RESULT: P1-EXTRUDE-R4 を f479814 で提出(R3 は FAIL のまま)
- REQUEST_ID: Q1-Q5-R2
  REVIEWED_HEAD: bd375c9
  ACTION: FIX_AND_REVIEW
  FIX_COMMIT: 12bfded, 77a359d(番号を丸めない・見せた候補をそのまま使う)
  RESULT: Q1-Q5-R3 を f479814 で提出(R2 は FAIL のまま)

## P1-EXTRUDE-R2 の指摘と、どう直したか

Codex は R2 を **FAIL** にした。2件とも本物だった。

### B1 失敗時の疑似 rollback が無関係な直前操作を取り消しうる

面の縁の追加が最初に失敗すると、`EndCompound()` は中身が変わっていないので
履歴を増やさない。そのあとの無条件な `Undo()` は、押し出しより前に
利用者がやっていた別の操作を取り消してしまう。

直し方。`Document` に `AbortCompound()` と、RAII の `Transaction` を足した。
`Commit()` を呼ばずに抜けたら、**履歴を増やさずに** 始める前の状態へ戻す。
一度入れてから取り消す方式では、押す前の別の操作が巻き添えになる。
ここは「無かったこと」にする。

あわせて `AdoptExtrudeResult()` が成否を返すようにし、
面の縁・全 Part / Wire 出力・Boolean 元の非表示の全部を検査して、
1件でも失敗したら transaction を abort するようにした。

### B2 面境界ワイヤーが押し出し Feature の依存関係へ登録されない

`AddPartFeature()` が Viewport の元選択を読み直していたので、
definition が指す UUID と、依存グラフが指す UUID が食い違っていた。
面の縁を直しても押し出しが計算し直されない。

直し方。`AddPartFeature()` へ入力 UUID を明示して渡す。
面の押し引きでは確定時に作った縁のワイヤー群、Boolean では対象の立体を含める。

### 足した試験(Codex の MISSING TESTS)

| Codex の要求 | どこ |
| --- | --- |
| (1) compound の最初・途中・最後で失敗、文書・版・履歴・表示が不変、直前の操作を取り消さない | core `compound_abort_tests`(6件) |
| (2) 面の縁の編集で押し出しが計算し直す対象に入る | 自己試験「面の縁を直すと押し出しが計算し直す対象に入る」 |
| (3) 面押し引きを保存・再読込して同じ形へ再構築 | 自己試験「面の押し引きは保存して開き直しても残る」 |
| (4) Boolean・輪郭併産・面押し引きが1回の Undo/Redo で往復 | 自己試験「面の押し引きは1回の取り消しとやり直しで往復する」 |

## Q1-Q5 の指摘と、どう直したか

Codex は Q1-Q5 を **FAIL** にした。

### B1 Windows のリンクが通らない

`MergeFabricationParts` / `SplitFabricationPart` / `fabrication::MergePieces` /
`PreviewMerge` / `SplitPiece` が未解決だった。
これは固定 HEAD(253e446)の時点の話で、CMake の結線は次の `ffee7bc` で
入っている。いまの `CMakeLists.txt` には
`V2PanelEditCommands.cpp`(921〜922行)と `fabrication/PanelEdit.cpp`(652行)が
どちらも入っている。**PC の往復で確かめる。**

### B2 曲げ半径が Document にも形状にも反映されない

3つとも本当だった。半径は画面が1つだけ持っていて、保存で消え、
取り消しで戻らず、形は一切動かなかった。測り方も、型紙の外周の 1/4 を
90 度と決めつけていた。

直し方。

- **測る。** `fabrication/BandBendRadius` を足した。折り線が受け持つ長さ
  L(両隣の帯の幅の半分ずつ)と、その折り線の角 θ から `R = L / θ`。
  円を多角形で近似したときの `θ = (w_i + w_{i+1}) / (2R)` そのものである。
  半径 50mm の円筒で試すと 49.87mm が出る。当て推量ではない。
- **置く。** `CreateFabricationModelDefinition` へ `bendRadiusMm` と
  `bendRadiusLock` を足した。部材ごとに持ち、保存・取り消し・やり直し・
  再計算のすべてに乗る。自動の部材は 0 を書き、どれも固定していなければ
  鍵ごと書かない(中身が同じ文書が1バイト違う、を避ける)。
- **効かせる。** 固定した半径は、その折り線の角を `θ = L / R` へ合わせる
  倍率になり、`FoldBandMesh` と `BuildBandFoldRails` へ渡る。
  帯の幅は動かさないので、どの半径でも面内長は変わらない。
  自己試験は座標の指紋を取って、形が本当に動いたことを見る。

### B3 まとまり作成と複数ドラッグが一操作一 Undo になっていない

`Document::Transaction` でまとめた。1つでも断られたら全部やめる。
輪になる移動が混じったときに、ほかだけ移って半分だけ移った状態にしない。

### B4 Q4 総合試験が近似作成ワークフローを試していない

そのとおりだった。近似済みの見本を開いて数を数えるだけだった。
`V2SelfTestApproxFlow` を足して、何も無い文書から本番の指示だけで
作業平面 → 線 → 形状ガイドの面 → 方式を見比べる → 近似を作る →
0/50/70/100% → 70% の状態から線と面を作る、まで通す。

### UX 指摘: 複数対象の正対

契約を決めた。**向きは、向きを持つ相手のうち最初の1つが決める。
収まりは選んだもの全部が決める。** 向きの違うものが混じっていたら帯でそう言う。
最後に選んだものが黙って向きを奪う作りだと、どちらの向きになるか人には分からない。
命令の一覧にも書き、自己試験で固定した。

## P1-EXTRUDE-R1 の指摘と、どう直したか

Codex は R1 を **FAIL** にした。4件とも本物だった。

| # | 指摘 | 直し方 |
| --- | --- | --- |
| B1 | 傾いた面で矢印と下見が別方向へ進む | 向きを決める場所を `ExtrudeDirectionNow()` 1か所にした |
| B2 | 下見を出しただけで文書が変わり、やめても戻らない | 縁は窓がその場限りで抱え、確定のときだけ文書へ入れる |
| B3 | 1回の押し出しが複数の取り消しに分裂する | `BeginCompound`/`EndCompound` で1つにまとめた |
| B4 | 棚で選んだ演算が確定時に既定へ戻される | 既定を当てるのは入力を最初に読んだときだけにした |

MISSING TESTS のうち、面の押し引きの取消・1操作1取り消し・選んだ演算の保持を足した。
残り(`FromWire` の外周+穴/逆向き辺の回帰、穴の途中失敗)は
カーネルの試験なので PC の往復で足す。

## UX PROBLEMS への答え

- 「面の縁」が通常ワイヤーとして一覧へ出る件。**確定するまで作らない** ようにしたので、
  やめれば1本も増えない。確定したものは押し出しの元になっているので、
  一覧に出ること自体は正しい(押し出しの記録が指している)。
  派生入力としてまとめて畳む表示は、まとまり(Q2)の上に載せる形で後続に回す。

### IMPLEMENTED(P1 で入れたもの)

- 選択の読み取り(A〜E)。立体と輪郭の順番を問わず、対象と輪郭の役割を決める
  (`app/ExtrudePlan`)。読み取り結果を日本語で出す。
- 矢印ハンドル。輪郭の重心を根元に、押し出しの向きへ出す。引くと距離が変わる。
  距離の欄と矢印は常に同じ値(`app/ExtrudeDrag`、`V2ExtrudeHandle`)。
- 右の棚(`V2ExtrudeDock`)。入力・距離・方向・方向反転・範囲・操作・結果・確定/取消。
  **いまの入力で意味のない欄は出さない。** 細かい設定は「詳細...」の窓へ回す。
- 窓(モーダル)を据え付けるのをやめた。「詳細...」のときだけ出す。
- 面の押し引き(EX-02)。`kernel/OcctFaceQuery` が面の縁・外向き法線・面積を返し、
  `app/FacePushPull` が符号つきの距離を押し出しの言葉へ言い換え、
  `V2FacePushPull` が縁を文書のワイヤーにしてから、いままでの押し出しへ渡す。
- 入力の選び直し(EX-07)。「対象を選び直す」「輪郭を選び直す」。片方だけ外れる。
- 面を拾えるようにした。`OcctTessellate` → `ShapeMesh` → `MeshPick` → 画面まで
  面番号を通し、面ごとに1つだけ候補を出す。
- **足す・引く押し出しに相手の形を渡すようにした。** 渡していなかったので、
  立体に窓を開ける押し出しは一度も通っていなかった(KER-E004)。
  開き直しの作り直しでも足し引きをやり直す。使い切った元の立体は隠す。
- **`FromWire` が辺を繋がった順に返すようにした。** 位相の並びのままだと、
  戻した線を輪郭として使えない(KER-C003)。

### BUILD

PASS(PC MSVC 2022 x64 Release / 雲 core g++)

### TEST

- PC: CTest **141/141**、アプリ自己試験 **225/225**(`f479814` で確認、2026-09-14)
- 雲: core CTest 134/134、並びの検査つき Debug 134/134、Qt 当て木 69 ファイル

### ACCEPTANCE(押し出しの受入試験 EX-01〜08)

| ID | 内容 | 結果 |
| --- | --- | --- |
| EX-01 | 閉じた輪郭 → 押し出し → ハンドル → Enter → 新規立体 | PASS(自己試験「押し出しで部品ができる」) |
| EX-02 | 立体の面 → 押し引き | PASS(自己試験「立体の面をつまんで押せる」) |
| EX-03 | 立体だけ → 何を選べばよいか言う | PASS(自己試験「押し出しが選択を読んで次を案内する」) |
| EX-04 | 立体+輪郭を順不同で選んでも役割が決まる | PASS(`extrude_plan_tests`) |
| EX-05 | 距離の欄と矢印が同期 | PASS(`extrude_drag_tests` + 棚の結線) |
| EX-06 | 方向反転で矢印も反転 | PASS(同上) |
| EX-07 | 入力の差し替え | PASS(自己試験「押し出しの入力を片方だけ選び直せる」) |
| EX-08 | 道具を替えると下見・棚・一時状態が残らない | PASS(自己試験「道具を替えると前の吸着と候補送りが消える」ほか) |

### KNOWN_ISSUES

- 面の押し引きは、押した面の縁を **文書のワイヤーとして残す**(穴があればその数だけ)。
  押し出しの記録が「どのワイヤーを押したか」で出来ており、面番号は作り直すたびに
  変わるので記録できないためである(architecture-and-data.md §6)。
  線を残さない案は保存の形を変えることになる(GUARDED)。
- 曲がった面の押し引きは断る(KER-F003)。まっすぐ押しても元の面と辻褄が合わない。
- `docs/manual.html` に3行足してある(Codex 領分。文言の確認だけ)。

### CLAUDE_NOTES(Codex に重点確認してほしい点)

1. 面の押し引きが文書にワイヤーを増やす決めごとの是非。代案は
   `ExtrudeDefinition` へ面の意味的キーを足すこと(GUARDED)。
2. 足す・引くで使い切った元の立体を **隠す** 扱いでよいか(消さない)。
   足し引きの命令(`part.boolean_*`)と同じにしてある。
3. `FromWire` を `BRepTools_WireExplorer` に替えたことの影響。
   ほかの呼び口(押し出しの結果、形状ガイド、書き出し)で並びが変わりうる。
4. 面番号(`pickedFaceIndex`)を選択に **一時的に** 持たせた扱い。保存はしていない。
5. `ApplyFacePushPull` が向きを `CustomXYZ` + 面の外向き法線で渡していること。
   輪郭の法線に任せると縁の回り方しだいで裏返る、という判断でよいか。


## UI-P1-007 が止まっていた理由と、どう直したか

`.ai/STATE.md` は「blocked: CTest failed with exit code 8」で止まっていた。
実装そのものは作業場 `kachakachaCAD-worktrees/ui-p1-007` に
**コミットされずに** 31ファイル・2250行あった。基点は `ae84ffa`。

その差分を取り出して三方向併合でこの枝へ入れた。**書き直していない。**
ぶつかったのは2か所だけで、どちらも include の並びだった。

止まっていた原因は3つとも門だった。

1. `snap_tests.cpp` が 1562行。1ファイル1500行の門を越えていた。
   持ち越しの試験を `snap_hysteresis_tests.cpp` へ分けた。
2. `V2SelfTestInput.cpp` に `#include <QEvent>` が無かった。
   2026-09-13 に入れた include の門が捕まえた。
3. `V2PartCommands.cpp` の関数が102行。100行の門を越えていた。

併合後、PC の自己試験で17件落ちた。これは併合ではなく、

- 押し出しを二段(下見→確定)にした私の変更に、自己試験19か所が追随していなかった
- Stage 1 が、吸着の入切のたびに帯を案内で上書きしていた
  (断った理由が読む前に消えた)

の2つで、どちらも直した(`188ea47`)。

## §5 の機能要件に対する現状

| 要件 | 状態 | どこで確かめているか |
| --- | --- | --- |
| 捕捉距離と解除距離の分離 | 済 | `snap_hysteresis_tests`(同順位は4px以上近くないと乗り換えない) |
| 微小移動で切り替わらない | 済 | 同上(揺れ・境目の揺れ) |
| 近接した端点/中点/中心/交点で安定 | 済 | 同上(重なった別の形へ移らない) |
| ズームで操作感が変わらない | 済 | `snap_tests`(10 / 1 / 1000 px/mm) |
| いま吸着している対象が分かる | 済 | 白フチ付きの印 + 帯の `[端点]` 表示 |
| 道具切替で残らない | 済 | core `SelectTool` で `Reset` + 画面 `DiscardHoverState`(R8) |
| 取消で残らない | 済 | core `CancelTool` で `Reset` + 画面 `DiscardHoverState`(R8) |
| 場面の差し替えで残らない | 済 | `DrawingSession::SetScene` で `Reset`(R8) |
| S キーで解除 | 済 | 自己試験「Shiftで水平になりSで吸着が止まる」ほか |
| 既存 Selection を壊さない | 済 | `selection_tests` 136行追加、全体 134/134 |
| grab-to-move を壊さない | 済 | `grab_to_move_tests` |

Stage 2 として挙がっていた結線(DrawingSession・道具切替・取消・S キー)は、
取り込んだ Stage 1 の差分にすでに入っていた。上の表のとおり動いている。

## Codex の状況

`.ai/STATE.md` の最終更新は 2026-09-13 20:34。以後応答が無い。
利用制限とみなし、オーナー指示にしたがって **CODEX_PENDING_CONTINUE** で進める。
自分の build / test / 受入試験 / 自己レビューをゲートにする。

## UI-P1-007-S1-R7 の指摘と、どう直したか

Codex は 2026-09-14 に戻ってきて `CODEX_REVIEW.md` に **VERDICT: FAIL** を書いた。
指摘は2つ。どちらも「前の状態が残る」という同じ形をしていた。

- **B1 道具を替えても前の道具の吸着表示が残る。**
  `DrawingSession::SelectTool()` は持ち越しを捨てるのに、
  `V2Viewport::OnToolChanged()` は `hover_.preview` しか消していなかった。
  `hover_.snap`・位置・案内・Tab/Alt の候補送りは前の道具の値のままだった。
  → 道具の持ち物をまとめて捨てる `DiscardHoverState()` を足し、
  いまのカーソル位置で **一度だけ** Hover を取り直す。
  帯は、いま出ているのが前の案内のときだけ書き換える。
  断った理由や確定結果は消さない(`RefreshHoverAfterToolChange`)。
- **B2 場面・文書の差し替えで持ち越しが生き残る。**
  `DrawingSession::SetScene()` が `scene_` を代入するだけだった。
  開く・Undo/Redo・作業平面切替・グリッド変更はどれもここを通る。
  → 明示メソッドにして `snapHysteresis_.Reset()` する。

直しながら **同じ穴がもう1か所** あったので一緒に直した。
`V2Viewport::CancelTool()` も、core が持ち越しを捨てるのに画面は preview しか
消しておらず、取り消した直後のリングと診断情報が session の中身と食い違っていた。

指摘された MISSING TESTS はすべて足した。

| Codex の要求 | どこ |
| --- | --- |
| (1) 直線→円弧→ベジェ→スプライン→選択、マウスを動かさず残らない | 自己試験「道具を替えると前の吸着と候補送りが消える」 |
| (2) SetScene 交換後、12px の外・16px の内で旧候補へ吸着しない | core `session` 「場面の差し替え後は12pxの外の旧候補へ吸着しない」 |
| (3) 開く/Undo/Redo/作業平面切替/グリッド変更の各経路 | 自己試験「場面を差し替えると吸着の持ち越しが消える」 + core「場面を差し替えると吸着の持ち越しを捨てる」 |
| (4) Cancel 直後のリングと診断情報が現在の状態と一致 | 自己試験「取り消した直後に古いリングが残らない」 |

非阻害の指摘(§6.1 道具に応じた吸着の強さ)は未着手のまま残す。
候補の種類を道具ごとに変える段でまとめて片づける。

## UI-P1-007-S1-R8 の指摘と、どう直したか

Codex は R8 を **FAIL** にした。指摘は的確だった。

- **B1 場面差し替え直後の旧吸着リングが画面側に残る。**
  core の持ち越しは捨てたが、画面の `hover_` を捨てていなかった。
  `SetScene` の呼び口は10か所以上ある。呼び口ごとに後始末を書けば必ず抜ける。
  → `DrawingSession::SetScene` から必ず呼ばれる知らせを1本足した
  (`SetSceneChangedCallback`)。画面はそこで `DiscardHoverState()` する。
  取り直しはしない。菜単から替えたときはポインタが画面の外にありうる。
- **試験の指摘。** R8 の自己試験は差し替えのあと `HoverAt()` を呼んでいた。
  古い `hover_` を上書きしてから見ていたので、
  「ポインタを動かさない直後」の残りを検出できていなかった。
  → 差し替え後は何も呼ばずに見る。
- **前提が緩かった。** `!heldBefore || !heldAfter` は前提が崩れても通る。
  → 「14px 先でも持ち越した端点へ吸い付いている」を必須検査にした。
- **道が足りなかった。** 実際に通していたのは Undo/Redo/グリッドの3つだけだった。
  → 開き直しと作業平面変更を足して5つにした。

## UI-P1-007-S1-R9 の指摘と、どう直したか

Codex は R9 を **FAIL** にした。今度は私が新しく入れた不具合だった。

- **B1 破棄済み Viewport のコールバックが Session に残る。**
  素の `std::function` を Session へ預けていた。画面は Session より先に消えるので、
  窓を閉じたあとの `SetScene` で消えた `this` を呼ぶ。
  → 札(`shared_ptr`)を会員が持つ形にした。`OnSceneChanged` が札を返し、
  札を持っている間だけ呼ばれる。どちらが先に消えても落ちない。
  core に寿命の試験を2つ足した。
- **`OnSceneReplaced` の条件が甘かった。** リングが出ているときだけ捨てていたので、
  位置や案内文だけが生き延びる。→ 無条件に捨てる。
- **候補送りの前提が甘かった。** 候補が無くても通っていた。
  → `candidatesBefore > 0` を必須にした。

## UI-P1-007 は受け入れ済み(2026-09-14)

Codex は R10 を **PASS WITH FIXES** にした。R7〜R10 で挙がった阻害要因は全部片づいた。
追加の R11 は要らない、と Codex 自身が書いている。

| 版 | 判定 | 何が問題だったか |
| --- | --- | --- |
| R7 | FAIL | 道具替えで前の吸着が残る / SetScene で持ち越しが残る |
| R8 | FAIL | 画面側の一時表示が残る / 試験が Hover を呼び直していて残留を見ていない |
| R9 | FAIL | 私が入れた寿命の不具合(破棄済み画面のコールバック) |
| R10 | PASS WITH FIXES | 非阻害2点(知らせ中の解除、綱の型)。どちらも入れた |

非阻害の指摘2点は `ac82541` で入れた。
残っているのは §6.1「道具に応じた吸着の強さ」で、
候補の種類を道具ごとに変える段でまとめて片づける。

## PENDING_CODEX_REVIEWS(古い順。消さない)

- REQUEST_ID: P1-EXTRUDE-R4 / TASK: Phase 1 押し出し / PHASE: 1
  BASE: bd375c9 / HEAD: f479814
  REVIEW_STATUS: PENDING_CODEX
  CLAUDE_SELF_REVIEW: PASS / BUILD: PASS(Windows + 雲)
  TEST: PASS(Windows CTest 141/141、自己試験 225/225、雲 core 134/134)
  前身: P1-EXTRUDE-R3(FAIL)。B1〜B3 を直し、MISSING TESTS を足した。
  **HEAD は Claude が固定した。最新 commit から選ばせない。**
- REQUEST_ID: Q1-Q5-R3 / TASK: Q1〜Q5 / PHASE: Q1-Q5
  BASE: bd375c9 / HEAD: f479814
  REVIEW_STATUS: PENDING_CODEX
  CLAUDE_SELF_REVIEW: PASS / BUILD: PASS(Windows + 雲)
  TEST: PASS(同じ固定範囲、同じ数字)
  前身: Q1-Q5-R2(FAIL)。B1〜B3 を直した。
  **HEAD は Claude が固定した。最新 commit から選ばせない。**
- REQUEST_ID: P1-EXTRUDE-R3 / TASK: Phase 1 押し出し / PHASE: 1
  BASE: 253e446 / HEAD: bd375c9
  REVIEW_STATUS: PENDING_CODEX
  CLAUDE_SELF_REVIEW: PASS / BUILD: PASS(雲 core)/ TEST: PASS(雲 core 132/132、
  Qt 当て木の型検査 69 ファイル)
  前身: P1-EXTRUDE-R2(FAIL)。B1・B2 を直し、MISSING TESTS を4件とも足した。
  **HEAD は Claude が固定した。最新 commit から選ばせない。**
- REQUEST_ID: Q1-Q5-R2 / TASK: Q1〜Q5 / PHASE: Q1-Q5
  BASE: 253e446 / HEAD: bd375c9
  REVIEW_STATUS: PENDING_CODEX
  CLAUDE_SELF_REVIEW: PASS / BUILD: PASS(雲 core)/ TEST: PASS(雲 core 132/132)
  前身: Q1-Q5(FAIL)。B2・B3・B4 と正対の契約を直した。
  B1(Windows リンク)は固定 HEAD より後の `ffee7bc` で結線済み。PC の往復で確かめる。
  **HEAD は Claude が固定した。最新 commit から選ばせない。**
- REQUEST_ID: P1-EXTRUDE-R1 / TASK: 押し出しUI / STAGE: 途中
  BASE: 4fa218c / HEAD: 188ea47
  CLAUDE_SELF_REVIEW: 未(機能として未完成。完成まで送らない)
  BUILD: PASS / TEST: PASS

## この往復で見つかった、前からあった欠陥

- **足す・引く押し出しが一度も通っていなかった。** `BuildExtrude` に相手の形を
  渡していなかったので、立体に窓を開ける押し出しは毎回 KER-E004 で断られていた。
  開き直しで足し引きをやり直す道も無かった。両方入れ、使い切った元の立体を隠す
  ところまで直した(`e97655a`)。
- **`FromWire` が辺を繋がった順に返していなかった。** `TopExp_Explorer` は位相の
  並びで返す。戻した線を輪郭として使うと KER-C003 で断られる。
  `BRepTools_WireExplorer` に替えた(`ce369eb`)。
- どちらも **面の押し引きを通したから露見した。** 実際に通す道を作らないと、
  こういう穴は見つからない。

## 解決した(2026-09-14): 帯近似の分け方は文書に持てた

下の「HUMAN_DECISION_REQUIRED(2件目)」は、**帯近似(V1 方式)については解けた。**

見落としていたのは、帯近似の分け方が「面の番号の並び」ではなく
**「境目のパラメータ」** だということである。`manualBoundaries` は前から
保存の形にあり、面の番号ではないので `architecture-and-data.md §6` の
「面番号を保存してはならない」とぶつからない。

2枚を1つにするのは、その間の境目を1本抜くこと。
1枚を2つに分けるのは、その真ん中に1本足すこと。
`automaticBoundaries` を偽にして書けば、以後は自動で切り直さない。
取り消し・やり直し・保存・再読込のすべてに乗る。

帯の数が変わるので、帯ごとに持っていた値(曲げ具合・半径・展開の基準)は
このとき捨てる。古い並びを新しい帯へ当てると、別の部材の値が当たってしまう。

残るのは **面を分類する方式(V2 方式)** の分け方である。そちらは面の集合なので、
下の話がそのまま残る。

## HUMAN_DECISION_REQUIRED(2件目、面を分類する方式だけ): 分け方を文書に持てない

`fabrication/PanelEdit` は「この2つを1つにしたらこうなる」「ここで分けたらこうなる」を
**判断できる**(隣り合わせの確認、面の移し替え、境目と切れ目の区別、前後の枚数とずれ)。
画面にも「部材を1つにする」「部材を分ける」を出し、前と後を見せる。

**しかし、決めた分け方を文書に残す道が無い。**
いまの製作モデルの作り方(`CreateFabricationModelDefinition`)は
「面と設定」しか持っていない。人が手で決めた分け方(どの面をどの部材へ入れたか)を
入れる場所が無いので、開き直すと自動の分け方へ戻ってしまう。

入れるとしたら `manualPartition`(部材ごとの面番号の並び)のような鍵になる。
これは **保存の形の変更(GUARDED)** で、しかも面番号を保存することになるため、
`architecture-and-data.md §6` の「面番号を保存してはならない」とぶつかる。
意味的キーで持つなら、面の意味的キーを近似の側でも作れるようにする必要がある。

**この設計をどうするか決めてほしい。** それまでは「言うだけ」で止める。
「決めたのに開き直すと戻る」を作るほうが悪い。

## CODEX_REVIEW_REQUIRED: ずれの許容が、近似で作る面を事実上禁じている

`OcctGuideSurface` の「作ったものを測り、通っていなければ捨てる」検査は、
許容を `max(modelLinearMm * 10, 1e-4)` = **0.0001mm** にしている。
考え方は正しい。V1 は測らずに出していた。しかし値が数値誤差の桁である。

案内付きロフト(`MakePipeShell`)も曲線網(`MakeFilling`)も、
**作りからして近似** である。断面をそのまま通ることは保証されない。
HO の前頭部で実際に測ったところ、

  - 案内付きロフト(断面を回さない): 0.19mm 外れる → 断られる
  - 曲線網: そもそも面が張れない
  - 断面を通すロフト(`ThruSections`): 通る

つまりこの許容のもとでは、**断面を通すロフトと平面しか使えない。**
案内付きロフトと曲線網は、命令の一覧にはあるが、実際にはどんな形でも断られる。
「あるのに使えない」は、いちばん困る形である。

考えられる直し方は3つ。どれを採るかは Codex に決めてほしい。

1. 許容を作図の尺度にする(`interactiveJoinMm`、既定 0.01mm、0.001〜0.1 で可変)。
   模型の実寸で「同じ点」とみなせる大きさ。ただし 0.19mm はこれでも通らない。
2. 許容を「人が指定した許容」にする。近似の許すずれと同じ考え方で、
   作るときに人が決める。既定は板厚の何割か。
3. 断らずに **ずれを言って通す**。`maximumDeviationMm` は既に返している。
   ただし「できたことにしない」という約束からは遠ざかる。

いまは見本を断面を通すロフトへ寄せて先へ進めた。
案内付きロフトと曲線網は、この判断が出るまで実質使えないままである。

## CODEX_REVIEW_REQUIRED: 形を作る層へ手を入れた(2026-09-14)

`BuildGuidedLoft`(`src/next_occt/kachakacha/kernel/OcctGuideSurface.cpp`)で、
断面を渡すときの `WithCorrection` を真から **偽** にした。
形を作る層は GUARDED なので、Codex の確認が要る。

理由。「合わせ直す」を頼むと、OCCT は断面を背骨と直角になるように
回してから使う。前面中央が前へ膨らむ形の断面は平らではないので、
回されると大きくずれる。HO の前頭部で 8.4mm ずれ、
`GEO-G008`「出来た面が、指定した線を通っていません」で断られた。

断面は作図の時点で正しい場所に置いてある。動かさせる理由が無い。
偽にすると、置いたとおりに使う。

影響。案内付きロフトを使うすべての面。断面が背骨と直角でない使い方
(いままで「合わせ直し」に助けられていた形)があれば、そちらは
作れなくなるか、ずれの検査で断られる可能性がある。
ずれの検査は前からあるので、**黙って違う形になることはない。**

## 要確認(Codex 領分に触った)

- **`.kcd2` に鍵を1つ足した**(2026-09-14)。`create_fabrication_model` の
  `splitSolidFaces`(真偽、既定は偽)。真なら立体の面を1枚ずつ部材にする。
  古い文書は偽で読むので、開いても部材の数は変わらない。
  2026-09-13 の切れ目の上限2つと同じやり方である。
  見本(`samples/*.kcd2`)を作り直した。
- **HUMAN_DECISION_REQUIRED: 面をまとめて1枚の部材にする道が無い。**
  `PanelStrategy` は「この3面を1枚にまとめる」まで決められるが、
  いまの部材は **1枚の格子** で出来ていて、別々の面の格子を1つに繋ぐ道が無い。
  ここは部材の持ち方そのものの作り直しになる(GUARDED)。
  それまでは、`PanelAdviceTextJa` が「3枚に分ければ作れます」と枚数を言い、
  `splitSolidFaces` が面ごとに切り出す、という半分の形になる。
  **作り直してよいか、いまの半分の形で当面よいかを決めてほしい。**

- **面の押し引きが文書にワイヤーを増やす**(EX-02、2026-09-14)。
  面を押すと、押した面の縁が「面の縁」という名前のワイヤーとして残る
  (穴があればその数だけ)。押し出しの記録(`ExtrudeDefinition`)は
  「どのワイヤーを押したか」で出来ており、面番号は作り直すたびに変わるので
  記録できない(architecture-and-data.md §6)。縁をワイヤーにすれば、
  保存の形も Command の作られ方も変えずに、開き直しても作り直せる。
  代わりの案は `ExtrudeDefinition` へ面の意味的キーを足すことで、そちらは GUARDED。
  **「面を押したのに線が増える」ことの是非を見てほしい。**

- `docs/manual.html` に3行足した(`help.copy_diagnostics` `wire.center_points`
  `wire.key_points`)。門が全コマンドの説明を求めるため。文言の確認だけ。
- `samples/*.kcd2` を書き直した。保存の鍵が増えたため(切れ目の上限、選択半径)。

## STATUS: COMPLETE_PENDING_CODEX(2026-09-14)

作業キューは空になった。

| 見るところ | 状態 |
| --- | --- |
| 実装キュー | 空(Q1〜Q5、§27〜33 すべて実装して PC で通った) |
| 修正キュー | 空 |
| 未処理の Codex Blocking | 無し(R3 と Q1-Q5-R2 の6件をすべて直し、再提出した) |
| 回帰の直し | 空(PC の CTest 141/141、自己試験 225/225) |
| HO 総合試験 | 全通(面が作れ、近似・曲げ・出力まで通る) |

`COMPLETE` にしないのは、**まだレビューを受けていない提出が2つある** ためである。

- `P1-EXTRUDE-R4`(bd375c9..f479814)
- `Q1-Q5-R3`(同じ範囲)

加えて、Codex の判断を待っているものが2件ある(下の CODEX_REVIEW_REQUIRED)。
人の判断を待っているものが2件ある(下の HUMAN_DECISION_REQUIRED)。
どれも「動かないから止まっている」のではなく、
**勝手に決めてはいけないから止めている** ものである。

## 作業キュー(オーナー指示 2026-09-14 第2便。上から順に)

**総合完成条件**は「HO ゲージの流線形前面を、この CAD で実際に設計し、板材近似し、
展開・曲げ状態を確認し、製作用の Wire / Surface を取り出せること」。
単体試験が通っただけでは COMPLETE にしない。

| # | 名前 | 中身 | 状態 |
| --- | --- | --- | --- |
| Q1 | 選択に正対の完成 | WorkPlane / 平面Face / 曲面Face / 平面Surface / 曲面Surface。向き+注視点+中央+Fit を1操作で。VF-01〜08 | **PC で確認済み。** 複数選んだときの契約も決めた(向きは最初の1つ、収まりは全部) |
| Q2 | 左ツリーの Group / Folder | 整理用コンテナ。作成・改名・D&D・入れ子・表示切替・解除・Undo/Redo・Save/Load。GR-01〜10 | **PC で確認済み。** 作成と複数引きずりは1回の取り消しで戻る |
| Q3 | HO 流線形前面 総合テストモデル | 1/80・16.5mm。WorkPlane→Wire→Surface。完成 BRep のハードコード禁止 | **PC で確認済み。** 面が本当に作れるところまで通した |
| Q4 | 総合試験 | 押し出し・板材近似・展開・任意曲げ出力まで通す。TM-01〜14 / UI-TM-01〜19 | **PC で確認済み。** 何も無い文書から本番の指示だけで通る |
| Q5 | ApproxPart の細部 | 部材ごとの選択・実寸半径入力・AUTO/LOCK・部材の分割/統合・展開基準辺 | **PC で確認済み。** 半径は文書が持ち実際に形を変える。分割/統合は境目を文書へ書いて枚数が変わる。展開の基準にする辺も入れた |

進行中の押し出し・近似の作業は中断しない。安全な区切りで取り込む。

### 停止の決まり(オーナー指示 2026-09-14)

キューに未完了が残っている限り、進捗報告や Phase 完了を理由に止まらない。
Codex レビュー待ちでも、自己レビュー・build・test・commit・PENDING_CODEX を
記録したうえで次の未完了へ進む。空になったら `STATUS: COMPLETE`、
未レビューが残るなら `STATUS: COMPLETE_PENDING_CODEX`。
止まってよいのは既定の BLOCKED_HUMAN(安全に解けない Git 競合、破壊的変更、
仕様の根本矛盾、設計論点のループ、同一 Blocking の反復不能、
利用者判断が要る重大分岐)だけ。

### Q1 でやったこと(2026-09-14)

**原因は2つあった。**

1. **命令が押せなかった。** `view.align_selection` の条件が
   `OnePlanarFaceOrWorkPlane`(= 作業平面 + 平らな面がちょうど1つ)で、
   `planarFaces` はどこでも数えていなかった。立体の面を選んでも 0 のままなので、
   命令は「構えたまま」になり、何も起きなかった。
   → `AnythingToFace`(作業平面・立体の面・面・立体・線のどれか1つ以上)に替えた。
   使い手の居なくなった `OnePlanarFaceOrWorkPlane` は畳んだ。
2. **相手を集められなかった。** `CollectFacingTarget` は作業平面・線・点しか見ていない。
   立体も形状ガイドも点を1つも出さないので、断っていた。
   → 立体の面(`kernel::FacePoseNear` で押した場所の近くの法線)、
   形状ガイドの面(`app::SurfaceFacingPose` で真ん中の向き)、
   立体そのもの(外接箱で中央と大きさだけ合わせ、向きは変えない)を足した。

あわせて **選択を残す**(正対の後に選び直しでは作図へ進めない)、
**「反対側から正対」**(`view.align_selection_back`)を入れた。

### Q1 でいま足りていないもの(調べた結果)

`V2MainWindow::CollectFacingTarget` は **作業平面・線・点しか見ていない**。
立体の面(subshape)も、形状ガイドの面も、点を1つも出さないので、
`target.points.empty()` で「作業平面・線・点のどれかを選んでください」と断っていた。
向き・中央・大きさを揃える仕掛け(`view::PlanFacingSelection`)は既にあるので、
足りないのは **相手の集め方** である。

## Phase の進み

- Phase 0 Baseline: 完了。
- UI-P1-007: Stage 1 取り込み・修正まで完了。Codex レビュー待ちだが止まらない。
- Phase 1 押し出し: ほぼ完了。
  - 選択の読み取り(A〜E、Target/Profile 判定): 完了 `2349ebd`
  - 矢印ハンドル・距離同期・下見・Enter/Esc: 完了 `e774350`
  - 右ペイン(入力・距離・方向・範囲・操作・確定): 完了 `5354276` `7f13b72`
  - 入力の選び直し(EX-07): 完了。`app::SelectionWithout` + 棚の2つのボタン。
  - 面の押し引き(EX-02): 完了(PC 確認済み)。
    `kernel/OcctFaceQuery` が面の縁を返し、`app/FacePushPull` が
    符号つきの距離を押し出しの言葉へ言い換え、`V2FacePushPull` が
    縁を文書のワイヤーにしてから、いままでの押し出しへ渡す。
    雲では OCCT を組み立てられないので、PC の1往復で初めて確かめられる。
- Phase 2〜5(板材近似): 未着手。

## 押し出しの受入試験(指示 §33)

| ID | 内容 | 状態 |
| --- | --- | --- |
| EX-01 | 閉じた輪郭 → 押し出し → ハンドル → Enter → 新規立体 | 済 |
| EX-02 | 立体の面 → 押し引き | 済(PC 自己試験「立体の面をつまんで押せる」) |
| EX-03 | 立体だけ → 「面または輪郭を選んでください」 | 済 |
| EX-04 | 立体+輪郭を順不同で選んでも役割が決まる | 済 |
| EX-05 | 距離の欄と矢印が同期 | 済 |
| EX-06 | 方向反転で矢印も反転 | 済 |
| EX-07 | 入力の差し替え | 済(棚に「対象を選び直す」「輪郭を選び直す」。片方だけ外れる) |
| EX-08 | 道具を替えると下見・棚・一時状態が残らない | 済 |
