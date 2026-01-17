# ADR-0001: Analyzer/Validator 二分割と Validator による確定

- Status: Accepted
- Date: 2026-01-17

## Context
SAP++ は「嘘SAFEゼロ/嘘BUGゼロ」「Proof-Carrying」「Validatorによる確定」「TCB最小化」を最上位原則としている。
Analyzer は複雑化してよいが、SAFE/BUG を Analyzer が自己申告する構造は、生成AI時代の“自作自演”により破綻しやすい。

## Decision
- システムを **Analyzer（不信）** と **Validator（信頼核 / TCB）** に二分割する。
- SAFE/BUG は **Validator が証拠を検証した場合のみ確定** とする。
- 検証失敗は必ず UNKNOWN へ降格し、標準理由コードを出力する。

## Consequences
- Analyzer の更新・差し替えは、Validator が通らない限り SAFE/BUG を増やせない（安全側）。
- 開発初期は UNKNOWN が多くても良いが、UNKNOWN は「不足理由/不足補題/開拓計画/安定ID」を必須とする。
- Validator は小さく単純である必要があり、証拠形式（proof_system）を先に固定する必要がある。

## References
- SRS v1.1: REQ-ARCH-001/002/003, REQ-PRIN-003/004, REQ-VAL-001/002/003
- SAD v0.1 / 実現方式検討結果報告書 v1
