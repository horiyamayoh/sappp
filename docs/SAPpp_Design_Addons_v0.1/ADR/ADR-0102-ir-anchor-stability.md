# ADR-0102: IRアンカーの微小編集耐性（ASTアンカー導入）

- Status: Proposed
- Date: 2026-01-17

## Context
po_id は block_id/inst_id をアンカーに含むため、Frontend の CFG/命令列が小変更で揺れると po_id が大きく変動し、diff が劣化する。
SRS は微小編集耐性（REQ-PO-002b）を SHOULD として要求する。

## Options
1. v1のまま（block_id/inst_id）: 決定性は高いが微小編集で揺れやすい。
2. **ASTアンカー**を併用: `clang AST node id + token span + stable hash` を追加し、po_id生成に混ぜる。
3. ソース範囲（file+offset）中心: マクロ/整形で揺れる可能性。

## Decision (TBD)
- v1 は (1) を採用。
- v1.1 以降で (2) を導入し、po_id 生成要素に「ASTアンカー（カノニカル化済み）」を追加するかを決める。

## Consequences
- (2)はdiff品質が上がるが、ASTアンカーの安定化（Clang差分）設計が難しい。

## References
- SRS v1.1: REQ-PO-002b
