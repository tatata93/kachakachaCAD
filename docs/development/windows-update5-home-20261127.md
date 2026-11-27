# 公開版更新5の自宅PCへの導入

状態: 導入・自宅での通常起動確認が完了。
[公開版更新5](https://github.com/tatata93/kachakachaCAD/releases/tag/v0.1.0-20261127-update5)のタグ先
`d19fa23b742508a760959fe522767d5aaa904b4b`、latest、公開状態、assetをGitHubで照合した。

- ZIPは74,726,421バイト。SHA256 `0e090459b99983018b8d5797b25d1c94cf93b3390798f50b664c3b740b37dfda` とCRCが一致。
- 通常clone内の `out/local-candidates/windows-update5-d19fa23b/package/kachakacha_cad_next.exe` へ、旧版とは別に展開した。
- 開発用PATH・Qt変数を除いた通常Windows起動2条件が成功。イベントループと画面上の表示を確認した。
- ER2作業コピー438エンティティのWindows読込・描画が成功。原本とコピーのSHA256は不変。
- 実行ファイルはZIP内と一致。SHA256 `91cd3ee804e3f7f53bf810b06b7c1308744acd165fc50c50dee577bdf949b030`。

証拠は候補用作業ツリーの `out/update5-home/` と起動先の一つ上の `installation.json`。
学校側の複数開口面の性能・形状比較、CTest197件、開発・同梱UI各1115件等の公開結果と、自宅の導入・起動・モデル読込確認を区別する。
学校の配布ZIPを再ビルドなしで使用し、PC固有の絶対パスとバイナリはGitへ追加しない。
