# クハ115 長野色・1/80 HO16.5 のネイティブモデル

最新は `revised.py`。貫通扉窓はユーザーの指示どおり**横長寄り**とし、
開口7.2×5.8 mm、幅/高さ1.241、Hゴム外周7.6×6.2 mmにした。
扉全高22.9 mmとは独立。開口・Hゴム・ガラスを同じ設計値から作る。
数値は写真照合による模型設計値で、実測寸法として確定したものではない。

以前の列車文書は読み込まず、ワイヤー・押し出し・ロフト・Booleanから作る。
`build.py` の汎用生成ヘルパーを再利用するが、旧形状文書は流用しない。
`build.py` / `review.py` / `render.py` / `assemble_delivery.py` は旧モデルの再現用に保全した入口。
最新モデルには以下の `revised` の付いた入口を使う。

## 生成と検証

Python 3.10以上。ネイティブ生成には追加Python依存なし、画像生成にはNumPyとPillowが必要。
既存のQt/OCCTビルド環境と検証済みCADを利用する。Windows/Linux共通のPython入口で、
デスクトップ表示確認だけはWindows実機専用。下記の `RUN` と `CAD_EXE` は実行先に置き換える。
新規の実行フォルダーを使い、以前の検証証拠へ上書きしない。

```text
python tools/modeling/kuha115_ho/revised.py --output RUN/candidate04
python tools/modeling/series115_print/validate_model.py RUN/candidate04/kuha115-ho80-revised.kcd2 CAD_EXE --work RUN/native04
python tools/modeling/series115_print/validate_model.py RUN/candidate04/kuha115-ho80-revised-split.kcd2 CAD_EXE --work RUN/split-native04
native_print_export RUN/native04/prepared.bin RUN/candidate04/design.json RUN/exports04
native_print_export RUN/split-native04/prepared.bin RUN/candidate04/design-split.json RUN/split-exports04
python tools/modeling/series115_print/check_stl.py RUN/exports04
python tools/modeling/series115_print/check_stl.py RUN/split-exports04
python tools/modeling/kuha115_ho/assembly_candidates.py RUN/exports04/print-parts.json RUN/candidate04/design.json RUN/candidate04/design-all-pairs.json
python tools/modeling/kuha115_ho/assembly_candidates.py RUN/split-exports04/print-parts.json RUN/candidate04/design-split.json RUN/candidate04/design-split-all-pairs.json
native_print_export RUN/native04/prepared.bin RUN/candidate04/design-all-pairs.json RUN/all-pairs04 --check-only
native_print_export RUN/split-native04/prepared.bin RUN/candidate04/design-split-all-pairs.json RUN/split-all-pairs04 --check-only
```

`native_print_export` は `tools/modeling/series115_print` の既存CMakeターゲット。
WindowsではQtとOCCT依存DLLのあるフォルダーをPATHに追加する。
`--check-only` はBRep検査のみであり、正式なSTEP/STL出力検査の代わりにはしない。
STL弦誤差0.025 mm、通風器のみ0.006 mm、体積差上限0.2%。

CAD保存文書は `review.extract_roundtrip(prepared, destination)` でprepared内から取り出す。
生成元とUUID・定義・所属・グループ・表示状態・材質を比較する。
通常版を `RUN/native04/cad-serialized.kcd2` へ取り出した後、さらに再読込する。

```text
python tools/modeling/series115_print/validate_model.py RUN/native04/cad-serialized.kcd2 CAD_EXE --work RUN/reopen04
native_print_export RUN/reopen04/prepared.bin RUN/candidate04/design.json RUN/reopen-geometry04 --check-only
python tools/modeling/kuha115_ho/desktop_model.py RUN/native04/cad-serialized.kcd2 CAD_EXE --work RUN/desktop04
python tools/modeling/kuha115_ho/review_revised.py RUN --run 04
python tools/modeling/kuha115_ho/render_revised.py RUN/exports04 RUN/previews04
python tools/modeling/kuha115_ho/assemble_revised.py RUN SAMPLES PACKAGE --run 04
```

分割版保存文書も `RUN/split-native04/cad-serialized.kcd2` に取り出す。
レビューが同ファイルを抽出するため、最終レビュー成功後に梱包できる。
検査はソースSHA・CAD実行ファイルSHA・preparedトークン・可視部品ID・件数を照合し、
古い候補リスト、失敗、0件、不一致を成功にしない。AABB候補集合は最新BRep境界箱から再計算する。

## 対象と限界

クハ115-1222／N15の長野色営業時代を外装候補とした設計案。
局所R・窓位置・小機器・床下同定には推定が残り、特定車番の完全な実測復元ではない。
最新の部位画像・組立・検査記録は `samples/series115-ho-revised-20261024/`。
通常版199部品、前後分割版206部品。屋根全体も前後に二分割する。
実印刷、走行、市販動力・輪軸との互換は未試験。静止時の形状検査と区別する。
