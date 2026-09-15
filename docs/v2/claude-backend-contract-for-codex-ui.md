# Codex の UI 再構築へ渡す、backend 側の約束(2026-09-15)

オーナー指示により、UI frontend は Codex が全面的に作り直す。
Claude は `src/apps/cad_next/**` を原則さわらない。
`CODEX_UI_OWNERSHIP.md` が出たら、そこに書かれた範囲を正とする。

この文書は「作り直すときに、**画面側で考え直さなくてよいもの**」の一覧である。
2026-09-14〜15 の作業で、判断はできるだけ core(`src/next/kachakacha/app`)へ
出してある。**画面は映して、押されたことを伝えるだけ**で済むようにしてある。
同じ判断を画面側で書き直すと、2か所に別の答えが生まれる。

## 1. 入力の状態(画面の欄と1対1)

| 頭書き | 何を決めるか |
| --- | --- |
| `app/ExtrudeInputState.h` | 対象 / 輪郭 / 演算 / 範囲 / 方向 / 距離 / 出力。`NextNeededSlot` が「次に何を選んでほしいか」を返す |
| `app/SurfaceInputState.h` | 作り方 / 断面 / ガイド / 境界 / 断面順。`RecommendSurfaceMethod` `SurfaceSlotsFor` `SurfaceReadyToBuild` `SurfaceCountProblemJa` |
| `app/ShelfLayout.h` | どのモード・どの道具で、どの棚が出るか。`ShelvesFor(mode, tool, extruding, surfacing)` |

**`ShelvesFor` を通さずに棚を `show()` しないこと。**
それをやって、押し出しの棚が構造的に見えない状態が長く続いた。
いまは棚の到達性を試験が見ている(`tests_v2/shelf_layout_tests.cpp`)。
`作業平面` と `表示` の2つだけは例外として自分の命令から出しており、
試験にその2つを名前で列挙してある。**3つ目を作ると門が鳴る。**

## 2. 拾い方(選択)

- `ExtrudeSlot` … いま求めているもの。候補の**並べ替え**に使う。捨てない。
- `PlainClickShouldAdd(toolActive, picked, already)` …
  素のクリックで前の選択に足すか。**「いま足りないスロット」で決めてはいけない。**
  判断に使うのは「道具が動いているか」。ここを混ぜて一度壊した。
- `FacePickMeansItsSolid(toolActive, picked, already)` …
  輪郭がもう入っているなら、拾った面は相手の立体を指している。

いずれも `app/ExtrudeInputState.h`。**Ctrl も Tab も知らずに操作できること**が
この3つの目的である。

## 3. 合図(Enter / Esc)

`app/ToolKeys.h`。欄の中で値を変えて Enter は「値を入れて下見を作り直す」、
変えずに Enter は「確定」、欄の外は「確定」、Esc は「やめる」。

**3D にキーボードの焦点が無くても効くこと。**
「3D を一度クリックして焦点を戻す」設計は禁止(そのクリックで選択が変わる)。
窓が `eventFilter` で先に受ける形にしてある。

## 4. 下見(文書へ書かずに見せる)

| 頭書き | 何を作るか |
| --- | --- |
| `app/ExtrudePreview.h` | 押し出しの側面とふたを多角形に。厚みが0なら返さない |
| `app/SurfacePreview.h` | 面の標本の格子と境界を折れ線に。標本が足りなければ返さない |
| `app/ToolRoleLabels.h` | 3D に出す札。`TARGET` `PROFILE` `Section 1..n` `Guide L/R` |
| `app/ToolFooter.h` | 一番下の一行と、合図の説明(Tab/Ctrl/Esc/Enter) |

**確定するまで文書へ書かないこと。**下見と確定は同じ写しから作ること。
確定のときに選択を読み直すと、見ていたものと違うものが出来る。

## 5. 面のずれ(2026-09-15、Codex 判定待ち)

`modeling/SurfaceDeviationLimit.h`。作り方ごとに許容が違う。
近づけて作る面(案内付きロフト・曲線網・境界埋め)は通ったときも
外れた量を言う(`KER-S102`)。**画面はその言葉をそのまま出すだけでよい。**

## 6. 画面側から消えてよいもの

今の `src/apps/cad_next/**` は作り直しの対象である。ただし上の core は残る。
`V2ExtrudeDock` `V2SurfaceDock` `V2Shelves` などの形は自由に変えてよい。
`V2SelfTestHumanPath.cpp` の人の道の試験は、**判定の中身は残してほしい**。
不可視の widget を直に叩いて合格にしない、という約束がそこに書いてある。
