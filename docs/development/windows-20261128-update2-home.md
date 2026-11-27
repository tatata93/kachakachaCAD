# 2026-11-28公開更新2の自宅PCへの導入

状態: 導入・通常起動・原本保持の確認が完了。
[公開版](https://github.com/tatata93/kachakachaCAD/releases/tag/v0.1.0-20261128-update2)のタグ先
`6e8692106bfc1690a5e3b2f0c23c7f2c9df2fedd`、latest、公開状態、assetをGitHubで照合した。

- ZIPは74,730,400バイト。SHA256 `a03a3381e2563b43197e0840026de865dec02863417650c02e6453c27246f23e` とCRCが一致。
- ZIP内と展開後のEXEは指定SHA256 `cb18701abd7d91f5adb239459d7d9d9f6f7a27846ec56604d3f00fc73ece1e51` と一致。実行前後に照合した。
- 起動先は通常clone内の `out/local-candidates/windows-20261128-update2-6e869210/package/kachakacha_cad_next.exe`。旧版とは別に展開し、再ビルドしていない。
- 開発用PATH・Qt変数を除いた通常Windows起動2条件が成功。イベントループと画面上の表示を確認した。
- 既存ER2の作業コピー438エンティティのWindows読込・描画が成功。原本とコピーのSHA256は不変。

証拠は候補用作業ツリーの `out/20261128-update2-home/` と起動先の一つ上の `installation.json`。
学校側の115系・ER2の転送データと速度比較、CTest197件、開発・配布UI各1116件等の公開結果と、自宅の導入・起動・モデル読込確認を区別する。
PC固有の絶対パスとバイナリはGitへ追加しない。
