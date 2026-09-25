# GPT版面生成の引継ぎ

2026-09-24。ブランチ `codex/v2-wp01-build-scaffold`、基点 `6ed2b931`。
**実装・Windows自動検証完了。** ソース検証コミット `dd6aa17245120b4f4de7ea793ceec1b0fb6af20b`。

実装: GptSurface入力検査、OcctGptSurfaceの外周/内部拘束・断面生成、
V2GptSurfaceToolの一覧/プレビュー/確定、保存フラグと再生成、命令・リボン・メニュー接続。
既存の面生成とは独立。外部AI通信はない。操作は [GPT_SURFACE.md](GPT_SURFACE.md)。

検証: Windowsビルド、ctest182/182、旧アプリ自己試験終了コード0、V2自己試験354/354。
GPT幾何8/8、HP-GPT-01〜04、構造検査16/16合格。
詳細・検証条件は [GPT_SURFACE_VALIDATION.md](GPT_SURFACE_VALIDATION.md)。
機能の残作業はなし。穴付き外周・G1/G2・辺数の異なる断面は仕様上の対象外。

## このPCの起動用バッチ

元の `_GO.cmd` / `_QUICK.cmd` は古いclaude-baseへのreset・bundle merge・強制終了を含んでいた。
元ファイルは `.before-gpt.cmd` として保管。作業後の既定動作と `--current` は、
ローカル `_claudeout/gpt-current.py` で指定ブランチを確認し、現在の内容をビルド・検証する。
_GO側は全成功時だけ同じブランチへpushする。reset・bundle merge・強制終了はしない。
`_GO.cmd --publish-verified` は、最後の検証コミットからの差分がdocsだけで追跡ファイルがcleanのときだけ送る。

開始時の他者変更は `wip-desktop-dh7772g-20260924-gpt-surface` の `b1924e32` に退避・push済み。
`.worktrees/` と `Claude outputs/` は他作業なので触らない。

## 2026-09-25 自動外周・方向表示の追加改修

ユーザーの分岐エラー報告に対応し、自動判定を既定化。GptSurfaceAutoとV2GptSurfaceAssistを追加。
ソースは1d6de942に途中保存し、複合ワイヤーのクリック対応と回帰試験を続けて補強した。
改修後のWindows検証完了: `3102580a`。ctest182/182、V2自己試験355/355、旧自己試験終了0。
実行中の旧アプリは終了させず実行ファイルを同じフォルダーの `.before-gpt-auto.exe` へ退避し、通常名に新版をビルドした。
編集中のユーザーは保存後に再起動すると新版になる。

## 2026-09-25 面の滑らかさ・視認性の改修完了

ソース `1210b82f5` を指定ブランチへ_GOで送信済み。Windows全ビルド、ctest182/182、
旧自己試験終了0、V2自己試験357/357合格。GPT幾何11/11、表示24/24、構造16/16。
SurfaceRasterの連続陰影・画素深度、OCCT曲面法線と細かな表示分割、不透明な青い面表示、
GPT曲面の近似条件改善を実装。HP-GPT-06にオーナー前頭部相当の8本の再現を追加。
プレビュー・確定後の撮影も確認済み。通常のbuild-msvc2022-x64/Release/kachakacha_cad_next.exeが新版。
保存後にアプリを再起動する。既存ファイルの面は再生成時に新しい計算条件が使われる。
詳細はGPT_SURFACE_VALIDATION.md。Linux/cloudゲートは未実行。

検証後、他AIのU/V格子追加を含む6afe53b14が同じブランチに合流していた。
その変更を維持。追加撮影とGPT限定6/6は後続の既存exeで実行。
全ゲートの証明は1210b82f5に限定し、後続まで検証済みとは記載しない。
