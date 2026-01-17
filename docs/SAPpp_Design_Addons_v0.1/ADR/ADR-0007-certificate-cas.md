# ADR-0007: Certificate は PO単位 + CAS/DAG 共有 + カノニカル化 + ハッシュ範囲固定

- Status: Accepted
- Date: 2026-01-17

## Context
証拠は Proof-Carrying の中核。巨大化しても運用可能であり、改ざん検出でき、Validatorが検証できる必要がある。

## Decision
- 証拠は **PO単位**とし、POごとの ProofRoot を持つ。
- 共通部分（不変条件・契約参照等）は CAS（content-addressed storage）で参照共有する。
- CAS オブジェクトはカノニカルJSONでシリアライズし、sha256 でアドレスする。
- ハッシュ対象範囲（hash_scope）を固定し、Validatorが再ハッシュで改ざん検出する。

## Consequences
- 証拠が巨大でも共有によりサイズを抑えられる。
- カノニカル化/ハッシュ範囲が仕様として固定される必要がある（別ADRで厳密化）。

## References
- SRS v1.1: REQ-CRT-001/002/010..013
