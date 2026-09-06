# Windows 95 UI テーマの基準

## 調査資料

- Microsoft Press, *The Windows Interface Guidelines for Software Design* (1995)。
  Microsoft Learn の参考文献にも同書が Windows 95 のUI設計資料として掲載されている。
  - https://learn.microsoft.com/en-us/windows/win32/appuistart/other-resources
  - https://www.aoisnow.net/blog/wp-content/uploads/2019/10/Microsoft_WindowsGuidelines.pdf
- Microsoft Learn, *Using MS Shell Dlg and MS Shell Dlg 2*。
  Windows 95/98/Me の論理シェルフォントと、Windows 98 日本語版での MS UI Gothic の対応を確認した。
  - https://learn.microsoft.com/en-us/windows/win32/Intl/using-ms-shell-dlg-and-ms-shell-dlg-2

## 採用した規則

- 面色は `#C0C0C0`、入力・一覧は白、選択は `#000080` と白文字に固定する。
- 通常ボタンは2段の隆起枠、押下ボタンと入力欄は2段の沈み枠にする。
- グループ枠は「沈み外枠 + 隆起内枠」、状態欄は1段の沈み枠にする。
- ツールボタンは常時隆起表示とし、押下時は枠を反転して文字と絵を右下へ1px移す。
  後年のフラットツールバー表現は使わない。
- ツールバーの標準アイコンは16x16px、ボタン高は最低22pxとする。
- 一覧の交互行色は使わず、ツリーの展開表示は三角ではなく9x9pxの `+` / `-` と点線にする。
- タブ、ポップアップメニュー、スクロールバー、スライダー、ドック見出しも同じ4色の立体枠で描く。
- 米国版の基準は MS Sans Serif 8pt だが、日本語を欠落なく表示するため日本語テーマでは
  Windows 95 と同時代の MS P Gothic 9ptを優先する。Windows 98 の MS UI Gothic は代替候補に下げる。
- 通常テーマ固有の角丸・淡色QSSはテーマ切替時に退避し、通常テーマへ戻すと復元する。
  表示色を選ぶスウォッチだけは入力値なので退避対象にしない。

## 実装箇所

- `src/apps/cad/Win95Style.cpp`: 部品描画、配色、寸法、ピクセルアイコン。
- `src/apps/cad/MainWindow.cpp`: テーマ切替、QSS退避復元、標準アイコン更新。
- `src/apps/cad/MainWindowSelfTest.cpp`: 配色、主要寸法、16pxアイコンの回帰確認。
