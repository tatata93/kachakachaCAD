# 2026-11-28公開更新11の自宅PCへの導入

状態: 公式ZIPの導入・通常Windows起動・重点UI試験・ER2読込と原本保持・115系での設定比較が完了。
[公開版](https://github.com/tatata93/kachakachaCAD/releases/tag/v0.1.0-20261128-update11)のタグ先
`c490018c6f152e59fa32720805aa8423fa462af1`、latest、公開状態とassetをGitHubで照合した。

- ZIPは74,758,411バイト。SHA256 `1d2ca7a75d41f4c50506daa65de30d13805e68fcbc3ddc7c94d80caad3a5008a` とCRCが一致。
- EXEは指定SHA256 `d3ec0133e69ff9434add8cefb4ff639dc083a0cb546713725f3fb14ba863e70e` と一致。同梱1,004ファイルはZIPと展開後の全バイトを照合した。
- 起動先は通常clone内の `out/local-candidates/windows-20261128-update11-c490018c/package/kachakacha_cad_next.exe`。従来版と別に展開し、再ビルドしていない。
- 開発用PATH・Qt変数を除いた通常Windows起動2条件が成功。継承設定がwindowsとoffscreenの両方で、通常GUIのWindowsプラットフォーム、イベントループ、表示・描画を確認した。
- 同梱版で次の完全一致ケース3件がすべて成功した。総数3・失敗0、正確な試験名と各合否を実レポートで照合した。
  - `HP-OPEN-01 Escape cancels saved vessel history with an armed editor and preserves Undo`
  - `HP-OPEN-04 prepared open preserves metadata and unknown archive entries`
  - `HP-OPEN-10 cancellation during foreground restoration releases the prepared shapes`
- 既存ER2の作業コピー438エンティティをWindowsで読み、描画画像を確認した。原本とコピーのSHA256は前後不変で、`ec2aa399712c88a108bada7ab4cc80c8acb997b0a3e1e3be47d5e02c355c703b`。

更新11は文書読込中の進捗ラベル更新をまとめる。最初の進捗は直ちに表示し、その後は100msのタイマーで最新の進捗を表示する。
導入確認後、自宅チャットへ起動リンク・絶対パス・公開ページと変更点を先に通知してから追加計測を始めた。
学校側のWindows全体・配布試験と従来自己試験を、自宅の導入確認と重点3件の範囲とは区別する。
従来版、ユーザー作業ブランチ・モデルと他担当の変更は保全し、製品コードを編集していない。

## 計時ログなしの通常アプリ読込比較

自宅の4論理CPUで、`samples/series115-heavy/series115-1000-1-80.kcd2` の作業コピーを使用した。
1,085形状。原本とコピーのSHA256は `8e08619769ef38028af3c2a9f4a6699e607c1e7e8066f3301100aa149b72b4d5`。
同じ更新11の同梱EXEで `HP-OPEN-OWNER actual KCD load timing` を完全一致で選び、
`KACHACAD_OPEN_PROGRESS_IMMEDIATE=1` と同変数を外した既定設定を交互に各4回比較した。

各ケースの内部 `CacheScope` が空の一時文書キャッシュを作り、通常の `OpenDocumentFile` で初回と再読込を続けて行う。
Windowsプラットフォーム、OS標準PATHを使い、開発用Qt・KACHACAD変数を除去した。
`KACHACAD_OPEN_TRACE_DIR` を設定せず、区間別計時ログは無効。`KACHACAD_OPEN_SERIAL` も使わない。
OSファイルキャッシュは消去せず、並行したシステム診断も行っていない。
所要時間はOwnerケースが `QElapsedTimer` で測るアプリの読込呼出し全体であり、EXE起動時間や後続の描画は含めない。
過去の計時ログを有効にした測定や、学校PCの時間との版間比較には使わない。

全8ケース・16読込が成功し、各回で1,085形状、初回・再読込の文書定義一致、原本・コピー・EXEのSHA256不変を確認した。
以下はミリ秒。全測定を残し、外れ値の除外はしていない。

| 組 | 即時更新 初回 | 既定 初回 | 即時更新 再読込 | 既定 再読込 |
| --- | ---: | ---: | ---: | ---: |
| 1 | 13,619 | 13,447 | 5,324 | 5,397 |
| 2 | 13,510 | 13,392 | 5,318 | 5,292 |
| 3 | 13,468 | 13,388 | 5,275 | 5,429 |
| 4 | 13,487 | 13,615 | 5,374 | 5,465 |

| 区間 | 即時更新の中央値（最小〜最大） | 既定の中央値（最小〜最大） |
| --- | --- | --- |
| 初回 | 13,498.5（13,468〜13,619） | 13,419.5（13,388〜13,615） |
| 再読込 | 5,321（5,275〜5,374） | 5,413（5,292〜5,465） |

初回の中央値は79ms短いが、再読込は92ms長い。両条件の範囲が重なり、自宅の今回4組では読込全体の明確な高速化は確認できなかった。
初回・再読込とも数秒以上の待ち時間は残っている。

## 計時ログありの別試行で進捗ラベル更新数を確認

時間比較の後、同じモデル・EXEで即時更新と既定を各1回だけ追加実行し、`KACHACAD_OPEN_TRACE_DIR` を指定した。
この追加2ケース・4読込も成功し、原本・コピー・EXEは不変。初回cache missと再読込cache hitをトレースで照合した。
`document-progress` スコープはアプリ本体の各読込に1件ずつ記録され、次の更新数だった。

| 読込 | 即時更新 | 既定 |
| --- | ---: | ---: |
| 初回 | `immediate-1087` | `coalesced-12` |
| 再読込 | `immediate-2` | `coalesced-2` |

初回の進捗ラベル更新は1,087回から12回へ減った。これは進捗ラベル更新数であり、画面全体の再描画回数やFPSの測定ではない。
ログありの追加試行の時間は、ログなし4組の集計に混ぜていない。学校の7〜9回という結果を自宅の測定値として扱わない。

証拠は候補用作業ツリーの `out/20261128-update11-home/`。
導入の `result.json`、重点 `focused-ui.json`、通常起動レポート・画像、ER2読込ログと画像を保持した。
`progress-comparison/` には全Ownerレポート、ログなし8ケースの `untraced-measurements.json`、
ログあり2ケースの `traced-measurements.json` と各プロセスのトレース、`summary.json` を保存した。
起動先の一つ上にも `installation.json` を保存した。PC固有の絶対パスとバイナリはGitへ追加しない。
