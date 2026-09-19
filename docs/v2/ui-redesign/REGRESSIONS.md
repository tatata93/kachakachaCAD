# REGRESSIONS — 指示書 known_regressions(I-01)の固定表

15件の既知の退行それぞれに、どの試験がそれを固定しているかを1対1で対応させる。
「状態」が EXISTING のものは、既にある試験がその退行の再発を検出できると確認済み。
NEW のものは、この作業で `src/apps/cad_next/V2SelfTestRegressions.cpp`(RG-\*)へ追加した。

| # | 退行 | 固定する試験(ファイル: ケース名) | 状態 |
|---|---|---|---|
| 1 | Extrude panel が hidden になる | `src/apps/cad_next/V2SelfTestHumanPath.cpp`: "HP-UI-01 押し出しの最中は棚が見えている"(下見の作り直し・選択替えでも棚が消えないことを確かめる) | EXISTING |
| 2 | Tool 切替後も古い Right Panel が残る | `src/apps/cad_next/V2SelfTestRegressions.cpp`: "RG-02 道具を先に構えても古い右の棚が残らない"(円→測定→Esc→選択→部品モードと渡り歩き、`OperationHost().CurrentShelf()` と `ShelfShown()` の両方で前の棚が残らないことを見る)。土台の一部(道具替え・モード替えでの単純な入れ替わり)は `src/apps/cad_next/V2SelfTestScreen.cpp`: "右の棚がいま使っている道具に付いてくる" が既に見ている | NEW(関連 EXISTING あり) |
| 3 | Tool-first で Ctrl が暗黙必須になる | `tests_v2/extrude_input_state_tests.cpp`: "extrude_input, 素のクリックで足すかは道具が動いているかで決まる"(`PlainClickShouldAdd`)。人の道: `src/apps/cad_next/V2SelfTestHumanPath.cpp`: "HP-EX-02 道具を先に構えても Ctrl 無しで選べる" | EXISTING |
| 4 | Face を選びたいのに Wire が常に勝つ | `tests_v2/extrude_input_state_tests.cpp`: "extrude_input, スロットに合わないものは候補にしない" と `SortKindsForSlot` の並べ替え試験(`PickFitsSlot` / `SortCandidatesForSlot` の土台)。人の道: `src/apps/cad_next/V2SelfTestHumanPath.cpp`: "HP-EX-03 立体の面を画面から拾って押す"(素のクリックで面が拾えることを実際の pick で見る) | EXISTING |
| 5 | Surface に Preview がない | `src/apps/cad_next/V2SelfTestHumanPath.cpp`: "HP-SF-01 面を作る棚と下見が見え、文書はまだ増えない"(`SurfacePreviewShown()` と `Viewport().ToolPreview()` を見る) | EXISTING |
| 6 | GuideTable を知らないと Surface を作れない | 同上 HP-SF-01 と "HP-SF-02 Enter で面が1枚できて棚が片付く"。どちらも矩形を引いて拾い `surface.create` を叩くだけで、GuideTable の欄には一切触れずに面ができることを示す | EXISTING |
| 7 | Extrude distance と板厚が同じ parameter | `tests_v2/command_parameter_tests.cpp`: "parameters, 押し出しの距離は板厚の上限を引き継がない"(`ParameterId::ExtrudeDistance`=板厚と `ParameterId::ExtrudeLengthMm`=押し出し距離が別の state で、互いに動かないことを確認) | EXISTING |
| 8 | 20mm 板厚上限が Extrude へ漏れる | 同上。同じ試験内で「板厚 40mm は断る」「押し出し 120mm は通る」を両方見ている(100mm 級の車体を押せなかった退行そのもの) | EXISTING |
| 9 | Enter が Viewport focus 依存 | `tests_v2/tool_keys_tests.cpp`(全ケース、`ActionForConfirmKey`/`ActionForCancelKey` が入力欄の内外どちらでも正しく動くこと)。人の道: `src/apps/cad_next/V2SelfTestHumanPath.cpp`: "HP-UI-03 右の欄へ打ったあとも Enter と Esc が効く"(3D をクリックし直さずに `HandleToolKey` が届くことを実際の押し出しで見る) | EXISTING |
| 10 | Tree / View selection divergence | `src/apps/cad_next/V2SelfTestExplorer.cpp`: "HP-XP-03 3D と一覧の選択が両方向に同期する"(旧 HP-EX-03。Extrude の自己試験と ID が衝突していたため改名) | EXISTING |
| 11 | 個別 visibility 不足 | `src/apps/cad_next/V2SelfTestExplorer.cpp`: "HP-XP-02 ◉ で1つずつ出し隠しでき、取り消しで戻る"(旧 HP-EX-02。Extrude の自己試験と ID が衝突していたため改名) | EXISTING |
| 12 | 「まとまり」表記 | `src/apps/cad_next/V2SelfTestRegressions.cpp`: "RG-12 画面の文字に「まとまり」が残っていない"(上の帯の作業中グループ、一覧の8節、右クリック献立、命令台帳の表示名/案内文/断り文言のすべてに「まとまり」が無いことを見る) | NEW |
| 13 | Origin が最上段でない | `src/apps/cad_next/V2SelfTestExplorer.cpp`: "HP-XP-01 一覧の節は正本の順で原点が先頭"(旧 HP-EX-01。Extrude の自己試験と ID が衝突していたため改名) | EXISTING |
| 14 | 狭い画面で UI が潰れる | `src/apps/cad_next/V2SelfTestRegressions.cpp`: "RG-14 狭い画面でも帯の道具が右端で切れない"(1280×720 で全モード・全カテゴリを実際にクリックして回り `V2Ribbon::ToolsFitInWidth()` を見る。これまでどの試験からも呼ばれていなかった)。関連 EXISTING: `src/apps/cad_next/V2SelfTestBasics.cpp`: "狭い画面でも部品がはみ出さない"(窓とビューポートの大きさ・ViewCube のはみ出しは見ているが、帯の道具の右端切れは見ていない) | NEW(関連 EXISTING あり) |
| 15 | hidden widget を直接操作するだけの self-test | 試験は追加しない(ランタイムでは検出できない類のもの)。`UI_LESSONS.md` の原則「self-test は人の道(実際の viewport pick、見えているボタン)。hidden widget 直叩き・UUID 注入だけを合格にしない」をレビューで守る。実際に拾う道具は `V2SelfTest.h` に集まっている: `DrawRectangleByHand` / `DrawRectangleAtByHand` / `ClickOnAnyCurve` / `ClickOnCurveOf` / `ClickOnAnyGuideSurface`(すべて `Viewport().ClickAt`/`SelectAt` を実際に呼ぶ)、および帯の `Ribbon().ClickCategory` / `ClickTool`。新しいケースを書くときはこれらを使い、`SelectionRef` を手で組んで `pickedFaceIndex` を書き込むような近道は避ける | 対象外(方針のみ) |

