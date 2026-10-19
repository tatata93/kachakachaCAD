> 最新の全機能版は [画像付きHTMLマニュアル](../manual.html) です。以下は補助資料です。

# kachakachaCAD 取扱説明書(V2)

プラ板と紙で鉄道模型を作るための CAD です。
この説明書は、実際の画面を撮ったものと一緒に、全部の機能を順に説明します。

画面の図は `kachakacha_cad_next.exe --manual-state <名前> --snapshot <png>` または
画面操作の自己試験で撮影しています。撮影方法は各図の節に記載しています。
図を作り直す手順は「図の作り直し」にあります。

---

> **部品モードのボタンが分からないなら [部品モードのボタン](部品モードのボタン.md)。**
> 全部の役割と使用例、選ぶ順が意味を持つものを並べてあります。
>
> **立方体を作りたいなら [立方体を作る](立方体を作る.md)。**
> 「12本の辺を引いてかごから作る」と「正方形を押し出す」の2通りを並べてあります。
>
> **はじめて使うなら、まず [はじめての1枚](はじめての1枚.md) を通してください。**
> 15分で、窓のある側板を1枚、原寸の型紙にするところまで行きます。
> この説明書は全部を書いてあるので、通し方を知ってから読むほうが早いです。

## 目次

正面と側面の2枚から窓を投影する手順は、[パノラミック窓の画像付き手順書](panorama-projection.html)にあります。
下記は同手順のWindows自動UI試験で撮影した画像です。

![2枚の作図面と車体](images/v2-panorama-01-two-planes.png)
![正面側の投影方向](images/v2-panorama-02-front-direction.png)
![側面側の投影方向](images/v2-panorama-03-side-direction.png)
![3面へ投影したプレビュー](images/v2-panorama-04-preview.png)
![閉じた窓外周の生成結果](images/v2-panorama-05-result.png)

