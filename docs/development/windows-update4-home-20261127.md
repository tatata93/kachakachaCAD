# 公開版更新4の自宅PCへの導入

状態: 導入・通常起動確認が完了。
[公開版更新4](https://github.com/tatata93/kachakachaCAD/releases/tag/v0.1.0-20261127-update4)のタグ先
`42b77327f83a73ac5c4c02aba6b545af7beccc87`、latest、公開状態、assetをGitHubで照合した。

- ZIPは74,725,970バイト。SHA256 `978c8e42f70d56bb1b4e46c05a80d8e5ee33108adb78916e720805ab4f601386` とCRCが一致。
- 起動先は通常clone内の `out/local-candidates/windows-update4-42b77327/package/kachakacha_cad_next.exe`。旧版とは別に展開した。
- 開発用PATH・Qt変数を除いた通常Windows起動2条件が成功。イベントループ、画面上の表示、日本語ラベルを確認。
- ER2作業コピー438エンティティのWindows読込・描画が成功。原本とコピーのSHA256は不変。
- 実行ファイルはZIP内と一致。SHA256 `5a43e9cf336968cf6e5e060282f9556427c082697a271e6d72afced8aa440b8c`。
- 配布物の版表示はOpen CASCADE 8.1。自宅の開発用ライブラリへ差し替えず、再ビルドしていない。

証拠は候補用作業ツリーの `out/update4-home/` と起動先の一つ上の `installation.json`。
学校側の公開記録にあるSVG再出力の比較、CTest197件、開発・同梱UI各1115件、旧自己試験等は、自宅で再実行した証拠とは区別する。
自宅では上記の配布物同一性・導入・起動・モデル読込を確認した。PC固有の絶対パスとバイナリはGitへ追加しない。
