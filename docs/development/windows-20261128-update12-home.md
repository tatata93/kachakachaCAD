# 2026-11-28公開更新12の自宅PCへの導入

状態: 公式ZIPの導入・通常Windows起動・重点UI試験・ER2二例の読込と原本保持が完了。
[公開版](https://github.com/tatata93/kachakachaCAD/releases/tag/v0.1.0-20261128-update12)のタグ先
`77412045627852e6648cd6206e297265fd55832c`、latest、公開状態とassetをGitHubで照合した。

- ZIPは74,758,916バイト。SHA256 `4f8f831f425d0d80f2ac52324c580f905ec9dcd0dcd70e00d67fc57e5b1128a7` とCRCが一致。
- EXEは指定SHA256 `246e72e702b0c431eabbd3f51e417a24eb765e0c9a7079851b0c8a28151bfe4a` と一致。同梱1,004ファイルはZIPと展開後の全バイトを照合した。
- 起動先は通常clone内の `out/local-candidates/windows-20261128-update12-77412045/package/kachakacha_cad_next.exe`。従来版と別に展開し、再ビルドしていない。
- 開発用PATH・Qt変数を除いた通常Windows起動2条件が成功。継承設定がwindowsとoffscreenの両方で、通常GUIのWindowsプラットフォーム、イベントループ、表示・描画を確認した。
- 同梱版で次の完全一致ケース3件がすべて成功した。総数3・失敗0、正確な試験名と各合否を実レポートで照合した。
  - `HP-OPEN-07 hidden native shapes mesh on first visibility`
  - `HP-OPEN-09 prepared transfer identities cancellation and poisoned cache`
  - `HP-HEAVY 非表示の押し出し輪郭を開き直す`
- 既存ER2の作業コピー438エンティティがWindowsで読込成功。原本とコピーのSHA256は前後不変で、`ec2aa399712c88a108bada7ab4cc80c8acb997b0a3e1e3be47d5e02c355c703b`。
- [個人OneDriveで受領した最新ER2](er2-workshop-20261128-home-receipt.md)も別の検証コピーを作り、984エンティティのWindows読込と描画に成功した。描画画像を確認し、再構築診断は出ていない。
  受領モデルとコピーのSHA256は前後不変で、`9190841d0661da0c428942794ef0930484269a5e8fa2e2d1054b6e35804c0710`。

更新12は文書再構築時に独立した押し出しの先行計算結果を採用し、元輪郭を再解決する重複計算を省く。
自宅では今回、押し出しを含む指定回帰と導入・読込の確認を行った。学校の115系の速度比較を自宅の計測値として扱わない。
学校側で完了したWindows全体・配布試験と、自宅の通常起動2条件・重点3件・実モデル読込の範囲を区別する。

受領ER2のフォルダーに `Open-ER2-with-update12.cmd` を追加し、この版とそのモデルを明示して開けるようにした。
従来の更新11用起動ファイル、各配布版、開いていたCADとユーザーモデル、他担当の作業は保持した。
製品コードとモデル・型紙を編集せず、PC固有の絶対パスと配布物はGitへ追加しない。

証拠は候補用作業ツリーの `out/20261128-update12-home/`。
`result.json`、重点 `focused-ui.json`、通常起動レポート、両ER2の読込ログ・描画画像と `model-launcher.json` を保存した。
起動先の一つ上にも `installation.json` を保存した。
