# 2026-11-28公開更新1の自宅PCへの導入

状態: 導入・通常起動・原本保持の確認が完了。
[公開版](https://github.com/tatata93/kachakachaCAD/releases/tag/v0.1.0-20261128-update1)のタグ先
`79fed8a2f75fd6173960e6e0e6977e3982123463`、latest、公開状態、assetをGitHubで照合した。

- ZIPは74,728,246バイト。SHA256 `fd3749c1e17235d61882e9bc0296c942d4b43b7deb673e31784d06d43dbd675d` とCRCが一致。
- ZIP内と展開後のEXEは指定SHA256 `4c814dabd6d21c911e423633a3ec0e1f35c46bbb3e813caeb8d8223d59eb40ba` と一致。実行前後に照合した。
- 通常clone内の `out/local-candidates/windows-20261128-update1-79fed8a2/package/kachakacha_cad_next.exe` へ、旧版とは別に展開した。
- 開発用PATH・Qt変数を除いた通常Windows起動2条件が成功。イベントループと画面上の表示を確認した。
- 既存ER2の作業コピー438エンティティのWindows読込・描画が成功。原本とコピーのSHA256は不変。

証拠は候補用作業ツリーの `out/20261128-update1-home/` と起動先の一つ上の `installation.json`。
学校側の水平合わせ・グリッド2点入力の関連試験、CTest197件、開発・配布UI各1116件等の公開結果と、自宅の導入・起動・モデル読込確認を区別する。
学校の公開ZIPを再ビルドなしで使用し、PC固有の絶対パスとバイナリはGitへ追加しない。
