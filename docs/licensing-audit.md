# 権利・ライセンス調査

調査日: 2026-08-25

これは開発上のライセンス棚卸しであり、個別案件についての法律相談ではない。
公開配布前には、配布地域と配布方法を伝えて知的財産に詳しい専門家へ最終確認する。

## 結論

- 現在の構成は、無償・有償を問わずデスクトップアプリとして配布できる。
- Qt と OCCT は共有 DLL として動的リンクしている。アプリ本体を非公開にすることも可能だが、LGPLの表示、ライセンス全文、対応ソース、DLL差し替え権を満たす必要がある。
- このCADで作ったKCD、SVG、DXF、PDF、STL、STEPへ、Qt/OCCTのライセンスが自動適用されることはない。作者は自作モデルを無償でも有償でも配布できる。
- ただし、実在車両の形状、社章、ロゴ、商品名、写真、他人の図面を再現したモデルは別問題である。意匠権、商標権、著作権、不正競争防止法、資料の利用条件を個別に確認する。
- kachakachaCAD本体は `GPL-3.0-or-later` で公開する。改変版を配布する場合も利用者の自由と対応ソースを維持する。

## 実際に配布される部品

| 部品 | 使用版 | 配布形態 | 主な条件 |
|---|---:|---|---|
| kachakachaCAD | 0.1.0 | EXE | GPL-3.0-or-later |
| Qt Core/GUI/Widgets | 6.9.2 | 共有DLL・プラグイン | LGPL-3.0-onlyを選択可能 |
| Open CASCADE Technology | 8.0.1 | `TK*.dll` | LGPL-2.1-only + OCCT例外 |
| FreeType | 2.14.3 | `freetype.dll` | FreeType Licenseを選択 |
| Brotli | 1.2.0 | DLL | MIT |
| bzip2 | 1.0.8 | `bz2.dll` | bzip2 license |
| libpng | 1.6.58 | `libpng16.dll` | libpng-2.0 |
| zlib | 1.3.2 | `z.dll` | zlib |
| Khronos EGL/OpenGL registry | 使用ヘッダ版 | OCCT/Qtのビルド入力 | Apache-2.0、MIT、ファイル別条件 |
| Yu Gothic / Meiryo | Windows搭載版 | OSから画面・PDF描画に使用、フォントファイルは非同梱 | Windowsのフォント条件 |
| Visual C++ runtime | ビルド環境依存 | 必要な場合のみ | Visual Studio REDIST条件 |

設計文書にあるEigenは、現在のソースでincludeもリンクもされておらず、配布物にも入っていない。この版の権利表示対象ではない。

Windows搭載フォントはアプリへコピーしておらず、利用者のWindowsから読み込む。Microsoftは、埋め込みフラグに従うアプリが作成したPDFなどへの文書フォント埋め込みと、その文書の一般的な配布を認めている。フォントファイル自体をアプリへ同梱したり、別形式へ変換して配布したりはしない。

## Qtで必要な対応

Qt公式のLGPL案内に従い、公開配布では次を行う。

1. Qtを使用していることとLGPLv3であることを、ヘルプ画面と配布文書に明示する。
2. LGPLv3とGPLv3の全文、QtのSBOMと第三者表示を同梱する。
3. 使用したQtの完全な対応ソースを、配布者自身が管理する場所から取得できるようにする。Qt公式サイトへのリンクだけでは不足する。
4. Qt DLLを利用者が交換して実行できる状態を維持する。解析・改変・差し替えを利用規約で禁止しない。
5. Qtを変更した場合は、その変更を含む対応ソースを提供する。

このアプリは `Qt6*.dll` を動的に読み込むため、差し替え可能性の面では適切である。公開配布用スクリプトは、不要なQtプラグイン、MesaソフトウェアOpenGL、D3Dコンパイラを含めない。

## OCCTで必要な対応

OCCT公式はLGPLv2.1と追加例外を採用している。公開配布では次を行う。

1. OCCT使用の目立つ表示とLGPLv2.1全文、OCCT例外を同梱する。
2. 配布したOCCT DLLに対応する完全なソースを同じ配布場所から取得可能にする。
3. `TK*.dll` を利用者がABI互換版へ交換できる状態にする。
4. OCCTを変更した場合は変更表示と変更後ソースを提供する。

