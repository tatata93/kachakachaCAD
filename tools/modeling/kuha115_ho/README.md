# クハ115 長野色・1/80 HO16.5 のネイティブモデル

最新は `revised.py --detailed` と `assemble_detailed.py`（写真照合版）。
通常の `revised.py` は前回配布の再現用に保全する。
貫通扉窓はユーザーの指示どおり**横長寄り**とし、
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
STL弦誤差は標準0.025 mm、通風器・詳細版屋根・表示箱0.006 mm、
詳細版前照灯の反射鏡・レンズ0.001 mm。体積差上限は0.2%を維持する。

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
最新の部位画像・組立・比較と検査記録は `samples/series115-ho-photo-20261025/`。
通常版233部品、前後分割版240部品。屋根全体も前後に二分割する。
前回199/206部品の配布は `samples/series115-ho-revised-20261024/` に保全する。
実印刷、走行、市販動力・輪軸との互換は未試験。静止時の形状検査と区別する。

## 写真照合版を再現する場合

上記手順の生成には `--detailed` を付け、実行番号は10のような新しい番号を使う。
詳細版の画像生成は `render_revised.py` の代わりに次を使う（NumPy・Pillowが必要）。

```text
python tools/modeling/kuha115_ho/render_detailed.py RUN/exports10 RUN/candidate10/design.json RUN/previews10
python tools/modeling/kuha115_ho/review_revised.py RUN --run 10
python tools/modeling/kuha115_ho/test_revised_review.py RUN --run 10
python tools/modeling/kuha115_ho/assemble_detailed.py RUN SAMPLES PACKAGE --run 10 --comparison COMPARISON
```

COMPARISONには公開写真へのリンクと部位別照合を記録したcomparison.json、過去の
AI完成予想原画像previous-concept.pngを置く。実車写真を再配布物へ転載しない。
比較記録の雛形・今回の結果は新しいsamplesフォルダーにも収録する。
CAD保存後の再読込と通常デスクトップ表示、全対候補の干渉・全STL・STEP検査を省略しない。

## 2026-10-26 取付修正版

この時点の版は `samples/series115-ho-fitted-20261026/`。スカートの床板一体支持腕、窓ガラスの裏貼り面と
回込窓の接着受け、屋根曲面に沿うAU75Gの2本の横断サドルを追加した。通常233／分割240部品。
接触は各93か所を距離1e-5 mm以下と面積で検査し、離す負の試験も実BRepで実施する。
実印刷・接着強度・走行の検証とは区別する。局所寸法は模型設計値。

生成は `--detailed --bogie-component COMPONENT/tr62-component.kcd2` を付ける。
COMPONENTには以前検証した8ソリッドと編集元・由来証拠を保全する。
全体の手順に加えて、次を実行する（RUN、NATIVE_EXPORT、SAMPLESは実行先を指定）。

```text
python tools/modeling/kuha115_ho/test_attachment_fit.py RUN/native03/prepared.bin RUN/candidate03/design.json NATIVE_EXPORT --work RUN/negative04
python tools/modeling/kuha115_ho/render_fit.py RUN/exports03 RUN/attachments03
python tools/modeling/kuha115_ho/assemble_fit.py RUN SAMPLES --run 03
```

全対干渉の追加パスは、同一prepared tokenと全体レポートのSHAが一致する場合だけ、点・分割・接触の
済んだ検査を参照する。レビューは接触結果の欠落・0件・失敗・古い入力を拒否する。
`assemble_fit.py` は先に生成済みのsampleへ取付拡大画像と検査表を付ける。旧版を上書きしない。

## 2026-10-26 外形修正版（最新）

最新は `samples/series115-ho-contoured-20261026/`。前の版は接触していても、クーラーの
下部の隙間や全高平板の扉を残していた。`--contoured` を追加して、AU75G本体を低く納め、
下部キセの全面を屋根で切り合わせる。細いスカート支持腕を廃し、左右の上縁返しから床へ
つながる1閉ソリッドとする。実車が床板の一枚板を延長した構造だとの主張ではない。
客扉12枚と乗務員扉2枚は、車体と同じ下部断面則を使用する。通常232／分割239部品。
旧版の再現は `--contoured` を付けずに行う。既存の保存モデルを上書きしない。

上の生成・全BRep・STEP/STL・干渉候補・保存再読込・Windows表示の手順を実施した後に、
以下で比較と梱包を行う。BASELINEは前版のnative03とcandidate03の検証作業フォルダー。
以前の証拠がない環境では、`--contoured` なしの前版も検証済みCADで再生成して用意する。

```text
python tools/modeling/kuha115_ho/revised.py --detailed --contoured --bogie-component COMPONENT/tr62-component.kcd2 --output RUN/candidate04
python tools/modeling/kuha115_ho/test_contour_geometry.py BASELINE RUN NATIVE_EXPORT --run 04 --work RUN/negative-old-geometry04
python tools/modeling/kuha115_ho/render_detailed.py RUN/exports04 RUN/candidate04/design.json RUN/previews04
python tools/modeling/kuha115_ho/render_contours.py RUN/exports04 BASELINE/exports03 RUN/sections04
python tools/modeling/kuha115_ho/assemble_contoured.py RUN SAMPLES PACKAGE --run 04
```

各版949点の断面・開口、全可視部品の1閉ソリッド、接触・全対候補を検査する。
旧モデルへ新しい断面検査を適用して、クーラー空隙・非連続床・14枚の平板扉の計16件を拒否する。
乗務員扉の検査はヒンジ等との結合後の可視ID、分割床は前後の可視IDを参照する。
詳細断面の画像は実CADのSTLと平面の交線であり、実車実測図や操作画面ではない。
