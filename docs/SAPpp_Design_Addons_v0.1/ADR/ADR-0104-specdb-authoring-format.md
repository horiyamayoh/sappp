# ADR-0104: Spec DB の入力形式（注釈構文/サイドカー形式）の確定

- Status: Proposed
- Date: 2026-01-17

## Context
Spec DB は MUST 要求。だが「ソース内注釈の具体構文」「外部サイドカー形式（JSON/YAML/TOML）」「対象解決キー」の仕様が未確定。

## Options
1. サイドカーは JSON（schemaで厳密化）。注釈は `//@sappp` で YAML-ish。
2. サイドカーは YAML（人間向け）。内部は JSON正規化。
3. 既存仕様言語（ACSL, Frama-C, SAL 等）に寄せる（学習コスト増）。

## Decision (TBD)
- v1: サイドカーは JSON とし、schema で検証可能にする。
- 注釈構文は最低限（Pre/Postのみ）から開始し、後方互換を壊さない拡張規則を定める。

## Consequences
- サイドカーのUXと、対象解決の正確さ（USR生成）の実装が要。

## References
- SRS v1.1: REQ-SPC-001/002/012/013