1. [はじめに](#1-はじめに)
2. [画面の見かた](#2-画面の見かた)
3. [5つのモード](#3-5つのモード)
4. [作図](#4-作図)
5. [吸着とグリッド](#5-吸着とグリッド)
6. [数値で入れる](#6-数値で入れる)
7. [選ぶ](#7-選ぶ)
8. [視点](#8-視点)
9. [まとまり(グループ)](#9-まとまりグループ)
10. [形をつくる(部品モード)](#10-形をつくる部品モード)
11. [製作モード](#11-製作モード)
12. [書き出し](#12-書き出し)
13. [手順の帯](#13-手順の帯)
14. [見た目を変える](#14-見た目を変える)
15. [困ったとき](#15-困ったとき)
16. [見本の文書](#16-見本の文書)
17. [図の作り直し](#17-図の作り直し)

作る手順を順に追いたいときは、`docs/manual/作り方.md` を読んでください。
押すボタンと入れる数字、そして各工程の思想が書いてあります。

---

## 1. はじめに

### できること

- 紙とプラ板から作る部材の形を決める。
- 曲がった車体を、平らな板から作れる形に割る。
- 1:1 の型紙(PDF / SVG / DXF)と、立体(STEP / STL)を出す。

### この道具の約束

**できないことを、できたことにしません。**

たとえば、線がつながっていなくて面が張れないとき、
勝手に近い点をくっつけて「できました」とは言いません。
どこが、どれだけ離れているかを番号つきで言って、断ります。

画面に出る知らせは、すべて `GEO-W001` のような番号を持っています。
この番号は文面が変わっても変わらないので、困ったときはこの番号で探せます。

### 動かすもの

Windows。配布の zip を好きなところへ展開して、`kachakacha_cad_next.exe` を起動します。
入れる作業(インストール)は要りません。

zip の中身は次のとおりです。

| 場所 | 中身 |
| --- | --- |
| `kachakacha_cad_next.exe` | 本体 |
| `samples\v2-sample.kcd2` | 見本の文書(16章) |
| `samples\streamlined-railway-nose-1-87.kcd2` | 流線形鉄道車両前面の1/87実用試験 |
| `manual.html` | ブラウザーで読む全機能マニュアル |
| `manual\` | この説明書と図 |
| `legal\` | 使っている第三者のソフトのライセンス表示 |

版を確かめるには `kachakacha_cad_next.exe --version` と打ちます。

組み立てたばかりの本体は、次の場所にあります。

```
C:\Users\tak01\github\kachakachaCAD\build-msvc2022-x64\Release\kachakacha_cad_next.exe
```

配布のひとまとめは `scripts\package-v2.ps1` で作ります。
作ったあと、そのひとまとめの exe を実際に動かして
「自己試験が通る」「見本が開ける」「開いた絵が撮れる」ところまで確かめます。
通らなければ zip にしません。

---

## 2. 画面の見かた

![起動した直後](images/v2-empty.png)

| 場所 | 名前 | すること |
| --- | --- | --- |
| いちばん上 | メニュー | すべての操作がここにあります。 |
| その下 | モードの帯 | 作図・部品・製作・出力を切り替えます。 |
| その下 | 道具箱 | いま使えるは道具だけが並びます。 |
| 左 | 作ったもの | 文書に入っているものの一覧です。 |
| 中央 | 画面 | ここに描きます。 |
| 右 | 手順 / 形状ガイドの役割 / 書き出し | いまのモードに合わせて出ます。 |
| 下 | 知らせ | 断られた理由が番号つきで並びます。 |
| いちばん下 | 帯 | いまの道具・まとまり・次にすることが出ます。 |

道具箱もメニューもショートカットも、**同じ1本の道**を通ります。
メニューから押しても、道具箱から押しても、同じことが起きます。

---

## 3. 5つのモード

上の帯で切り替えます。**モードを変えても、選んでいるものと文書は変わりません。**
変わるのは「出てくる道具」だけです。

| モード | 何をするところ |
| --- | --- |
| 作図 | 線を引く。寸法を決める。 |
| 部品 | 線から面と立体を作る。 |
| 製作 | 立体を、板から作れる部材へ割る。 |
| 出力 | 型紙と立体を出す。 |

![部品モード](images/v2-mode-part.png)

![製作モード](images/v2-mode-fabrication.png)

![出力モード](images/v2-mode-output.png)

---

## 4. 作図

道具箱から道具を選び、画面を押していきます。

![道具を選んだところ](images/v2-tools.png)

| 道具 | ショートカット | 置く点 |
| --- | --- | --- |
| 点 | — | 1点 |
| 直線 | — | 2点 |
| 連続線 | — | 何点でも(右クリックで終わり) |
| 矩形 | — | 対角の2点 |
| 円 | — | 中心と半径の点 |
| 円弧 | — | 3点 |
| ベジェ | — | 4点 |
| B-spline | — | 4点以上 |

![直線を1本引いたところ](images/v2-draw-line.png)

- 途中でやめるときは `Esc`。置いた点は捨てられ、文書は変わりません。
- 1つ前の点だけ消すときは `Backspace`。
- 元に戻すのは `Ctrl+Z`、やり直すのは `Ctrl+Y`。

### 曲線は曲線のまま

円弧を折れ線にしません。円は円のまま、B-spline は B-spline のまま持ちます。
型紙に出すときも、SVG は円弧コマンド、DXF は `ARC` / `SPLINE` で出ます。

![5種類の曲線](images/v2-curves.png)

---

## 5. 吸着とグリッド

近くに端点・中点・交点があると、そこへ吸着します。
何に吸着したかは画面に名前で出ます。

![端点へ吸着したところ](images/v2-snap.png)

![グリッド](images/v2-grid.png)

- 吸着を一時的に止めるには `S` を押しながら動かします。
- グリッドの間隔は「基準」メニューから変えられます。
- 副点(細かい目盛)は、画面上で 6px より狭くなると自動で消えます。
  出したままにすると、点が多すぎてどれに吸着したのか分からなくなるためです。

---

## 6. 数値で入れる

道具を使っている途中で数字を打つと、カーソルの横に入力欄が出ます。

![数値入力](images/v2-cursor-input.png)

- 道具ごとに「主要な欄」が1つ決まっていて、そこへ自動で焦点が合います。
- `Tab` で次の欄、`Shift+Tab` で前の欄へ移ります。
- `Enter` で確定、`Esc` でやめます。
- 式が書けます。`(180/2)*3` と打つと `(180/2)*3 = 270 mm` と出ます。
  全角で打っても通ります。
- **確定した欄はマウスで動きません。** 打っている途中の欄も動きません。
  動くのは、まだ触っていない欄だけです。

入れた値どうしが合わないときは、最後に変えた欄だけを確定しません。
`UI-C004 入れた値どうしが合いません。` と出ます。

---

## 7. 選ぶ

道具箱の「選択」を選び、線を押します。

![2本選んだところ](images/v2-select.png)

| 押しかた | すること |
| --- | --- |
| そのまま押す | それだけを選ぶ(前の選択は消える) |
| `Shift` を押しながら | 足す |
| `Ctrl` を押しながら | 入っていれば外し、無ければ足す |
| `Alt` を押しながら | 外す |

- 何も無いところをそのまま押すと、選択が空になります。
  足している途中(`Shift` など)に外を押しても、選択は消えません。
- **選んだ順は変わりません。** 形状ガイドの役割は選んだ順で決まるためです。
- 消したものは、選択からも自動で外れます。

---

## 8. 視点

右上のビューキューブを押すと、その面へ正対します。

![ビューキューブ](images/v2-view-cube.png)

![斜めから見たところ](images/v2-isometric.png)

- キューブは面 6・辺 12・角 8 の 26 か所を押し分けられます。
- ドラッグすると回ります。**離した瞬間に近い向きへ吸い付いたりしません。**
  89度まで回しても、勝手に90度へ寄せません。
- 「全体表示」(`F`)で、描いたもの全部が入る大きさにします。
- 視点を変えても、文書は変わりません。

### 視点の操作板

キューブのまわりに、2系統の操作が並びます。

**1. 回転リング(視点に追従)** — キューブを囲む3本の色付きの輪です。

| 輪 | 何のまわりに回すか |
| --- | --- |
| 赤 | モデルの X 軸 |
| 緑 | モデルの Y 軸 |
| 青 | モデルの Z 軸 |

輪はキューブと同じ写し方で描くので、視点を変えると一緒に傾きます。
**どの軸で回るのかが、見たままで分かります。**

**輪は線のどこを押しても掴めます**(線から5px以内)。矢じりだけを狙う必要はありません。
押した場所に近いほうの矢じりの向きへ回ります。

**2. 画面基準の矢印(位置は固定)** — 視点を変えても動きません。

| 場所 | すること |
| --- | --- |
| 下の2つ | 視点を左へ・右へ回す |
| 右の2つ | 視点を上へ・下へ回す |
| 上の2つ | 画面のまま回す(ロール) |

このほかに、左上の **家**(等角ビューへ戻す)と、下の **「選択に正対」** があります。

- どれも押すと **15度** 回ります。90度へは飛びません。
- 押したまま引きずると、続けて回ります。上下の矢印は縦の動き、ほかは横の動きで回します。
- `Shift` を押しながらで細かく(1pxあたり0.05度)、`Ctrl` を押しながらで粗く(1度)。
- 押す順は「画面基準の矢印 → キューブ → 輪」です。
  輪を先に見ると、キューブの面が輪の線に隠れて押せなくなるためです。

## 9. まとまり(グループ)

作ったものは「作業中のまとまり」へ入ります。いまどれが作業中かは、下の帯に出ます。

![まとまり](images/v2-active-group.png)

- 作業中のまとまりを変えても、既にあるものは動きません。
- 派生してできたもの(型紙など)は、元の下の派生まとまりへ入ります。
  作業中を切り替えても、そこからは動きません。

---

## 10. 形をつくる(部品モード)

線から面を作り、面から立体を作ります。

### 形状ガイド

複数の線を「役割」に割り当てて、面を作ります。

![形状ガイドの役割テーブル](images/v2-guide-table.png)

「面作成 → 面にする」で線を選び、区画ごとに作成対象と張り方を決めます。
中央断面と外周から丸い屋根を作る場合は「滑らかなドーム（近似）」を選びます。
不要な区画の「作る」を外し、下見を確認してEnterで確定します。
張り方は区画ごとに設定します。左右をそろえるときは「この張り方を作る曲面すべてに適用」を押します。
平面・ロフトと「作る」を外した区画は変えません。異なる設定が残っていると棚の状態に表示します。
標準・平坦優先・丸み優先にも切り替えられます。内部ガイドや支持面G1/G2には標準などを使います。

![右区画だけの滑らかなドームの下見](images/v2-dome-preview.png)

![右区画だけのドームを確定した結果](images/v2-dome-result.png)

115モデルのWindows自動試験の実画面です。左区画と元の線は変更しません。

![両区画へ同じ張り方を一括適用したドーム](images/v2-dome-both.png)

左右を同じドームにして確定した結果です。実位置で断面を対応させ、裾角付近の余計な盛り上がりを抑えます。

- 表の行の色は、画面の中の線の色と同じです。
  どの線がどの役割なのかを、表と画面の両方で同時に見られます。
- 役割が足りないときは、**どの役割が足りないか**を書いて断ります。
  勝手に「近い線」を割り当てません。
- 行は入れ替えられます。方向も反転できます。

### 押し出し

輪郭と厚みを決めて立体にします。板の厚みは外側・中央・内側のどれに付けるか選べます。

---

## 11. 製作モード

立体を、板から作れる部材へ割ります。

近似の通常欄は枚数・許容ずれ・方向・間隔の自動調整です。方式、候補比較、手動境界や最小幅は折り畳みます。元の実曲面を評価し、出力面も下見と同じレール対応を使います。製作GPT近似の新規入口は廃止しました。既存ファイルの読込・生成とGPT版面作製は維持します。

- 二重に曲がっているところには、切れ目(relief)を入れます。
  向きは、曲がりの強い方向から決めます。
- 組み立ての進み具合(0% / 30% / 100%)を変えられます。
  **どの状態でも、辺の長さは変わりません。** 変わるのは形だけです。
- 型紙にする前に検査します。通らなければ出しません。

---

## 12. 書き出し

右の「書き出し」から出します。

![書き出し](images/v2-export.png)

### 何を出すか

| 対象 | 出せる形式 |
| --- | --- |
| 表示している部品 | STL / STEP |
| 選んだ部品 | STL / STEP |
| 選んだ製作部材 | STL / STEP / SVG / DXF / 1:1 PDF |
| いまの型紙 | SVG / DXF / 1:1 PDF |
| 選んだワイヤー | SVG / DXF / 1:1 PDF |
| 選んだものだけの文書 | kcd2 |
| この文書 | kcd2 |

- 数が 0 のものも一覧から消しません。消すと「選んでいない」のか
  「そもそも無い」のかが分からなくなるためです。薄く出して、選べなくします。
- 対象を変えても、いまの形式がその対象でも使えるなら、形式は変わりません。
- 出せない組合せは、押す前に断ります(`EXP-017`)。
- 出す先を決めていなければ押せません(`EXP-016`)。
- 同じ名前のファイルがあれば、「上書きしてよい」を押すまで出しません(`EXP-018`)。
- 出す先を変えると、上書きの承諾は取り消されます。別のファイルへの承諾ではないためです。

### 出すときの約束

- 検査に通ってから、はじめて中身を作ります。
  先に作ると、断られたときの計算が丸ごと無駄になるためです。
- **0バイトのファイルを残しません。** 中身が空なら、ファイルを作らずに断ります。
- 書いている途中で失敗しても、前のファイルは読める形で残ります。
- 型紙は実寸です。紙に入らないからといって縮めません。
- 同じ入力からは同じバイト列が出ます。版管理に載せられます。

---

## 13. 手順の帯

右の「手順」に、いまのモードでやることが番号順に並びます。

![部品モードの手順](images/v2-steps-part.png)

![製作モードの手順](images/v2-steps-fabrication.png)

![出力モードの手順](images/v2-steps-output.png)

- 前の段が済むまで、先の段には入れません。
- 入れない段には、**なぜ入れないか**が書いてあります。

---

## 14. 見た目を変える

「見た目」メニューから、通常と Windows 95 風を切り替えられます。

![Windows 95 風](images/v2-win95.png)

線の見え方も、見た目によって変わります。曲線は曲線のままです。

![Windows 95 風で曲線を描いたところ](images/v2-curves-win95.png)

どちらでも、画面の部品ははみ出しません。
1366x768 でも 1920x1080 でも、拡大率 100% / 125% / 150% / 200% でも同じです。

---

## 15. 困ったとき

### 知らせの番号

下の「知らせ」に出る `GEO-W001` のような番号は、文面が変わっても変わりません。
番号の一覧は `docs/v2/diagnostic-catalog.md` にあります。よく出るものだけ挙げます。

| 番号 | 意味 | どうするか |
| --- | --- | --- |
| `GEO-W001` | となりあう線がつながっていません。 | 端点を吸着させてつなぎ直します。 |
| `GEO-W003` | 端点がぴったり合っていません。 | どれだけ離れているかが本文に出ます。 |
| `GEO-W004` | ワイヤーが自分自身と交わっています。 | 交わっているところを直します。 |
| `UI-R009` | 面を作るための行がそろっていません。 | 足りない役割が本文に出ます。 |
| `EXP-015` | 出すものが選ばれていません。 | 先に選びます。表示・非表示では決まりません。 |
| `EXP-016` | 書き出す先が決まっていません。 | 「出す先を選ぶ…」で決めます。 |
| `EXP-017` | その形式では出せません。 | 一覧で選べる形式に変えます。 |
| `EXP-018` | 同じ名前のファイルが既にあります。 | 「上書きしてよい」を押すか、別の名前にします。 |

### 重い操作

100ms を超える計算には、途中でやめる道が出ます。
やめても文書は変わりません。

### 落ちたとき

保存は、書いている途中で失敗しても前のファイルが読める形で行います。
`.kcd2` の隣に `.bak` が残っていることがあります。これが直前のものです。

---

## 16. 見本の文書

`samples/v2-sample.kcd2` を開くと、1/87 の車体側面が入っています。

| 中身 | 数 |
| --- | --- |
| 側面の外形(実物 20000mm x 3000mm を 1/87) | 1 |
| 腰の高さ(補助線) | 1 |
| 窓 | 6 |
| 前照灯(円) | 1 |

この見本で、この説明書の 4・5・7・8・12 章がそのまま試せます。

「ファイル」→「開く」から開きます。画面を出さずに開いて確かめるには、

```
kachakacha_cad_next.exe --open samples\v2-sample.kcd2
```

と打ちます。`--snapshot` を足すと、開いた絵だけを撮って終わります。

見本を作り直すには次のように打ちます。

```
kachakacha_v2_write_sample samples/v2-sample.kcd2
```

見本を直して置き直し忘れると `v2_sample_document_tests` が落ちます。
説明書の図と中身がずれないようにするためです。

### 流線形前頭部の実用試験

`samples/streamlined-railway-nose-1-87.kcd2` には、ER1 / 初期ER2の丸形前頭部を
参考にした1/87の**試験用近似形状**が入っています。資料値として使ったのは車体幅
3520 mmだけです。奥行き、曲率、窓寸法は実車寸法ではありません。

![流線形前頭部のロフト、6枚窓、前照灯](images/v2-railway-nose.png)

見本には、水平断面7本、それらを通るロフト面、厚さ0.20 mmの外板、曲線で作った
6枚の前面窓と中央上部前照灯、最大24部材の帯近似が入っています。自動試験では、
ロフト面が全断面を通ること、厚み付き外板に体積があること、窓と灯具が型紙へ残る
こと、保存後に読み直せることを実際の幾何カーネルで確かめています。

```
kachakacha_cad_next.exe --open samples\streamlined-railway-nose-1-87.kcd2
kachakacha_v2_write_sample --railway-nose samples\streamlined-railway-nose-1-87.kcd2
```

---

## 17. 図の作り直し

図はすべて、次の形で撮ったものです。

```
kachakacha_cad_next.exe --manual-state <名前> --snapshot docs\manual\images\v2-<名前>.png
```

`<名前>` に使えるものは
`empty` `grid` `tools` `curves` `curves-win95` `draw-line` `snap` `isometric`
`win95` `guide` `mode-part` `mode-fabrication` `mode-output` `view-cube`
`guide-table` `cursor-input` `active-group` `steps-part` `steps-fabrication`
`steps-output` `select` `export` `sample` `railway-nose` です。

画面の大きさを決めるには `--size 1366x768` を足します。

画面を出さずに一通り触って確かめるには `--self-test` と打ちます。

`panorama-01-two-planes`、`panorama-02-front-direction`、`panorama-03-side-direction`、
`panorama-04-preview`、`panorama-05-result` は
`PROJ panorama two workplanes per-row directions` 自己試験のWindows実画面です。
`KACHACAD_PANORAMA_GUIDE_OUTPUT` に出力先を指定し、同ケースを実行すると
`01-two-planes.png`〜`05-result.png` と入力・結果のKCD2を保存します。
マニュアルへ登録するときは画像名の先頭に `v2-panorama-` を付けます。
これらは `--manual-state` の引数ではありません。

`dome-preview` と `dome-result` は `HP-LF-115-DOME` 自己試験で保存したWindows実画面です。
`dome-both` は `HP-LF-115-DOME-BOTH` の両区画一括設定・確定後の実画面です。
入力は `KACHACAD_SURFACE_TEST_FILE`、保存先の接頭辞は `KACHACAD_SURFACE_TEST_OUTPUT` で指定します。
`--manual-state` の引数ではありません。元モデルは上書きしません。

## 2026-10-05 追加の図

説明書モードを含む5モードの実アプリ画面です。



![v2-instructions-3d](images/v2-instructions-3d.png)

![v2-ui-approx-candidates](images/v2-ui-approx-candidates.png)

![v2-ui-boolean-preview](images/v2-ui-boolean-preview.png)

![v2-ui-explorer-groups](images/v2-ui-explorer-groups.png)

![v2-ui-extrude-profile-only](images/v2-ui-extrude-profile-only.png)

![v2-ui-fab-generate](images/v2-ui-fab-generate.png)

![v2-ui-guided-loft-preview](images/v2-ui-guided-loft-preview.png)

![v2-ui-measure-overlay](images/v2-ui-measure-overlay.png)

![v2-ui-surface-preview](images/v2-ui-surface-preview.png)

![v2-ui-tool-area](images/v2-ui-tool-area.png)

![v2-ui-tool-fillet](images/v2-ui-tool-fillet.png)

![v2-ui-tool-revolve](images/v2-ui-tool-revolve.png)

![v2-ui-tool-shell](images/v2-ui-tool-shell.png)

![v2-ui-tool-split](images/v2-ui-tool-split.png)


追加の撮影状態: `ui-approx-candidates`, `ui-boolean-preview`, `ui-explorer-groups`, `ui-extrude-profile-only`, `ui-fab-generate`, `ui-guided-loft-preview`, `ui-measure-overlay`, `ui-surface-preview`, `ui-tool-area`, `ui-tool-fillet`, `ui-tool-revolve`, `ui-tool-shell`, `ui-tool-split`。

`instructions-3d` は HP-INSTRUCTIONS 自己試験で保存したWindows実画面です。manual-state の引数ではありません。

画像フィットは下記の操作画面、自動分割は `approx-outline-preview` の実操作画面を参照します。

## 2026-10-10 組み立て説明書の二つのエディター

上のモード帯で「説明書」を選び、「画像エディター / 用紙編集」で切り替えます。

### 画像エディターで場面を作る

1. 「現在のモデルを選ぶ」または「別のモデルを読込」で使う部品・面を選び、3Dビューで確認して配置します。
2. 「選択・移動」で部品の位置と視点を調整します。「矢印」は始点から先端へドラッグして配置し、選択後に両端の丸・四角で調整できます。
3. 「番号・文章」でフォント・文字サイズ・太字・斜体を指定します。場面は追加・複製して別々に編集でき、各場面を対応画像形式で出力できます。

![画像エディターで3D場面と矢印を編集](images/v2-instructions-scene-editor.png)

### 用紙編集でページを作る

1. 「用紙サイズ」でA4/A3/A5/Letter/自由サイズと縦横を設定します。
2. 「場面を配置」で場面を選び、用紙をクリックします。図は3D場面の参照を保持し、場面の編集が反映されます。「選択・移動」で図をダブルクリックすると元の場面を編集できます。
3. 「画像を貼る」でPNG/JPEGを配置し、「番号・文章」で書式付きの説明文を入れます。「選択・移動」で位置を変え、右下ハンドルまたは右ペインの数値でサイズを変えます。Shiftで縦横比を保持します。
4. 説明書を .kci に保存します。各用紙を画像、全用紙を指定した紙寸法のPDFで出力できます。

![用紙編集で3D場面参照・画像・文章を配置](images/v2-instructions-layout.png)

`instructions-scene-editor` と `instructions-layout` は `HP-INSTRUCTIONS-SHEETS` 自己試験で保存したWindows実画面です。
`--manual-state` の引数ではありません。図の3Dデータは表示形状のスナップショットであり、元CAD正本への自動追従ではありません。
詳細は [画像付きHTMLマニュアルの組み立て説明書](../manual.html#assembly-guide) を参照してください。

## 元の面に沿う近似の実画面（2026-10-11）

通常近似の枚数・許容ずれ・分割方向・間隔自動調整を表示し、詳細を折り畳んだ下見。

![自動間隔12枚の近似下見](images/v2-approx-outline-preview.png)

ワイヤー48本と近似面12枚を現在状態100%で生成した結果。元ファイルを上書きせず別名保存と再読込を検査しました。

![外周に沿う近似面を生成した実画面](images/v2-approx-outline-result.png)

HP-AP-OUTLINE-115 のWindows自動試験画像です。等間隔/自動間隔と5方向を検査。選択面・12枚・縦方向・自動間隔では独立標本の最大偏差0.094534 mm、RMS0.031028 mm（全域保証ではありません）。

`approx-outline-preview` と `approx-outline-result` はこの自己試験で撮影しています。`--manual-state` の引数ではありません。

![12部材の独立した等長展開を原寸で配置した型紙](images/v2-approx-outline-pattern.png)

`approx-outline-pattern` は同じ自己試験で展開ボタンの経路を通して撮影。5方向×両間隔の型紙で辺長を検査し、型紙の寸法と曲げ角からの計算上の再組立・継ぎ目は0.000001 mm未満でした。板厚・材料の曲げ癖・接着の実物検証は含みません。


![元面を保持した厚み補正の下見と数値結果](images/v2-thicken-corrected.png)

`thicken-corrected` は HP-TH-CORRECT-OWNER の配布版Windows自動操作で撮影。内側0.5 mm、反対面を補正を選び、下見・確定・Undo/Redo・保存再生成を検査した画面です。対応点の法線距離・位置偏差は標本結果で、厳密な一定厚みの保証ではありません。`--manual-state` の引数ではありません。

## 曲面への投影（2026-10-13）

作図 → 編集 → 面へ投影から、未選択でもツールを開始できます。元ワイヤーと投影先の面を
別々の欄へ追加し、方向投影・等長巻き付け・最近点を切り替え、3D下見を確認してEnterで確定します。
ソリッドのフェイスと複数面に対応。正面・側面の入力行ごとに方向を指定でき、方向反転は1ボタンです。
巻き付けは1面の平面・円柱・円錐に対応し、基準点と向きを3Dビューで決めます。

![ソリッドのフェイスへの投影を3Dで下見](images/v2-curved-projection.png)

![115系の窓を3枚のフェイスへ投影し両ペインで入力を色分け](images/v2-window-projection-115.png)

`window-projection-115` は添付モデルのPROJ自己試験で撮影したWindows実画面です。
`--manual-state` の引数ではありません。

投影元は紫の「元」、投影先は緑の「先」として、3D・左ペイン・右ペインで同じ役割を示します。
①／②の押下中の役割へ直接追加します。方向投影の「面＋ワイヤー」は閉じた平面輪郭を
選択したすべてのフェイスへ投影し、元の曲率と穴を維持します。詳細に許容偏差・最大距離があります。

`curved-projection` はPROJ自己試験のWindows通常描画で撮影した実画面で、`--manual-state` の引数ではありません。
Escは入力だけを取消してツールと設定を維持します。元形状は残り、出力は投影時点の派生ワイヤーです。
直接編集する場合は現在状態を固定します。詳しい操作は[画像付きHTMLマニュアル](../manual.html#curved-wire-projection)を参照してください。

## 投影したライトと扉の押し出し（2026-10-14）

![全周を保持したライトと0.5mm押し出した扉・窓](images/v2-lamp-projection-115.png)

`lamp-projection-115` は添付モデルの任意PROJ自己試験で撮影するWindows実画面です。
`--manual-state` の引数ではありません。平面へ投影できる元の曲線形式と閉鎖性を保持し、
不要な直線・円弧の分割をまとめます。非平面の閉輪郭は曲面押し出しを使用します。
面の範囲外・穴・深さ差で開いた結果は、プレビューと確定後の両方で案内します。

## 窓の形にトリムして境界ワイヤーを残す（2026-10-16）

[画像付きの操作手順](solid-trim.html)。部品の「めり込みを削る」「交わりで分割」は同じ右ペインで
方式を切り替えます。対象と切断要素を別々の欄へ選び、「境界ワイヤーも作る」で通常ワイヤーを残せます。
面・サーフェス・ソリッド・ワイヤーを加工でき、点・平面は切断位置にも使えます。
フェイスだけの指定は独立した面を生成します。点自体には削る区画はありません。

![車体と窓輪郭を用意](images/v2-trim-window-01-input.png)
![閉輪郭でソリッドを分割](images/v2-trim-window-02-regions.png)
![窓の中身を外して車体を残す](images/v2-trim-window-03-keep-body.png)
![再読込した穴付きソリッドと切断境界ワイヤー](images/v2-trim-window-04-result.png)
![曲面上の外周と窓輪郭から作った穴付き面](images/v2-trim-curved-window-hole.png)

`trim-window-01-input`、`trim-window-02-regions`、`trim-window-03-keep-body`、
`trim-window-04-result`、`trim-curved-window-hole` はWindowsの操作試験で取得した実画面です。
`--manual-state` の引数ではありません。
窓輪郭を指定方向に貫通させ、残す区画を一覧か3Dビューで選択して確定します。
曲面の外周と窓輪郭から穴付き面も作れます。加工と境界生成は1回のUndoで戻せます。

## 断面表示・削除確認・独立した厚み部品（2026-10-17）

[画像付きの手順](section-and-delete.html)。作業面で切る側と表示専用オフセットを指定し、グリッドを手前に重ねられます。元モデルの形は変えません。参照先のある削除は一覧と3Dの赤色表示を確認して確定し、1回のUndoで戻せます。厚みの「元の面に依存させない」は作成時点の独立部品を保存します。

![115面の断面表示とグリッド](images/v2-section-115.png)
![削除される参照先と寸法の一覧](images/v2-delete-confirm.png)

画像はWindowsの操作試験で取得。115面.kcd2の元ファイルは変更していません。

![削除される形状の3D強調表示](images/v2-delete-preview.png)
![グリッドを手前に表示](images/v2-section-grid.png)

`section-115`、`section-grid`、`delete-confirm`、`delete-preview` は実操作試験の画像名で、`--manual-state` の引数ではありません。

## 全ツールの図付き具体例

入口は `../manual.html` の全コマンド一覧です。各項目から `tools/<command-id>.html` の準備、番号手順、設定、期待結果、取消、失敗時の案内へ進めます。共有ID内の方式は別の具体例として掲載しています。

- 編集元: `tool_recipes.py`（コマンド別）、`tool_variants.py`（方式別）、`chapters.html`、`illustrated-workflows.html`、`command-notes.json`。
- 生成: `python docs/manual/build_manual.py`。整合確認: 同コマンドに `--check`。
- 説明用の模式図は廃止し、実アプリ画面を使います。`tool-icons/` は製品の実際の操作アイコンです。
- `tool-screens/` はWindows UI試験で取得した画面です。`tool-evidence.json` が試験名・時刻・実行ファイルのSHA256を保持します。コマンド直後の入力待ちや下見も含み、確定形状の証拠と混同しません。
- `tool-test-coverage.json` でコマンドごとの呼出しケースを確認できます。全コマンド到達試験と、有効な幾何入力を用いた機能試験は別です。

未対応の方式は成功例にせず、制限と代替操作を明記します。保存形式のIDと、ユーザー向けの表示名を混同しないでください。


## 準備から完成までの詳しい手順

[作業手順から探す](learn/index.html)。各項目は、元図形を作る練習へのリンク、実際のリボン入口と検索、選択する役割、欄ごとの数値、実行・確定の違い、失敗時の戻り先を持ちます。
[ライトを中央面まで伸ばす](headlight-to-plane.html)も配布マニュアルへ移しました。
編集元は lesson_foundations.py（7つの準備練習）、lesson_details.py（全168項目の個別解説）、lesson_walkthroughs.py（複数役割の段階手順）、lesson_renderer.pyです。短いレシピだけを増やして完了にしません。

![投影ライトの準備](images/v2-headlight-lamp-faces-preview.png)
![中央面を選ぶ](images/v2-headlight-lamp-central-plane-pick.png)
![押し出しの下見](images/v2-headlight-lamp-central-plane-preview.png)
![押し出しの全体結果](images/v2-headlight-lamp-central-plane-result.png)
![ライトの拡大結果](images/v2-headlight-lamp-central-plane-detail.png)

実画面は画像ごとに別例・入力待ち・下見・確定の別を記載しています。模式図を実際のCAD完成形とは扱いません。

`headlight-lamp-faces-preview`、`headlight-lamp-central-plane-pick`、`headlight-lamp-central-plane-preview`、`headlight-lamp-central-plane-result`、`headlight-lamp-central-plane-detail` は2026-10-16の実アプリ記録を同梱した画像名です。`--manual-state` の引数ではありません。

## 2026-10-18 の入口確認

`tool-entry-screens/` は全168コマンドをActivateCommandで開いた対象未選択の実画面です。
対応するactivation.jsonに入力ペイン、待っている対象、状態、時間を記録しています。
入口の検査と、形状生成に成功した実例の画像を区別します。各ページの主手順は準備図形・役割・値・確定を読み合わせてください。
角加工は新しい対象モードと下見／Enter確定へ更新しました。

![通常のWindows起動](images/v2-desktop-startup.png)

[startup.html](startup.html)は配布ZIPの展開から最初の作図までの起動手順です。

`desktop-startup` は通常のWindowsイベントループで起動・表示・描画を確認した画像名です。`--manual-state` の名前ではありません。

## 説明書エディターの再整備（2026-10-18）

[instruction-editor.html](instruction-editor.html) に左のシーン一覧からの用紙配置、場面・用紙・文章・写真・保存・出力・削除の手順をまとめた。
`instruction-editor-scene`、`instruction-editor-paper` は115面.kcd2の表示中要素を取り込んだ実画面、
`instruction-editor-text`、`instruction-editor-paragraph` は注記だけの編集回帰の実画面。
Qtマウス・キーイベントをWindowsアプリへ送り、KCI/PDF出力とUndo・読込まで検査した。
画像エディターのPDFは現在の場面、用紙編集のPDFは全用紙を対象にする。

![説明書エディターの実画面](images/v2-instruction-editor-scene.png)

![説明書エディターの実画面](images/v2-instruction-editor-paper.png)

![説明書エディターの実画面](images/v2-instruction-editor-text.png)

![説明書エディターの実画面](images/v2-instruction-editor-paragraph.png)


## 重なる線と左一覧の操作（2026-10-19）

重なる線の上で右クリックし、候補にマウスを合わせると3Dの強調対象が切り替わります。
クリックで確定します。Tab／Shift+Tabでも候補を切り替えられます。
終端面などの入力中にも同じ候補一覧を使えます。
[詳しい操作](explorer-selection.html)を参照してください。

カテゴリのチェックはその中の項目を一括で表示・非表示にします。
グループ自身のチェックは、中身と子グループの個別設定を保持したまま全体を切り替えます。
左一覧上部の「新規グループ」は、選択したグループと同列に、「子グループ」はその中に作ります。

閉じた車体外周と、その面上の閉じた窓の輪郭を一緒に選んで「面にする」と穴付き面を作れます。
元の面が残るので、結果だけを局所表示して穴を確認してください。
[投影から穴付き面まで](solid-trim.html)に詳しい手順があります。

![115面から作成した中央窓の穴付き面](images/v2-owner-planar-window-hole.png)

![115面の左側曲面から作成した丸いライト穴](images/v2-owner-curved-window-hole.png)

`owner-planar-window-hole` と `owner-curved-window-hole` は、115面.kcd2を入力するWindowsのPROJ OWNER操作試験で取得した実画面です。通常の空の画面サンプルではありません。


## フォルダ・面の選択と表示の実画面（2026-10-19）

`folder-open`: ![フォルダと面の実操作画面](images/v2-folder-open.png)

`folder-delete`: ![フォルダと面の実操作画面](images/v2-folder-delete.png)

`wire-under-face`: ![フォルダと面の実操作画面](images/v2-wire-under-face.png)

`surface-grid-off`: ![フォルダと面の実操作画面](images/v2-surface-grid-off.png)

`surface-grid-all`: ![フォルダと面の実操作画面](images/v2-surface-grid-all.png)

`folder-move-paths`: ![フォルダと面の実操作画面](images/v2-folder-move-paths.png)

WindowsのExplorer操作試験の実Qtウィンドウから取得。OSからの入力注入とは区別する。[操作手順](explorer-selection.html)。


## 2026-10-19 実操作画面への統一

`image-fit-operation` は画像の1点目を選び、2点目へカーソルを移動したWindows実画面です。
`image-fit-settings` は同じ画像フィット試験の設定・下見画面です。
入力待ち・下見・確定後をキャプションで区別し、共通入力画面を各方式の完成結果として扱いません。

![画像側の2点を指定中](images/v2-image-fit-operation.png)

![画像フィット後の設定](images/v2-image-fit-settings.png)


## 2026-10-19 説明書と選択の追加実画面

`instruction-paper-zoom`: ![Windowsの実Qt操作画面](images/v2-instruction-paper-zoom.png)

`instruction-paper-arrange`: ![Windowsの実Qt操作画面](images/v2-instruction-paper-arrange.png)

`overlap-candidates-highlight`: ![Windowsの実Qt操作画面](images/v2-overlap-candidates-highlight.png)

`overlap-candidates-menu`: ![Windowsの実Qt操作画面](images/v2-overlap-candidates-menu.png)

説明書はHP-INSTRUCTIONS操作試験、重なり候補はHP-XP-09で取得。入力イベントを実ウィジェットへ送り、撮影したアプリ画面です。OS入力とは区別します。

`instruction-inline-text`: ![用紙上で文章を直接編集](images/v2-instruction-inline-text.png)

HP-INSTRUCTIONS-EDIT、2026-10-19のWindows実Qtウィンドウ。
