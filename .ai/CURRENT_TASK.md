# TASK ID

UI-P1-007

# Execution Mode

STAGED

# Review Effort

EXTRA_HIGH

# Execution Reason

事前リスクスコア4; 複数の操作状態が連携

# Review Reason

STAGEDの標準レビュー深度; 自動昇格: 実差分 14ファイル/593行、5サブシステム、差分スコア10; EXTRA_HIGHを使用

Latest diff assessment: 実差分 22ファイル/1549行、7サブシステム、差分スコア11; EXTRA_HIGHを使用

# Current Stage

2/2: integration - 連携と回帰

UI連携と回帰確認まで完成させる。

1. UI連携と回帰試験を完成させる。

# 目的

選択半径とスナップヒステリシス

# 背景

See the referenced specifications and current code.

# 変更対象候補

- `src/next/kachakacha/geometry/GeometryTolerance.h`
- `src/next/kachakacha/modeling/SnapEngine.h`
- `src/next/kachakacha/modeling/SnapEngine.cpp`
- `tests_v2/snap_tests.cpp`
- `src/apps/cad_next/V2SelfTestInput.cpp`

# 変更禁止範囲

- Do not change files unrelated to this task
- Do not change V1

# 必須仕様

- `docs/v2/ui-ux-integrated-spec.md#43-候補優先順位`
- `docs/v2/ui-ux-integrated-spec.md#61-スナップ`

# 作業手順

1. Inspect the relevant code
2. Implement the smallest change
3. Add tests

# Acceptance Tests

- Edge 6、Vertex 8、Snap 12 logical pxを別の許容値として扱う
- ズーム倍率にかかわらず画面上の選択半径を保つ
- 小さなポインタ揺れでスナップ候補が切り替わり続けない
- Sを押している間だけスナップを無効にする

# 完了条件

全Acceptance Testsとローカル検証が成功し、無関係な変更がないこと。

# 報告形式

1. 変更ファイル一覧
2. 実装内容
3. Build結果
4. Test結果
5. git diff要約
6. 残存問題
