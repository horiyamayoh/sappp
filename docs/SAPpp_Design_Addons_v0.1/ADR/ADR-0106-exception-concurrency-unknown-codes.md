# ADR-0106: 例外・並行性の v1 保守的 UNKNOWN 条件と unknown_code 細分化

- Status: Proposed
- Date: 2026-01-17

## Context
SRS は 例外/仮想/並行性の“器”を必須要求するが、精密化は段階導入可。
v1 は保守的に UNKNOWN に倒すが、分類粒度が粗いと開拓計画が立てにくい。

## Options
1. 例外/並行性は v1 では一律 UNKNOWN（粗い）
2. 最低限の分類（ExceptionFlowConservative / ConcurrencyUnsupported / AtomicOrderUnknown 等）を導入
3. さらに細分化して、開拓アクションを機械可読にする

## Decision (TBD)
- v1 は (2) を最小セットとして採用し、
  - どのIRイベントが混ざるとUNKNOWNにするか
  - どの契約が不足するとどのunknown_codeにするか
  を仕様化する。

## Consequences
- UNKNOWN が“開拓可能”として機能する。

## References
- SRS v1.1: REQ-CONC-011, REQ-UNK-001..003
