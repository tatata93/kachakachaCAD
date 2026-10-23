# Native 115系モデルの再生成と検証

製品実装から独立したモデル作成用ツール。長さはmm、1/80。`../native_kcd.py` を再利用する。詳細・出典・未確認事項は `samples/series115-nagano-print/README.md` を参照。

## 作成

Python 3 + Pillowを用意し、リポジトリのルートから:

```sh
python tools/modeling/series115_print/build_model.py --output out/series115-generated
```

同じ出力先の生成ファイルは更新するので、ユーザーが編集したモデルを生成先にしない。

## 配布CADによる再生成

Windows/Linuxとも同じPython入口。`<exe>`はそのOSで実行できる検証済みCADのパス。

```sh
python tools/modeling/series115_print/validate_model.py out/series115-generated/kuha115-1000-nagano-1-80.kcd2 <exe> --work out/series115-check/main
python tools/modeling/series115_print/validate_model.py out/series115-generated/kuha115-1000-nagano-1-80-split.kcd2 <exe> --work out/series115-check/split
```

作業フォルダーは新規のみ。文書原本は書換えない。Qtをoffscreenで使用するため、これは実際のOSマウス操作の証拠とは区別する。Windowsでは別途通常起動し、実画面も保存した。

## OCCTによる検査・出力

既存のQt6/OCCT動的リンク環境を使う。依存の位置はユーザーのCMake設定/環境で指定する。新規製品依存はない。

```sh
cmake -S tools/modeling/series115_print -B out/series115-export -DCMAKE_BUILD_TYPE=Release
cmake --build out/series115-export --config Release --parallel 2
```

WindowsのVS generatorでは `out/series115-export/Release/native_print_export.exe`、単一構成なら `out/series115-export/native_print_export`。Qt/OCCTの実行時ライブラリーが必要。下記 `<exporter>` を対応する実行ファイルに置換する。

```sh
<exporter> out/series115-check/main/prepared.bin out/series115-generated/design.json out/series115-check/main/exports
<exporter> out/series115-check/split/prepared.bin out/series115-generated/design-split.json out/series115-check/split/exports
python tools/modeling/series115_print/check_stl.py out/series115-check/main/exports
python tools/modeling/series115_print/check_stl.py out/series115-check/split/exports
python tools/modeling/series115_print/package_print.py out/series115-check/main/exports out/series115-check/split/exports out/series115-generated out/series115-print-delivery
python tools/modeling/series115_print/check_stl.py out/series115-print-delivery/full-car
python tools/modeling/series115_print/check_stl.py out/series115-print-delivery/small-printer
```

準備バイナリーは現在のCADが作成する内部形式version2。互換性を仮定せずmagic/versionを確認する。空のmanifest、部品0件、BRep不正、STEP不一致等は失敗。形状正本はKCD2のフィーチャーであり、この中間BRepではない。

部位ごとの出力STLは平行移動のみ。出力後にも `check_stl.py` を通す。`print-parts.json` とネイティブ再生成の `validation.json` は実行トークンで対応を確認する。製品の全体ゲートをこのモデル専用検査へ置換しない。

## 画像

`make_views.py` は主要寸法に合わせた独自の三面図を生成する。日本語フォントはWindows標準のMeiryoがあれば使用し、他OSではPillow既定フォントにフォールバックする。`render_preview.py` はPillow/NumPyでSTLの実形状をZバッファ描画し、塗装帯を説明する。モデル形状を改変した美化画像ではないが、実CAD画面でもなく、透明ガラスの物理的光学表現でもない。

```sh
python tools/modeling/series115_print/render_preview.py out/series115-check/main/exports out/series115-generated
```

サポート自動生成、スライサー実行、物理印刷、走行試験は含めていない。
