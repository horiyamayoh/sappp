# ADR-0004: Normalized IR（nir.v1）は CFG + C++イベントの自前IRを採用

- Status: Accepted
- Date: 2026-01-17

## Context
SRS は「コア解析はIRのみ」「寿命/例外/仮想/並行性/UB sink を表現できる器」を初期から要求する。
LLVM IR へ寄せると最適化やフラグ差で表現が揺れ、PO安定IDやdiffが難化する。

## Decision
- 自前の CFG ベース IR（nir.v1）を採用する。
- C++破綻点は **イベント命令**としてIRに埋め込む（lifetime/exception/vcall/concurrency/ub.check 等）。
- 暗黙デストラクタはCFG上に明示的イベントとして表現する。
- operator[] 等は契約適用可能な形に正規化する。

## Consequences
- Analyzer/Validator の共通言語が固定される。
- 初期は未精密な領域を UNKNOWN へ倒せるが、表現力は先に確保する。

## References
- SRS v1.1: REQ-IR-001/002/010/011/012/013, REQ-PRIN-007
