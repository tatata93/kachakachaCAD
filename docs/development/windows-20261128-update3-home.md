# 2026-11-28公開更新3の自宅PCへの導入

状態: 導入・通常起動・重点UI試験・原本保持の確認が完了。
[公開版](https://github.com/tatata93/kachakachaCAD/releases/tag/v0.1.0-20261128-update3)のタグ先
`ac85dae686d26655f8ba23df7169ffe744f5950b`、latest、公開状態、assetをGitHubで照合した。

- ZIPは74,732,125バイト。SHA256 `6997f57a8be14313f416ca13f231941e88126d7025f663d4f5684adbef528f81` とCRCが一致。
- ZIP内と展開後のEXEは指定SHA256 `d5080e498d666da6cafa5dabc83d4b9f9af7d975dbd3b2e368b01767c913de2c` と一致。実行前後に照合した。
- 起動先は通常clone内の `out/local-candidates/windows-20261128-update3-ac85dae6/package/kachakacha_cad_next.exe`。旧版とは別に展開し、再ビルドしていない。
- 開発用PATH・Qt変数を除いた通常Windows起動2条件が成功。イベントループと画面上の表示を確認した。
- 同梱EXEで `HP-OUT-21 pattern preview matches paper SVG coordinates` を完全一致で選び、オフスクリーン重点UI試験1件が成功。型紙下見の座標と印刷用SVGの座標の一致を検証した。
- 既存ER2の作業コピー438エンティティのWindows読込・描画が成功。画像を確認し、原本とコピーのSHA256は不変。

証拠は候補用作業ツリーの `out/20261128-update3-home/` と起動先の一つ上の `installation.json`。
学校側の実ER2透明板8枚の下見・SVG比較、CTest197件、開発・配布UI各1117件等の公開結果と、
自宅の導入・起動・重点UI1件・モデル読込確認を区別する。自宅では全体ゲートと透明板8枚の実操作を再実行していない。
PC固有の絶対パスとバイナリはGitへ追加しない。
