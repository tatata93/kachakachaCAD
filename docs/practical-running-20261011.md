# 実用見本と検証版の使い方

この作業は専用の `cad-validation` クローンで実施した。
元のリポジトリと、その未コミット変更は変更していない。実際の開始位置は作業台帳に記録した。

## 起動

退避版の `runtime.zip` を空のフォルダーに展開し、`kachakacha_cad_next.exe` を起動する。
DLL、`platforms` 等のフォルダーも一緒に置く。EXEだけを移動しない。
アプリの「ファイル→開く」で見本の `.kcd2` を開く。コマンドラインからも開ける。

```powershell
.\kachakacha_cad_next.exe --open 'C:\path\to\sample.kcd2'
```

`checkpoint-082` は全190件の回帰試験に通った退避版。SVG名前空間・複数ページ型紙の修正前なので、
型紙出力には最終納品の最新検証版を使う。`checkpoint-063` と `checkpoint-005` も上書きせず残した。
各版のソース、配布物、ハッシュ、試験ログは同じチェックポイント内で対応付ける。

## 見本の開き方

- `source.kcd2` は作図・参照断面・部材の元データ。
- `approximation.kcd2` は製造モデルで、分割数・部材・組立状態を編集できる。
- `ideal-history.kcd2` / `approximation-history.kcd2` は結合までの履歴を持つ。開く際に形状を再構築する。
- `*-independent.kcd2` は独立した形状で、元の作図履歴への依存がない。形状を確認したいときに使う。
- STLには単位情報がない。この見本の数値はすべてmm。STEPの寸法も同じ尺度。
- `patterns` の PDF は原寸1:1で印刷する。「用紙に合わせる」を無効にする。参照縮尺は実物との比であり、印刷倍率ではない。
- SVG/DXFは型紙の下見で表示している1ページ、PDFは全ページを出す。複数ページのSVG/DXFにはページ番号を付けてある。

壺など多数の面を持つ結合履歴は再読込に数十分かかる。完了ログ・診断を待ち、CPUが一瞬止まったことだけで完了と判断しない。
独立版を添付した場合も、編集可能な元の履歴は別に残す。診断BRepから準備した独立版には由来を明記する。

## サンプルの対応表（12:02 UTC時点）

各フォルダーの `source.kcd2` が元の作図。`ideal` は理想形状のソリッド、`approximation` は近似部材の結合体。
各完成フォルダーの `assembly.stl` / `assembly.step` は同じ段階の出力で、混同しない。

| フォルダー | 理想形状 | 近似と展開 | 出力・残件 |
|---|---|---|---|
| `115` | 1:100、46部材結合。40開口を含む | 68部材結合、6組の型紙。最大偏差0.070933mm | 理想/近似ともSTL・STEP・履歴再読込完了。ドアの片側0.18mmの張り出しは幅に含む |
| `n700` | 1:150、34部材結合 | 車体9製造モデル75パネルと既存詳細の計100部材。9組の型紙、最大偏差0.044405mm | 理想/近似ともSTL・STEP・履歴再読込完了。先頭の尖った閉じたロフトはソリッドを保持し、無理な一様オフセットや展開をしない |
| `aerosol` | 1:2、缶本体6部材結合。取り外しキャップは別ファイル | 納品は48パネル・最大偏差0.138945mmの製作モデルと型紙。細部を含む53部材結合 | 理想/近似ともSTL・STEP・履歴再読込完了。STL参照断面の近似最大偏差0.131055mm、肉厚0.800186～0.805677mm。各355層に押出あり。96面の別試験は実行中で、納品完成モデルとは区別 |
| `tank` | 1:100、81個別部材を保存 | 4製造モデル256パネル、4組の型紙。最大偏差0.129338mm | 個別STLとネイティブは保存。理想の最終結合は体積精度で拒否。近似は最初の板厚付けで安全検査が拒否し、終了コード1を確認。最終結合は未達 |
| `vase` | 1:2、高さ165mmの参照曲線、壁と底板の結合 | 32帯と底板、2ページの型紙。最大近似偏差0.194916mm。STL独立測定0.193562mm | 理想/近似ともSTL・STEP・履歴再読込完了。底面の微小張り出しあり。近似の独立版は同じ33部材の検証済み診断結合に由来 |
| `free-oloid` | Keenan Craneの無料CC0原モデル（OBJ） | 正規化した80mmのSTLを別途用意 | 閉鎖性・方向・交差・266層のスライス確認。ネイティブCADへの取り込み/展開完了とは扱わない |

