# ADR-0008: proof_system v1 は「決定的に検証可能」な規則に限定（Validator内SMTなし）

- Status: Accepted
- Date: 2026-01-17

## Context
Validator は小さく単純である必要がある。SMTをValidatorに含めるとTCBが肥大化し、停止性や可搬性も難化する。

## Decision
- proof_system v1 では以下のみを扱う:
  - SAFE: CFG上の帰納的不変条件 + PO含意
  - BUG: IR意味論に沿った反例トレースの再生 + PO違反到達
- SMT proof object 等を扱う場合は proof_system_version を上げて段階導入する。

## Consequences
- 初期は UNKNOWN が多くても、誤った SAFE/BUG を出さない構造が成立する。
- Analyzer は高度化しても、証拠を v1 規則に還元できない部分は UNKNOWN になる。

## References
- SRS v1.1: REQ-VAL-003/004, REQ-VAL-010, REQ-ANL-020/021