OCCT例外により、OCCTヘッダ由来のコードを含むアプリのオブジェクトコードは任意の条件で配布できる。ただしOCCTを使っている旨の目立つ表示は必要である。

## 作成モデルの販売

ライブラリ側の観点では可能である。GNUの公式FAQも、一般にプログラムのライセンスは利用者自身の入力から生成した出力へ及ばないとしている。本ソフトはライブラリのソースや画像素材をSTL/STEPへ埋め込まない。

鉄道模型では次を分けて確認する。

- 自分で創作した架空車両、治具、部品: 原則として作者が配布条件を決められる。
- 実在車両の忠実な模型: 登録意匠や商品形態などの確認が必要。日本の意匠権は出願日から最長25年。
- 鉄道会社名、社章、ロゴ、車両愛称: 商標・表示の問題があるため、販売前に権利者のガイドラインや許諾を確認する。
- 他人の写真、図面、3Dデータから作成: 元資料の著作権・契約条件を確認する。購入資料であっても再配布権まで得られるとは限らない。
- 「公式」「公認」と誤認させる表示: 許諾がない限り避ける。

モデル配布時の推奨表示例:

```text
本データは作者が作成した非公式の鉄道模型用データです。
各鉄道事業者・車両メーカーとは関係ありません。
会社名、商品名、ロゴ等は各権利者に帰属します。
```

この表示だけで権利侵害が解消するわけではない。忠実な実車模型を継続的に販売する段階では、対象ごとの確認を行う。

## 採用した本体ライセンス

### GPL-3.0-or-later

ソフト本体を今後も自由ソフトとして守るため、このライセンスを採用した。ソース公開と再配布を認める一方、配布される改変版も同じ自由を保つ。Qt/OCCTとの説明がまとまりやすく、作成モデルには自動適用されない。非公開プラグインを将来認めたい場合は設計に注意する。

### MPL-2.0

変更したファイル単位で公開を求める中間案。企業利用や別ライセンスの部品と組み合わせやすい。Qt/OCCTのLGPL義務は別途残る。

### MIT

最も簡単で、第三者による商用・非公開派生も許す案。普及を優先する場合に向く。改良版を公開へ戻す義務はない。Qt/OCCTのLGPL義務は別途残る。

無償のバイナリだけを配る独自ライセンスも可能だが、すでにソースを公開して共同開発する方針とは噛み合いにくく、QtのLGPL対応も簡単にはならない。

## 公開前チェック

- [x] 本体ライセンスを決め、ルートへ `LICENSE` と `COPYRIGHT` を追加した。
- [ ] 全コード・画像・サンプルの著作者を確認した。
- [ ] `package-cad.ps1` で生成した `legal` フォルダを削除していない。
- [ ] 同じReleaseページへ `package-third-party-source.ps1` のソース一式を置いた。
- [ ] Qt/OCCTのDLLを静的リンクへ変更していない。
- [ ] 利用規約でLGPL対象DLLの解析・改変・差し替えを禁止していない。
- [ ] 実在車両データを同梱する場合、その権利と資料出典を確認した。
- [ ] 配布地域・ストア規約を含め、専門家の最終確認を受けた。

## 公式資料

- Qt, Obligations of the GPL and LGPL: https://www.qt.io/development/open-source-lgpl-obligations
- Qt Open Source Licensing FAQ: https://www.qt.io/faq/qt-open-source-licensing
- Qt Third-Party Code: https://doc.qt.io/qt-6/licenses-used-in-qt.html
- Open CASCADE Technology: https://github.com/Open-Cascade-SAS/OCCT
- GNU License FAQ（生成物とLGPL動的リンク）: https://www.gnu.org/licenses/gpl-faq.en.html
- Microsoft Visual C++再配布: https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files
- Microsoft Windowsフォントの文書埋め込み: https://learn.microsoft.com/en-us/typography/fonts/font-faq
- 特許庁 意匠制度の概要: https://www.jpo.go.jp/system/design/gaiyo/seidogaiyo/torokugaiyo/index.html
- 特許庁 権利侵害とは: https://www.jpo.go.jp/support/ipr/kenrishingai.html
