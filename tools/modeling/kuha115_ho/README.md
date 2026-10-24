# 新規クハ115-1000・1/80 HOモデル

既存の列車文書を読み込まず、`build.py` の寸法・輪郭・押し出し・ロフト・Booleanから作る。
ユーザーの参考図は補助資料であり、図中のAU75Gのファン数や広窓の描写はメーカー資料・写真と照合して修正した。
生成文書の部品はネイティブの編集履歴を持つ。STLを編集の正本にはしない。

## 予想図から本モデルへ反映する修正（2026-10-24）

ユーザーは完成予想図の全体の方向性を了承したが、**正面貫通扉の窓が縦長過ぎる**と指摘した。
本モデルではこの窓を、実車写真の比率に合わせた背の低い丸角窓へ修正する。
扉全体の高さと窓の高さを独立して設定し、縦長の扉に合わせて窓まで引き伸ばさない。
開口・Hゴム外周／内周・透明板を同じ輪郭から作り、上端・左右幅・下端位置を実車正面写真で照合する。
前面用の修正を後妻の窓へ無条件に共用しない。予想図は形状の正本にしない。

この節は次の本モデルに対する確定した修正要件。比較用の旧CADは未修正であり、窓の数値寸法は未確定。

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
