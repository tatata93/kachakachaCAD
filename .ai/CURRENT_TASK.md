# TASK ID

UI-P1-003

# 目的

重なった選択候補をV2Viewportへ結線し、TabとAltで意図した候補を選べるようにする。

# 背景

コアは点・曲線の全候補と、形状の手前から奥までの候補を返せる。画面側はまだ最寄りの1件だけを使っているため、重なった線や奥の部品を選び分けられない。

# 変更対象候補

- `src/apps/cad_next/V2Viewport.h`
- `src/apps/cad_next/V2Viewport.cpp`
- `src/apps/cad_next/V2ViewportDraw.cpp`
- `src/apps/cad_next/V2SelfTestScreen.cpp`

# 変更禁止範囲

- `src/next`の選択・メッシュ候補収集アルゴリズムを別実装へ置き換えない。
- MainWindow、Document、Feature DAG、幾何アルゴリズムを変更しない。
- 右クリック候補メニュー、範囲選択、カーソル全面改訂を混ぜない。
- V1を削除・変更しない。

# 必須仕様

- `docs/v2/ui-ux-integrated-spec.md`の選択、Hover、Alt、Tab規定に従う。
- `CollectPickCandidates`と`CollectMeshHits`を再利用する。
- 候補順は安定し、同じ位置でTabを押すたび次候補へ循環する。
- Tabだけでは通常選択を変更しない。
- クリックは現在表示中の候補を選ぶ。
- Alt+クリックは現在候補の次、または規定上の奥候補を選ぶ。
- ポインタが別の場所へ移った場合の候補番号リセットを決定的にする。
- Hover、Selection、Previewを同じ状態として扱わない。

# 作業手順

1. 関連するViewportのhover/select/key処理と既存自己試験を読む。
2. 候補一覧と現在番号をViewportの一箇所だけで保持する。
3. 曲線候補と形状候補を既存の意味を壊さず統合する。
4. Hover、Tab、通常クリック、Alt+クリックを同じ候補状態へ結線する。
5. 画面を出さずに重なりと状態遷移を確認する自己試験を追加する。
6. Windowsのbuild、CTest、アプリ自己試験を通す。

# Acceptance Tests

- 重なった2本以上の線でTabを押すと候補が順番に切り替わる。
- Tab操作だけでは`SelectionSet`が変わらない。
- Tab後の通常クリックは表示中候補を選ぶ。
- Alt+クリックは奥側の候補を選ぶ。
- 別位置へ移動後は先頭候補から始まり、同位置内では順序が揺れない。
- 既存のCtrl複数選択、Shift拘束、Sスナップ抑止が退行しない。
- `ctest --preset windows-msvc`と`kachakacha_cad_next.exe --self-test`が成功する。

# 完了条件

指定範囲の実装と自動試験が揃い、全ビルド・試験が成功し、無関係な変更がないこと。未実装を表示だけで偽装しないこと。

# 報告形式

1. 変更ファイル一覧
2. 実装内容
3. Build結果
4. Test結果
5. git diff要約
6. 残存問題
