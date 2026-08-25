kachakachaCAD 使用ライブラリと権利
====================================

このフォルダには、kachakachaCAD が利用・同梱する第三者ソフトウェアの
著作権表示、ライセンス本文、SBOM（部品表）を収録しています。

kachakachaCAD 本体
------------------

kachakachaCAD 本体は GNU General Public License version 3 or later
（GPL-3.0-or-later）で公開しています。配布物ルートの LICENSE にライセンス全文、
COPYRIGHT に著作権表示と適用通知があります。

主な使用ライブラリ
------------------

* Qt 6.9.2
  画面、ウィンドウ、描画、PDF生成などに使用しています。
  Qt の共有 DLL を GNU Lesser General Public License version 3
  （LGPLv3）の条件で使用しています。

* Open CASCADE Technology 8.0.1
  3D形状の検査、STL/STEP出力に使用しています。
  GNU Lesser General Public License version 2.1 と
  Open CASCADE exception version 1.0 の条件で使用しています。

* FreeType、Brotli、bzip2、libpng、zlib
  Open CASCADE Technology の実行時依存として共有 DLL を同梱します。
  個別の条件は third-party フォルダ内に収録しています。

利用者の権利
------------

Qt と Open CASCADE Technology は、交換可能な共有 DLL として配置しています。
利用者は、各ライセンスが認める範囲で DLL を調査、変更、差し替えて実行できます。
kachakachaCAD の配布条件で、そのためのリバースエンジニアリングを禁止しません。

このソフトで作成した KCD、SVG、DXF、PDF、STL、STEP などのデータに、
Qt や Open CASCADE Technology のライセンスが自動的に適用されることはありません。
モデルの題材にした車両、ロゴ、商品デザインなど第三者の権利は別途確認が必要です。

詳細
----

* THIRD_PARTY_NOTICES.txt: 使用部品と著作権表示
* SOURCE-CODE-JA.txt: LGPL対象ライブラリのソース提供とDLL差し替え方法
* license-texts: GNU LGPL/GPLの全文
* qt-license-texts: Qt 6.9.2が収録する各ライセンスの全文
* third-party: vcpkgが収録した各部品の著作権表示とライセンス
* sbom: Qtおよびvcpkgが生成したSPDX形式の部品表

kachakachaCAD 本体のライセンスは、配布物のルートにある LICENSE に従います。
適用する版と著作権表示は、配布物のルートにある COPYRIGHT に従います。
