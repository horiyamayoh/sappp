# ADR-0011: Spec DB は Contract IR に正規化し、Tier制度を強制する

- Status: Accepted
- Date: 2026-01-17

## Context
契約は外部ライブラリ等の境界条件をモデル化するために必須。一方、AI推定契約を保証根拠に混入させると嘘SAFEの温床になる。

## Decision
- 契約は「ソース内注釈」と「外部サイドカー」を受理するが、内部では `contract_ir.v1` に正規化する。
- 契約対象は clang USR を主キーとして一意に解決する。
- Tier0/1/2/Disabled を実装し、Tier2 は SAFE 根拠として使用禁止（Validatorが混入を検査）。
- version_scope の評価順を固定する（abi → library_version → conditions）。
- version_scope で同率の場合は priority を優先し、さらに同率なら contract_id で安定ソートする。

## Consequences
- 導入初期から第三者コードに契約適用できる。
- 契約のフォーマット（注釈構文/サイドカーの具体形式）は別ADRで確定する。

## References
- SRS v1.1: REQ-SPC-001..014, REQ-SPC-020..023, REQ-AI-002
