# core と UI の結線の台帳

**この表とソースが食い違うと `tests_v2/core_ui_wiring_tests.cpp` が失敗する。**

## なぜこの表があるか

V2 は「幾何は core、画面は薄く」という作りにしてある。
そのため **core に実装があって試験も通っているのに、画面から一度も呼べない** という
状態が起きやすい。実際そうなっていた ── 面に厚みを付ける `BuildPanelSolids`、
任意の曲げ状態を作る `Assembly`、部材の分け方を決める `PanelStrategy` は、
どれも書けていて試験もあるのに、アプリから到達できなかった。

これは読んでも気づけない。**アプリの .cpp から `#include` をたどって届くか** で機械が測る。
名前で数えると、同じ名前の関数が別のヘッダにもあるときに見落とすので、
到達可能性で測る。

## 決まり

- `src/next/kachakacha/**/*.h` と `src/next_occt/**/*.h` のうち、
  `src/apps/cad_next/*.cpp` から `#include` をたどって **届かない** ものは、
  すべてこの表に載っていなければならない。
- 表に載っているのに **届いてしまう** ものがあってもいけない。
  繋いだら表から消す、を強制するためである。
- 「いつ繋ぐか」を必ず書く。書けないものは、そもそも作るべきではなかったということ。

## いま届いていないもの

| ヘッダ | 何ができるもの | いつ繋ぐか |
| --- | --- | --- |
| `base/SourceScan.h` | ソース木の走査 | **繋がない。**試験の道具である |
| `base/TestHarness.h` | 試験の枠組み | **繋がない。**試験の道具である |
| `app/EvaluationQueue.h` | 重い計算を待たせずに回す仕組み | 計算が待たされるようになってから。いまは同期で足りている |
| `app/PointSources.h` | 距離・接近・曲線上から作図点を作る | 作図点の作り方を増やすとき |
| `document/BrokenReference.h` | 参照切れの見つけ方と直し方 | 開き直しで参照が切れる場面を作ってから |
| `geometry/CurveJoin.h` | 曲線のつなぎ(角の処理) | `wire.join` は別の道(`WireConnect`)を通っている。どちらを残すか決めてから |
| `fabrication/CurvatureAnalysis.h` | 面の曲がり方の分類 | 近似(V2方式)を繋ぐとき |
| `fabrication/PanelStrategy.h` | 部材の分け方(1枚/少数/分割/混合) | 近似(V2方式)を繋ぐとき |
| `fabrication/ReliefCut.h` | 二重曲率を逃がす切れ目 | 近似(V2方式)を繋ぐとき |
| `fabrication/OpeningClip.h` | 部材をまたぐ開口の切り出し | 近似を繋ぐとき |
| `fabrication/ManualRole.h` | 役割の手動割り当て | 近似を繋ぐとき |
| `fabrication/ClosedLoop.h` | 閉じた輪の折り角を解く | 曲げ具合を繋ぐとき |
| `fabrication/Assembly.h` | 曲げ具合(組立率)で形を作る | 曲げ具合を繋ぐとき |
| `fabrication/FreezeState.h` | ある曲げ状態を1回だけ評価して固める | 任意状態の固定を繋ぐとき |
| `fabrication/FreezeMaterialize.h` | 固めたものを文書のものに変える | 任意状態の固定を繋ぐとき |
| `kernel/OcctPanelSolid.h` | 平らな輪郭に厚みを付けて立体にする | 曲げ状態の固定を繋ぐとき。曲がった面の厚み付けは `OcctThicken.h` が受け持つ |

## この門が見ないもの

**カーネル(`src/next_occt`)の引数の型違いは、この門では捕まらない。**
雲側に OCCT が無いので、`KACHACAD_V2_WITH_OCCT` の中は一度も構文検査に通らない。
実際、`FromEdge(edge, tolerance)` と書いて(正しくは `tolerance.modelLinearMm`)、
PC の MSVC で初めて落ちたことがある。

OCCT の宣言だけのスタブを作れば雲でも見られるが、使っているヘッダが78個あり、
本物と食い違えば **第二の真実** になって、かえって当てにならなくなる。
そこで作らない。かわりに **カーネルの変更は小さく刻んで、1往復ずつ PC で確かめる**。