## 新規ケース(この作業で追加)

`src/apps/cad_next/V2SelfTestRegressions.cpp`(`RegressionCases()`。`V2SelfTest.h`/`V2SelfTest.cpp`/`CMakeLists.txt` へ登録済み):

- `RG-02 道具を先に構えても古い右の棚が残らない`
- `RG-12 画面の文字に「まとまり」が残っていない`
- `RG-14 狭い画面でも帯の道具が右端で切れない`

## 検証(このセッションの tool 出力)

- `timeout 290 sh tools/qtstub/typecheck.sh . g++` → `qt stub typecheck: OK (102 files)`
- `cmake --build build-core --parallel 8` → 変更は Qt 専用ターゲット(`kachakacha_cad_next`)のみなので `build-core` は再ビルド不要(`ninja: no work to do.`)
- `(cd build-core && ctest -E qt_stub)` → `100% tests passed, 0 tests failed out of 149`

PC 側(実際に Qt を通す自己試験の実行)はこのセッションでは行っていない。次のセッションで PC 実行し、`PC_VERIFIED` の記録を別途残すこと。

## 2026-09-19 PC 3 回目で見つかった退行(固定済み)

- HP-AR-01 の窓ハング: 何が = 配列の窓 V2ArrayDialog の `exec()` を本番で据え付けていたため自己試験(offscreen)が閉じられず 900 秒でタイムアウト。なぜ = モーダル窓を本番経路に残していた。固定する試験 = HP-AR-01/02(`SetArrayChooser(nullptr)` で棚の道を通す)。
- HP-XP-02 の use-after-free: 何が = ToggleEntityVisibilityFromItem が木の作り直し後に消えた `QTreeWidgetItem*` の `text(0)` を読んでいた。なぜ = AdoptCurrentDocument/RefreshEntityList 後もポインタを保持し続けていた。固定する試験 = HP-XP-02(名前と id を先に写してから木を作り直す)。
- HP-SF-06(やめると 3D の印も消える): 何が = 面作成をキャンセルしても 3D 上の選択の印が残っていた。なぜ = EndSurfacePreview が選択を空にしていなかった。固定する試験 = HP-SF-06。
- resp の絵が倍率で同じ: 何が = `resp/v2-<state>-<WxH>-<scale>.png` の倍率違いの絵がすべて同一だった。なぜ = main.cpp の撮影が devicePixelRatio を掛けずに撮っていた。固定する試験 = 撮影スクリプトの目視比較(自動試験は未追加)。
