# TASK ID

UI-P1-005

# Execution Mode

BATCH

# Review Effort

HIGH

# Execution Reason

事前リスクスコア1

# Review Reason

BATCHの標準レビュー深度; 自動昇格: 実差分 8ファイル/1061行、2サブシステム、差分スコア6; HIGHを使用

Latest diff assessment: 実差分 8ファイル/1061行、2サブシステム、差分スコア6; HIGHを使用

# Current Stage

1/1: full - 一括実装

方向で包含と交差を分ける矩形選択



# 目的

方向で包含と交差を分ける矩形選択

# 背景

See the referenced specifications and current code.

# 変更対象候補

- `src/next/kachakacha/app/Selection.h`
- `src/next/kachakacha/app/Selection.cpp`
- `src/apps/cad_next/V2Viewport.h`
- `src/apps/cad_next/V2Viewport.cpp`
- `src/apps/cad_next/V2SelfTestScreen.cpp`

# 変更禁止範囲

- Do not change files unrelated to this task
- Do not change V1

# 必須仕様

- `docs/v2/ui-ux-integrated-spec.md#42-基本操作`

# 作業手順

1. Inspect the relevant code
2. Implement the smallest change
3. Add tests

# Acceptance Tests

- 左から右は矩形に完全包含された対象だけを選ぶ
- 右から左は矩形と交差した対象も選ぶ
- Ctrl併用時は既存選択へ追加または解除する
- 5 logical px未満の移動を矩形選択として扱わない

# 完了条件

全Acceptance Testsとローカル検証が成功し、無関係な変更がないこと。

# 報告形式

1. 変更ファイル一覧
2. 実装内容
3. Build結果
4. Test結果
5. git diff要約
6. 残存問題