平らな底板、車輪、手すり、ノズル等は形状に応じて理想ソリッドを保持する。無意味に曲面近似へ分割しない。
壺の `editable-panels.kcd2` は元の面生成・板厚付けまでの編集履歴。
`vase/approximation/assembly-history.kcd2` は33個の独立部材から結合した履歴であり、元曲線への全依存を含む版とは区別する。
各ファイルの元の場所とSHA256は ZIP 内の `manifest.json` を参照。

## このPCでのビルドと試験

使用した環境: Windows x64、Visual Studio 2022/MSVC、Qt 6.9.2、Open CASCADE 8.0.1、CMake。
検出された依存パスはローカルの `CMakeCache.txt` に記録される。
既存の環境検出は `cmake/KachakachaEnvironment.cmake` と `scripts/doctor.ps1` に従う。

```powershell
# 展開したソース、または専用クローンのルートで実行する
.\scripts\doctor.ps1
cmake --preset windows-msvc
cmake --build --preset windows-msvc --parallel 2
ctest --preset windows-msvc --parallel 1 --output-on-failure
```

本体は `build-msvc2022-x64\Release\kachakacha_cad_next.exe`。
上記のCTestには通常GUIと配布ZIPのGUI起動も含まれる。重いCADを同時に多数実行すると試験の時間制限に影響する。
ビルド対象のEXEや配布フォルダー内のEXEが動いている間は、そのファイルを再リンク・上書きしない。

実用試験は可視Qtアプリのコマンド、設定カード、ダイアログ、取消、保存再読込を実行する。
選択IDや入力フィクスチャを自動設定する部分があるため、すべてをマウスで手操作した試験とは説明しない。

```powershell
$env:KACHACAD_PRACTICAL_STAGE='pattern'
$env:KACHACAD_PRACTICAL_REFERENCE_SCALE='150'
python tools\run_practical.py --fixture 'C:\path\approximation.kcd2' --output 'C:\path\new-results'
```

出力先は毎回新しくする。実行ファイルと元データのSHA256、設定、開始終了時刻、終了コード、元データ不変確認を保存する。
`freeze-sweep` 工程は0/30/50/100%と「部品のみ／線と部品」を確認する。既存の工程は `ideal`、`approximation`、`assembly`、`export`、`inspect`。

## 検査結果の読み方

`practical-issues-20261011.md` は原因・修正・残件の一覧。
各モデルのJSONにはSTL再読込の閉鎖性、方向、交差、寸法、断面偏差を記録する。
断面偏差は写真から作った参照曲線/断面への数値差であり、実物の工場図面への精度保証ではない。
三角形交差は数値検査で、数学的な完全証明ではない。法線レイの肉厚は角や隣の壁の影響を受けるので、専用断面検査と区別する。

スライスはローカルBambu StudioのA1/0.4mm/PLA/0.2mmという再現用仮定。利用者のプリンタを推定していない。
G-codeの各層に押出しがあるかまで検査したが、実機での造形・強度・サポート取り外しは未検証。プリンタ送信は行っていない。

無料比較用OloidはKeenan CraneのCC0モデル。CADネイティブへの取り込み機能を検証したものではなく、独立STL比較対象。
第三者の写真は参照用であり、再配布権が明確でない画像は納品ZIPに含めない。参照URLと利用条件は出典台帳に残す。

## 任意の独立解析を再現する場合

CAD本体の起動にPythonは不要。解析ツールはPython 3.12で実行し、numpy 2.5.4、scipy 1.18.1、trimesh 5.1.1、rtree 1.4.1、Pillow 12.3.0、networkx 3.7を使用した。
検証PCでは作業フォルダーの `analysis-python` に分離配置している。本体への追加依存ではない。

```powershell
# 別環境に解析依存を用意する場合。既存の検証PCでは配置済みのものを使える。
python -m pip install --target analysis-python numpy==2.5.4 scipy==1.18.1 trimesh==5.1.1 rtree==1.4.1 Pillow==12.3.0 networkx==3.7
$env:PYTHONPATH=(Resolve-Path analysis-python).Path
python tools/inspect_print_mesh.py path/to/assembly.stl --output path/to/topology.json
python tools/inspect_mesh_crossings.py path/to/assembly.stl --output path/to/crossings.json
```

解析結果JSONの出力先フォルダーは先に作成する。専用の断面比較は `compare_can_sections.py`、`compare_vase_sections.py` 等を用い、元の参照モデルと対応するSTLを指定する。
