# 2026-11-28公開更新10の自宅PCへの導入

状態: 公式ZIPの導入・通常Windows起動・重点UI試験・ER2読込と原本保持が完了。
[公開版](https://github.com/tatata93/kachakachaCAD/releases/tag/v0.1.0-20261128-update10)のタグ先
`aeb11cd75aa06d2daefa507541835353f309fe28`、latest、公開状態とassetをGitHubで照合した。

- ZIPは74,758,012バイト。SHA256 `be4dd12a4c4a636b90c8b461c226987cd016d50e0710f55c0a58ec641e17cb55` とCRCが一致。
- EXEは指定SHA256 `d537bff44eab8876cf6c3aba5405a5a48c5621af1bc96947ec744af294becfdf` と一致。同梱1,004ファイルはZIPと展開後の全バイトを照合した。
- 起動先は通常clone内の `out/local-candidates/windows-20261128-update10-aeb11cd7/package/kachakacha_cad_next.exe`。従来版と別に展開し、再ビルドしていない。
- 開発用PATH・Qt変数を除いた通常Windows起動2条件が成功。継承設定がwindowsとoffscreenの両方で、通常GUIはWindowsプラットフォーム、イベントループ、表示・描画を確認した。
- 同梱版で次の完全一致ケース3件がすべて成功した。総数3・失敗0、試験名と各合否を実レポートで照合した。
  - `HP-XP line and curve positions by length from the explorer`
  - `HP-XP explicit tree endpoints, workplane roots and face roles`
  - `HP-XP explorer and viewport two-point directions`
- 既存ER2の作業コピー438エンティティをWindowsで読み、描画画像を確認した。原本とコピーのSHA256は前後不変で、`ec2aa399712c88a108bada7ab4cc80c8acb997b0a3e1e3be47d5e02c355c703b`。

更新10は点入力中に、左ペインのワイヤーを右クリックし、「部分を指定」から線分の「中点」と「始点からの長さ (%)」を選べる。
割合を入力して「指定」またはEnterで確定すると、元曲線上の位置が既存の点入力へ渡る。
ベジェ・Bスプラインの位置は長さの近似計算から求める。変更点は公開ソース・説明書と今回の同梱UI試験を照合した。

証拠は候補用作業ツリーの `out/20261128-update10-home/` の `result.json`、`focused-ui.json`、起動レポート、ER2読込ログ・画像と、起動先の一つ上の `installation.json`。
学校側で完了したWindows全体ゲート・従来自己試験と、自宅で行った導入・重点3件の確認を区別する。全体ゲートを自宅で再実行したという意味ではない。
従来版、ユーザー作業ブランチとモデル、他担当の変更は保持し、製品コードは編集していない。
PC固有の絶対パスと配布バイナリはGitへ追加しない。
