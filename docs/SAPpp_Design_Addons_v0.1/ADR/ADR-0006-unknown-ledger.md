# ADR-0006: UNKNOWN は台帳化し、missing_lemma と refinement_plan を必須とする

- Status: Accepted
- Date: 2026-01-17

## Context
SRS は UNKNOWN=0 を合格条件にしない一方、UNKNOWN を“開拓可能”にすることを最上位価値とする。

## Decision
- UNKNOWN 1件ごとに以下を必須フィールドとして持つ（unknown.v1）:
  - unknown_code（分類）
  - missing_lemma（機械可読＋pretty）
  - refinement_plan（推奨精密化：パラメータ付き actions）
  - unknown_stable_id（差分追跡）

## Consequences
- UNKNOWN が開発バックログ/改善計画として機能する。
- unknown_code 体系はバージョン管理し拡張する。

## References
- SRS v1.1: REQ-UNK-001..005, REQ-PRIN-006
