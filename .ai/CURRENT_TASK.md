# TASK ID

UI-P1-007

# Execution Mode

STAGED

# Review Effort

HIGH

# Execution Reason

事前リスクスコア4; 複数の操作状態が連携

# Review Reason

STAGEDの標準レビュー深度

# Current Stage

1/2: foundation - 基盤

状態と共通境界を先に完成させる。

1. 関連する基盤と状態遷移を実装する。

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

- 基盤Stageの作業が完了し、既存ビルドと試験が成功する。

# 完了条件

全Acceptance Testsとローカル検証が成功し、無関係な変更がないこと。

# 報告形式

1. 変更ファイル一覧
2. 実装内容
3. Build結果
4. Test結果
5. git diff要約
6. 残存問題
