# HTMLマニュアルの更新と検証

入口は `docs/manual.html`。HTMLと `docs/manual/` を一緒に置くと、ネット接続なしで閲覧・検索できる。
本文の正本は `chapters.html` と `illustrated-workflows.html`、追加のコマンド説明は `command-notes.json`。
コマンド名・キー・条件・操作案内は現在の `CommandCatalog.cpp` から生成する。
登録数・重複・欠落・ローカル参照・HTMLタグの整合を生成時に検査する。

```powershell
python docs/manual/build_manual.py
python docs/manual/build_manual.py --check
ctest --test-dir build-msvc2022-x64 -C Release -R manual --output-on-failure
```

画像は `images/v2-<状態名>.png`。V2配布実行ファイルの
`--manual-state <状態名> --size 1440x900 --snapshot <保存先>` で撮影する。
Windowsでは `QT_QPA_PLATFORM=windows` を使う。offscreenプラットフォームでは
日本語フォントが四角になる場合があるため、撮影後に日本語表示も確認する。
状態名は README の図一覧を参照。説明書の `instructions-3d` だけは
`V2SelfTestInstructions.cpp` の HP-INSTRUCTIONS がテンポラリーに保存した画面から採用する。
2026-10-19以降、説明図は実アプリ画面のみを使う。模式図・疑似画面を追加しない。
Qt操作試験かOS操作の経路、入力待ち/下見/確定後を記録し、撮影用stateだけの画像はその範囲を明記する。

## 2026-10-05 の検証範囲

- ソース基準 d767ab58d。登録163命令を各1回掲載。5モード。
- Windowsの既存V2実行ファイルで35状態を撮影、すべて終了0。
- 説明書モードの既検証実画面1枚を加え、当時は実画面36枚・模式図2枚。模式図は2026-10-19に実操作画面へ置換。
- ローカルのリンクと画像、HTMLのタグ整合、アンカー重複・リンク先、1500行以内を検査。
- 既存 `v2_manual_tests` の14項目に合格。
- 検索の複数語・0件・解除・内部リンク移動・印刷呼出しはNodeの模擬DOMで検査。
- 画像一覧と日本語を目視確認。内蔵ブラウザーはfile URLを拒否したため、ブラウザーの
  実表示・レスポンシブ表示・印刷結果は未確認。PC_VERIFIEDや全機能の再検証とは扱わない。
- 変更はマニュアルのみ。CAD本体の再ビルド・全機能試験は今回実施しない。


2026-10-13: `images/v2-thicken-corrected.png` は更新配布版のHP-TH-CORRECT-OWNERから撮影。
自己試験用の環境変数KACHACAD_THICKEN_TEST_FILEで入力文書を指定し、KACHACAD_THICKEN_TEST_OUTPUTへ画像と別名の結果を保存する。原本は上書きしない。
