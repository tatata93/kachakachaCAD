# 公開版更新3の自宅PCへの導入

状態: 導入と自宅での通常起動確認が完了。
[公開版更新3](https://github.com/tatata93/kachakachaCAD/releases/tag/v0.1.0-20261127-update3)のタグ先は
`86a82b03c93909c3a0d3028a4b1c65251749c628`。GitHubのlatest、非draft・非prerelease、assetのサイズとdigestを照合した。

| 確認対象 | 結果 |
| --- | --- |
| Windows用ZIP | 74,725,909バイト。SHA256 `c5e19ab27031c7d6694d0e64ad791ada56535c2055a65d11dece8dd50c3f8b74` が一致 |
| 展開 | CRC、展開範囲、必要なDLLとQt設定を確認し、新規フォルダーへ展開 |
| 起動 | 開発用PATH・Qt変数を除いた通常Windows起動2条件が成功。イベントループ、画面上の表示、日本語ラベルを確認 |
| モデル読込 | ER2作業コピー438エンティティのWindows上での読込・描画が成功。原本とコピーのSHA256は不変 |
| 実行ファイル | ZIP内のバイナリと一致。SHA256 `0f88cf45efb20b4e4d64b0bf99a635a45e39c1c1da0dcaedee87e13c868ca095` |
| 同梱核 | 配布物の版表示でOpen CASCADE 8.1を確認。自宅の開発用OCCTへ差し替えない |

起動先は自宅の通常clone内の
`out/local-candidates/windows-update3-86a82b03/package/kachakacha_cad_next.exe`。
既存の候補版05と旧公開版を置き換えず、同じ公開ZIPを再ビルドなしで導入した。
PC固有の絶対パスとバイナリはGitへ追加しない。

自宅側の取得・検証記録は候補用作業ツリーの `out/update3-home/`、
起動先の一つ上に `installation.json` を保存した。
学校側の公開記録にはCTest197件、開発・同梱UI各1115件、旧自己試験、通常起動2条件の成功がある。
それらを自宅で再実行した証拠とはせず、自宅の導入・起動・モデル読込結果と区別する。
