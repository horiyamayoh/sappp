# ADR-0010: pack（再現パック）+ diff を Milestone A で成立させる

- Status: Accepted
- Date: 2026-01-17

## Context
SRS は再現パック（pack）と差分（diff）をSDD運用の基盤要求としている。

## Decision
- `pack.tar.gz + manifest.json` を生成し、packから validate が再実行できる構成にする。
- pack は build_snapshot / nir / source_map / po_list / unknown_ledger / certstore / specdb snapshot / validated_results / config を含む。
- diff は PO安定ID（po_id）をキーに before/after のカテゴリ変化を出力し、後退理由を機械可読で出す。

## Consequences
- CI・回帰・差分説明の運用が可能になる。
- ヘッダ/標準ライブラリ再現（repro_assets）は段階導入（L0〜L3）とする。

## References
- SRS v1.1: REQ-REP-001/003/004
