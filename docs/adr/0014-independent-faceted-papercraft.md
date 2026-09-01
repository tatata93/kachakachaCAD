# ADR 0014: 従来展開と独立した多面体ペーパークラフト生成器を持つ

## 背景

従来の曲げ帯方式は、プラ板の連続した曲げ面をなるべく残すための機能である。一方、紙工作として確実に組み立てる用途では、完成形が角ばってもよい代わりに、面を伸縮させない展開図、折り戻せる接続関係、再現度に応じた面数が必要になる。両者は工作上の目的が違うため、一つの生成器へ条件分岐を増やすと比較ができず、修正が相互に影響する。

## 調査

- 三谷純・鈴木宏正の SIGGRAPH 2004 論文は、自由曲面を細長い三角形の帯へ近似し、紙の曲げやすさを残した展開を提案している。大量の三角形をそのまま個別展開するより工作可能な片数にしやすい。[Making Papercraft Toys from Meshes using Strip-based Approximate Unfolding](https://mitani.cs.tsukuba.ac.jp/dl/mitani_2004_siggraph_papercraft.pdf)
- Blender公式マニュアルの Paper Model は、面角度と辺長に切断優先度を与え、展開後の島を生成する。利用者がシームを手で修正できる。[Blender Manual: Paper Model](https://docs.blender.org/manual/en/3.6/addons/import_export/paper_model.html)
- Pepakura Designer は3Dモデルから展開図を作り、3Dと2Dを対応させて編集する専用製品である。一方、公式FAQでは元3Dモデルの作成機能は持たないと説明されている。本CADでは元のワイヤー、面、板をそのまま編集できることを維持する。[Pepakura Designer](https://www.pepakura.tamasoft.co.jp/pepakura_designer/), [FAQ](https://pepakura.tamasoft.co.jp/pepakura_designer/faqs/)
- 多面体の辺展開では、面の双対グラフの全域木が一つの展開方法に対応し、木に含まれない辺が切断辺になる。全候補探索は現実的でないため、重なりを検出しながら木を切り分ける実用的な方法が必要になる。[Mesh Simplification for Unfolding](https://www.cs.columbia.edu/~silviasellan/pdf/papers/simplification-for-unfolding.pdf)
- CGALには元面へ再投影する等方的リメッシュがあるが、本機能の入力はCADの面であり、主編集対象をメッシュへ変えないため、現段階では外部メッシュ処理を導入しない。[CGAL Polygon Mesh Processing](https://doc.cgal.org/latest/Polygon_mesh_processing/index.html)

## 決定

- 新しい `FacetedPapercraft` 生成器を従来の `PlateFlatPattern` 生成器と別ファイル、別APIで実装する。
- UIには「多面体ペーパークラフト（新方式）」と「曲げ帯ペーパークラフト（従来方式）」を並べ、同じ板で比較できるようにする。
- 元の面と板は変更しない。再現度から作った角面、展開片、途中組立モデルはすべて派生オブジェクトとする。
- 再現度1から10を面間隔へ変換する。高い値ほど面数を増やし、元面と三角面の最大距離を小さくする。
- 面は切断方向に沿う細長い帯へまとめる。各帯の隣接面は木構造なので、三角面の辺長を変えずに順番に平面へ開ける。
- 展開した同一片の三角形が面積を持って重なる場合、その部分木の親辺を切断して再展開する。重なりがなくなるまで繰り返す。
- 開口の内側にある角面は生成しない。窓とライト穴は表示線ではなく、展開片、組立途中、完成近似、STL/STEPから材料が実際に除かれた領域になる。
- 切断辺のうち曲がりが設定角度以上の箇所は、許可されている場合だけV字切れ込みへ置換する。直線Vと、左右の切断辺自体が曲がる曲線Vを選べる。
- のりしろはこの段階では生成しない。
- 組立スライダーは各三角面を剛体として親子の共有辺まわりに回す。0%から100%まで辺長を変えず、任意位置を通常の厚み付き板群として保存・STL・STEP出力できる。

## 結果

新方式は、元曲面を正確に平らへ潰したように見せる機能ではない。利用者が選んだ再現度で角ばった別の完成モデルを先に定義し、そのモデルを歪みなく折り戻せる紙片へ変換する。従来方式は連続曲げを優先する比較対象として残る。
