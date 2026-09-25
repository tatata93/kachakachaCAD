# GPT版面生成のWindows検証

2026-09-24 / ソース検証コミット `dd6aa17245120b4f4de7ea793ceec1b0fb6af20b`。

- `_QUICK.cmd --current` と `_GO.cmd --current` の現在ブランチ専用経路を使用。
- `scripts/check.ps1 -Preset windows-msvc`: Windows/MSVC本番構成ビルド成功、ctest **182/182** 合格。
- `kachakacha_cad.exe --self-test`: 終了コード0。
- `kachakacha_cad_next.exe --self-test`: **354/354** 合格。絞り込みなし。
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

他作業の追加修正（35954efeまで）を保持して指定ブランチへ合流後、上記の全ゲートを再実行して合格。
_GO.cmd経由で同ブランチへのpushも成功。

## 2026-09-25 自動外周・目印の追加検証

検証ソース `3102580a75838234346eb35cef57fdabb3f97362`。`_GO.cmd --current` によりWindows通常構成でビルド、
ctest182/182、旧アプリ自己試験終了0、V2自己試験355/355を確認し、指定ブランチへ送信。
GPT幾何10/10、作図32/32、構造検査16/16、HP-GPT-01〜05合格。
全体自己試験は通常のoffscreen設定。追加でフォント指定をしたGPT限定試験も5/5合格、
番号/色/矢印/選択行の強調を撮影確認した。

- 外周と非平面の内部線が分岐で接続していても、役割を自動推定して面を生成。
- 候補切替で全線を保持し、面の外へ出る入力は偏差検査で拒否。
- 登録済み線のクリックと一覧の対応、手動反転の矢印、自動判定解除、確定後の目印消去。
- 作図直後のSegment UUIDが保存定義と違う不具合も修正。Undo/Redo・保存再読込の面再生成を確認。
- ログ: `_claudeout/gpt-current-check.txt`、`gpt-auto-fix-check.txt`。
- Linux/cloudゲートはこのWindowsセッションでは未実行。

検証完了後に他作業の5aa749457までの追加変更が同ブランチへ入ったため、その変更を保持して記録を追記した。
上記の検証対象は明記した3102580aであり、後続の他作業の変更まで検証済みとするものではない。
