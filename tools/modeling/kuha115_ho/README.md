# 新規クハ115-1000・1/80 HOモデル

既存の列車文書を読み込まず、`build.py` の寸法・輪郭・押し出し・ロフト・Booleanから作る。
ユーザーの参考図は補助資料であり、図中のAU75Gのファン数や広窓の描写はメーカー資料・写真と照合して修正した。
生成文書の部品はネイティブの編集履歴を持つ。STLを編集の正本にはしない。

## 再生成と検査

Python 3.10以上。生成にはPillow、画像化にはNumPyも必要。以下の変数は各環境のパスに置き換える。
Windows/Linuxとも同じPython入口を使う。既存のCAD・Qt/OCCT環境を利用し、新たな依存は追加しない。

```text
python tools/modeling/kuha115_ho/build.py --output RUN/delivery-model --reference REFERENCE.png
python tools/modeling/series115_print/validate_model.py RUN/delivery-model/kuha115-1000-ho80-new.kcd2 CAD_EXE --work RUN/final-check
python tools/modeling/series115_print/validate_model.py RUN/delivery-model/kuha115-1000-ho80-new-split.kcd2 CAD_EXE --work RUN/split-check
```

作業先は新規フォルダーにする。CADの出力した`prepared.bin`を使い、既存のQt/OCCT用CMake環境で
`tools/modeling/series115_print`の`native_print_export`をビルドして実行する。

```text
native_print_export RUN/final-check/prepared.bin RUN/delivery-model/design.json RUN/verified-main
native_print_export RUN/split-check/prepared.bin RUN/delivery-model/design-split.json RUN/verified-split
python tools/modeling/series115_print/check_stl.py RUN/verified-main
python tools/modeling/series115_print/check_stl.py RUN/verified-split
python tools/modeling/kuha115_ho/review.py RUN
python tools/modeling/kuha115_ho/render.py RUN/verified-main PREVIEWS
```

`--check-only`は出力前の幾何検査専用。STEP/STL検査や完成出力の代わりにはしない。
STL弦誤差は通常0.025 mm、曲面底を持つ通風器は0.005 mm。検査の体積誤差上限0.2%は従来から変更していない。
`review.py`はソースSHA、CAD実行ファイルSHA、トークン、可視部品ID、検査件数、失敗、
STEP再読込、STL閉鎖性、分割再組立、CAD保存構造を照合する。
`final-check/cad-serialized.kcd2`はCADが保存した文書であり、追加の読込確認に使用できる。

## 対象と限界

長野色・黒Hゴム・AU75Gを装備した更新後の代表的なクハ115-1000。
特定車番・撮影年月の完全な実測復元ではない。小寸法の根拠、組立方法、検査結果は
`samples/series115-ho-new-20261024/README.md`と`index.html`に記録する。
通常版149部品、小型プリンタ版156部品。印刷・走行・市販動力との互換は実機未確認。
