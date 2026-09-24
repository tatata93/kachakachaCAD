# GPT版面生成の引継ぎ

2026-09-24。作業ブランチは `codex/v2-wp01-build-scaffold`。基点 `6ed2b931`。
**実装済み・最終確認中。既存面生成の修正ではなく独立した生成経路。**

実装: GptSurface 入力検査、OcctGptSurface 外周/内部拘束・断面生成、
V2GptSurfaceTool の入力一覧/プレビュー/確定、保存フラグと再生成、命令・リボン・メニュー接続。
仕様は GPT_SURFACE.md。外部AI通信はない。

検証・残作業:
- _QUICK.cmd --current（scripts/check.ps1）: Windowsビルド、ctest 182/182合格。
- 旧kachakacha_cad.exe --self-testは終了コード0。V2自己試験は352/352（HP-GPT-01〜03含む）合格。
- gpt_surface_tests: 8ケース合格（保存/非表示参照/重複/向き/平面/内部拘束/複合断面/円形断面等）。
- architecture_tests: 16/16合格。行数ガード含む。
- HP-GPT-04（実クリックで複合断面を作成・並べ替え）を追加。通常設定の最終自己試験は次に実施。
- 日本語の撮影用にQT_QPA_FONTDIRを指定した追加実行では既存の座標依存UI試験が失敗。
  通常の検証設定とは分ける。撮影ログは _claudeout/gpt-final-ui.txt。
- 操作説明と命令の全登録は更新済み。最終実測結果を記録し _GO.cmd --current でpushする。

注意: このPCの既存 _GO.cmd / _QUICK.cmd は古い claude-base へresetする内容だった。
既定引数では実行しない。ローカル _GO.cmd の --preserve-wip で退避branchをpush済み。
_QUICK.cmd --current / _GO.cmd --current を追加済み。ローカル _claudeout/gpt-current.py を呼び、
指定ブランチを確認して scripts/check.ps1 と旧/V2両self-testを実行する。
_GO側は全成功時だけ現在ブランチへpushする。reset・bundle merge・プロセス強制停止はしない。
開始時の他者変更は wip-desktop-dh7772g-20260924-gpt-surface の b1924e32 に退避・push済み。
`.worktrees/` と後から現れた `Claude outputs/` は他作業のため追加・変更しない。
