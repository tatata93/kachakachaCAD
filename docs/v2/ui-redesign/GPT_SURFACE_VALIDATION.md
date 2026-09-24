# GPT版面生成のWindows検証

2026-09-24 / ソース検証コミット `a11ec4f6aa16066f3219d6743c939a54cdef0d41`。

- `_QUICK.cmd --current` と `_GO.cmd --current` の現在ブランチ専用経路を使用。
- `scripts/check.ps1 -Preset windows-msvc`: Windows/MSVC本番構成ビルド成功、ctest **182/182** 合格。
- `kachakacha_cad.exe --self-test`: 終了コード0。
- `kachakacha_cad_next.exe --self-test`: **353/353** 合格。絞り込みなし。
- `gpt_surface_tests`: **8/8** 合格。外周・内部拘束・複合断面・円形断面・保存・非表示参照・重複・方向等。
- HP-GPT-01〜04: 外周のプレビュー/確定/Undo/Redo/保存再読込、断面生成、拒否/取消、
  実際の選択クリックから複合断面の追加/並べ替え/確定を確認。
- `architecture_tests`: **16/16** 合格（ファイル1500行、cad_next関数100行を含む）。
- 証拠ログ: ローカル `_claudeout/gpt-current-check.txt`、`gpt-kernel-tests.txt`、`gpt-focused-ui.txt`。

通常の画面自己試験は既存PC手順と同じ `QT_QPA_PLATFORM=offscreen`。
撮影用の追加実験で `QT_QPA_FONTDIR` を指定すると、既存の数値入力の画面内配置・辺選択等の
座標依存試験が失敗したため、通常ゲートとは区別する（`gpt-final-ui.txt`）。
そのときのHP-GPT-04の試験入口の誤りは修正し、通常設定で4件とも合格を確認した。
Windowsの実Qt/OCCTで自動検証した結果であり、オーナーの目視受入の代行とはしない。
Linux/cloudのビルド・qtstub型検査はこのWindowsセッションでは実施していない。
