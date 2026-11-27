# 公開版更新6の自宅PCへの導入

2026-11-28。状態: 導入・通常起動・原本保持の確認が完了。
[公開版更新6](https://github.com/tatata93/kachakachaCAD/releases/tag/v0.1.0-20261127-update6)のタグ先
`e6a204c4b77b31e669fd9bc966011d8c08a3a00d`、latest、公開状態、assetをGitHubで照合した。

- ZIPは74,726,362バイト。SHA256 `16a5f7104f6a1fb208cfe1269854aaddff0c6f943ecff4287852f311cbd248ee` とCRCが一致。
- ZIP内と展開後のEXEは指定SHA256 `51485bda0038018ca023de3aaeec5af098b2ba41aa73efb3a2ffd14430b17ba7` と一致。実行前後とも照合した。
- 起動先は通常clone内の `out/local-candidates/windows-update6-e6a204c4/package/kachakacha_cad_next.exe`。旧版と作業ファイルは保持し、再ビルドしていない。
- 開発用PATH・Qt変数を除いた通常Windows起動2条件が成功。イベントループと画面上の表示を確認した。
- 既存ER2の作業コピー438エンティティのWindows読込・描画が成功。原本とコピーのSHA256は不変。

証拠は候補用作業ツリーの `out/update6-home/` と起動先の一つ上の `installation.json`。
学校側の初回再構築・118形状等の比較、CTest197件、開発・配布UI各1115件等の公開結果と、自宅で行った導入・起動・モデル読込確認を区別する。
学校側の性能値を自宅での実測へ置き換えず、PC固有の絶対パスと配布物はGitへ追加しない。
