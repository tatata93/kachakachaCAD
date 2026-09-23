# GPT版面生成の引継ぎ

2026-09-24。作業ブランチは `codex/v2-wp01-build-scaffold`。基点 `6ed2b931`。
**実装途中・PC未検証。既存面生成の修正ではなく独立した生成経路。**

実装: GptSurface 入力検査、OcctGptSurface 外周/内部拘束・断面生成、
V2GptSurfaceTool の入力一覧/プレビュー/確定、保存フラグと再生成、命令・リボン・メニュー接続。
仕様は GPT_SURFACE.md。外部AI通信はない。

未完了:
- Windowsコンパイルとエラー修正。初回は MSBuild の Path/PATH 重複でコンパイラ起動前に失敗。
- 独立生成・入力拒否・保存再読込のテスト、UI self-test。
- command-catalog/manual/矩陣/台帳へ操作と実測結果を反映。
- cad_next関数100行/ファイル1500行ガード確認。
- Windowsテスト・self-testを通してから _GO.cmd 経由で同じブランチへpush。

注意: このPCの既存 _GO.cmd / _QUICK.cmd は古い claude-base へresetする内容だった。
既定引数では実行しない。ローカル _GO.cmd の --preserve-wip で退避branchをpush済み。
--current はまだ既存 _FIX_AND_BUILD.cmd を呼び、その中にもbundle merge/プロセス強制停止が
あるので、そのまま実行せず現在ブランチ専用の安全な経路へ修正する。
開始時の他者変更は wip-desktop-dh7772g-20260924-gpt-surface の b1924e32 に退避・push済み。
`.worktrees/` と後から現れた `Claude outputs/` は他作業のため追加・変更しない。
