# GPT版面生成の引継ぎ

2026-09-24。作業ブランチは `codex/v2-wp01-build-scaffold`。基点 `6ed2b931`。
**実装途中・PC未検証。既存面生成の修正ではなく独立した生成経路。**

実装: GptSurface 入力検査、OcctGptSurface 外周/内部拘束・断面生成、
V2GptSurfaceTool の入力一覧/プレビュー/確定、保存フラグと再生成、命令・リボン・メニュー接続。
仕様は GPT_SURFACE.md。外部AI通信はない。

未完了:
- Windows全体コンパイルとエラー修正。初回の MSBuild Path/PATH 重複は環境を大文字キーに正規化して解消。
- 初版 gpt_surface_tests 5ケースはWindows/OCCTで合格。追加の保存・非表示参照テストは次のビルドで確認。
- UI self-test HP-GPT-01〜03は追加済み、まだ実行前。
- command-catalog/manual/矩陣/台帳へ操作を反映済み。実測の最終結果は未記録。
- cad_next関数100行/ファイル1500行を含む architecture_tests は16/16合格。
- Windowsテスト・self-testを通してから _GO.cmd 経由で同じブランチへpush。

注意: このPCの既存 _GO.cmd / _QUICK.cmd は古い claude-base へresetする内容だった。
既定引数では実行しない。ローカル _GO.cmd の --preserve-wip で退避branchをpush済み。
_QUICK.cmd --current / _GO.cmd --current を追加済み。ローカル _claudeout/gpt-current.py を呼び、
指定ブランチを確認して scripts/check.ps1 と旧/V2両self-testを実行する。
_GO側は全成功時だけ現在ブランチへpushする。reset・bundle merge・プロセス強制停止はしない。
開始時の他者変更は wip-desktop-dh7772g-20260924-gpt-surface の b1924e32 に退避・push済み。
`.worktrees/` と後から現れた `Claude outputs/` は他作業のため追加・変更しない。
