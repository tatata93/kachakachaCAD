# TASK ID

UI-P1-006

# Execution Mode

STAGED

# Review Effort

EXTRA_HIGH

# Execution Reason

事前リスクスコア2; 複数の操作状態が連携; 自動昇格: 実差分が10ファイル以上、3サブシステム以上、または2000行以上

# Review Reason

BATCHの標準レビュー深度; 自動昇格: 実差分 13ファイル/843行、3サブシステム、差分スコア10; EXTRA_HIGHを使用

Latest diff assessment: 実差分 15ファイル/1091行、3サブシステム、差分スコア10; EXTRA_HIGHを使用

# Current Stage

1/1: full - 一括実装

意味状態別ハイライトと部分要素表示



# 目的

意味状態別ハイライトと部分要素表示

# 背景

See the referenced specifications and current code.

# 変更対象候補

- `src/apps/cad_next/V2Viewport.h`
- `src/apps/cad_next/V2Viewport.cpp`
- `src/apps/cad_next/V2ViewportDraw.cpp`
- `src/apps/cad_next/V2SelfTestScreen.cpp`

# 変更禁止範囲

- Do not change files unrelated to this task
- Do not change V1

# 必須仕様

- `docs/v2/ui-ux-integrated-spec.md#3-意味状態と表示`

# 作業手順

1. Inspect the relevant code
2. Implement the smallest change
3. Add tests

# Acceptance Tests

- 通常、Hover、選択、Snap、Previewを別の意味状態で描く
- 同じワイヤーの選択線分だけを強調し他線分を選択色にしない
- テーマ変更後も意味状態の区別を保つ
- Previewは通常選択できない

# 完了条件

全Acceptance Testsとローカル検証が成功し、無関係な変更がないこと。

# 報告形式

1. 変更ファイル一覧
2. 実装内容
3. Build結果
4. Test結果
5. git diff要約
6. 残存問題
