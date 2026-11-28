# 2026-11-28公開更新8の自宅PCへの導入

状態: 導入・通常起動・重点UI試験・実モデル読込・原本保持・画面側復元方式の比較が完了。
[公開版](https://github.com/tatata93/kachakachaCAD/releases/tag/v0.1.0-20261128-update8)のタグ先
`48db69456025d8fc48a7dfad5f392fc4d1e050fe`、latest、公開状態、assetをGitHubで照合した。

- ZIPは74,751,532バイト。SHA256 `b2f53b7e58c744130c5bbe13d584666e822252fd1e1318f20bf9fc4411c56230` とCRCが一致。
- EXEは指定SHA256 `df80bf3b791544e31f7b7cd3f4499300795007b00cdfbd6b1aadb0fbeb0a7c78` と一致。同梱1,004ファイルはZIPと展開後の全バイトを照合した。
- 起動先は通常clone内の `out/local-candidates/windows-20261128-update8-48db6945/package/kachakacha_cad_next.exe`。旧版とは別に展開し、再ビルドしていない。
- 開発用PATH・Qt変数を除いた通常Windows起動2条件が成功。Windowsプラットフォーム、イベントループ、表示・描画を確認した。
- 同梱版で次の完全一致ケース2件が成功した。
  - `HP-OPEN-10 cancellation during foreground restoration releases the prepared shapes`
  - `HP-OPEN-09 prepared transfer identities cancellation and poisoned cache`
- 既存ER2の作業コピー438エンティティと115系の作業コピー2,455エンティティをWindowsで読み、描画画像を確認した。両原本とコピーのSHA256は前後不変。

導入確認ラッパーは指定順と試験台帳順の違いを誤って不一致とし、2件とも成功した後で停止した。
実行レポートの総数2・失敗0・正確な試験名と合否を確認し、ラッパーを順序に依存しない完全一致比較へ修正した。
既存の成功レポートを保持し、配布物・通常起動記録・原本を再照合して導入結果を確定した。製品コードと試験対象は変更していない。

導入結果と起動リンクを自宅チャットへ先に通知してから、以下の追加計測を開始した。
証拠は候補用作業ツリーの `out/20261128-update8-home/` と起動先の一つ上の `installation.json`。
学校側のCTest197件（skip 0）、開発・配布UI各1118件、従来自己試験等の全体ゲートと、自宅の確認範囲を区別する。
従来版とユーザーデータ、他担当の変更は保持し、PC固有の絶対パスとバイナリはGitへ追加しない。

## 通常アプリ経路の画面側復元の比較

自宅の4論理CPUで `samples/series115-heavy/series115-1000-1-80.kcd2` の作業コピーを使用した。
部品1,083と形状ガイド2の計1,085形状。原本とコピーのSHA256は
`8e08619769ef38028af3c2a9f4a6699e607c1e7e8066f3301100aa149b72b4d5`。

同じ更新8の同梱EXEで `HP-OPEN-OWNER actual KCD load timing` だけを完全一致で選択した。
`KACHACAD_OWNER_OPEN_FILE` を作業コピーにし、`KACHACAD_OPEN_FOREGROUND_SERIAL=1` と同変数を外した既定を交互に各3回実行した。
既存の保存・再構築・キャッシュを直列化する `KACHACAD_OPEN_SERIAL` は使用していない。
アプリ本体の `--self-test` から既存の `OpenDocumentFile` を通し、`--document-job` だけの計測とは区別した。
各試験は内部の `CacheScope` で空の一時キャッシュを作り、初回と再読込を続けて行った。
Windowsプラットフォームを指定し、開発用PATH・Qt変数は除外した。OSファイルキャッシュは消去せず、並行したシステム診断も行っていない。

アプリ本体のプロセスで `open/restore-native-shapes` の `stage_ms`（当該区間の時間）と `elapsed_ms`（読込開始からの累積）、
`open/complete` の `elapsed_ms`、`foreground-native-restore/workers-started` の有無・PID・UTC・計時を記録した。
読込時間は `OpenDocumentFile` 内の計時で、EXE起動時間や後続の描画は含めない。

全6回のOwnerケースが成功し、計12回の読込で1,085形状、初回・再読込の文書定義の一致、原本の保持を確認した。
各ケースの初回はcache miss、再読込はcache hitだった。原本・作業コピー・EXEのSHA256は各回で不変。
既定ではアプリ本体の各読込で `foreground-native-restore/workers-started` を1件、計6件記録した。画面側直列では0件であり、比較する経路が実際に切り替わったことを確認した。
同イベントは並列処理の起動確認に使い、復元所要時間には `open/restore-native-shapes` の `stage_ms` を用いた。

値はミリ秒の中央値（最小〜最大）。各条件3回をすべて集計し、外れ値も除外していない。

| 区間 | 画面側直列 | 既定 |
| --- | --- | --- |
| 初回 復元区間 `stage_ms` | 1,649.077（1,618.650〜1,673.271） | 718.376（655.298〜865.299） |
| 再読込 復元区間 `stage_ms` | 1,552.895（1,550.665〜2,032.850） | 630.082（623.688〜631.030） |
| 初回 復元完了の累積 `elapsed_ms` | 14,379.331（14,164.414〜17,764.658） | 13,403.426（13,241.696〜16,974.170） |
| 再読込 復元完了の累積 `elapsed_ms` | 4,157.021（4,150.338〜9,094.673） | 3,380.058（3,257.983〜4,869.963） |
| 初回 `open/complete` の `elapsed_ms` | 16,579.335（16,345.734〜20,180.974） | 15,613.392（15,456.942〜19,420.522） |
| 再読込 `open/complete` の `elapsed_ms` | 6,254.169（6,248.957〜11,370.386） | 5,511.533（5,371.552〜7,038.308） |

再読込の画面側復元区間は約59%、読込完了までは約12%短くなった。同じ更新8の設定比較であり、更新7のEXEや学校PCの時間との比較ではない。
3回目は両設定で読込完了までの時間が増えたため、ばらつきと最大値も残した。既定でも初回最大19.421秒、再読込最大7.038秒で、待ち時間がなくなったとは判定しない。

元の選択ケース、全レポート・区間別トレース・起動ログ、`measurements.json` と `summary.json` は `foreground-comparison/` に保存した。
