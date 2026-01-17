# ADR-0005: PO 生成は sink 網羅 + PO安定ID（po_id）を採用

- Status: Accepted
- Date: 2026-01-17

## Context
SAP++ は PO を漏れなく生成し、PO単位で SAFE/BUG/UNKNOWN を確定・追跡（diff）できることが必要。

## Decision
- Frontend/IR上で「危険操作（sink）」を網羅し、すべての登場箇所で PO を生成する。
- POには安定ID `po_id` を付与し、**diff のキー**とする。
- po_id 生成要素（最低限）:
  - 解析対象同一性（repo_identity: path + content hash）
  - 関数同定（clang USR）
  - IRアンカー（block_id + inst_id）
  - po_kind
  - semantics/profile/proof の version triple

## Consequences
- PO単位の追跡が可能。
- 微小編集耐性（行番号ズレ等）強化は別ADRで扱う。

## References
- SRS v1.1: REQ-PO-001/002/002a/002b, REQ-REP-003
