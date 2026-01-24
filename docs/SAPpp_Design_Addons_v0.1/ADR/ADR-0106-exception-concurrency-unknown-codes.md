# ADR-0106: 例外・並行性の v1 保守的 UNKNOWN 条件と unknown_code 細分化

- Status: Decided
- Date: 2026-01-24

## Context
SRS は 例外/仮想/並行性の“器”を必須要求するが、精密化は段階導入可。
v1 は保守的に UNKNOWN に倒すが、分類粒度が粗いと開拓計画が立てにくい。

## Options
1. 例外/並行性は v1 では一律 UNKNOWN（粗い）
2. 最低限の分類（ExceptionFlowConservative / ConcurrencyUnsupported / AtomicOrderUnknown 等）を導入
3. さらに細分化して、開拓アクションを機械可読にする

## Decision
v1 は Option (2) を最小セットとして採用し、例外/仮想/並行性の保守的 UNKNOWN を
**IRイベント駆動で分類**する。分類は PO 単位だが「関数内に該当イベントが存在するか」
で判断し、**安定な優先順位**で1つの unknown_code を選ぶ。

### 分類ルール（優先順位の高い順）
1. `SyncContractMissing`
   - 条件: `sync.event` が存在 **かつ** 対象関数の契約に `concurrency` 節が無い
2. `AtomicOrderUnknown`
   - 条件: `atomic.*` または `fence` が存在
3. `ConcurrencyUnsupported`
   - 条件: `thread.spawn` / `thread.join` / `sync.event` が存在（上位条件に該当しない場合）
4. `ExceptionFlowConservative`
   - 条件: `invoke` / `throw` / `landingpad` / `resume` のいずれか、または CFG の
     `exception` エッジが存在
5. `VirtualDispatchUnknown`
   - 条件: `vcall` 命令が存在、または `tables.vcall_candidates` が非空

**補足**:
- `MissingContract.*` と `Lifetime*` はより直接的な原因のため、上記より優先する。
- `RaceUnproven` は data race PO が導入されるまで v1 では未使用（将来拡張枠）。

### refinement_plan の割り当て
| unknown_code | refinement_plan.action | domain | 意図 |
| --- | --- | --- | --- |
| ExceptionFlowConservative | refine-exception | exception | 例外フローのモデル化 |
| VirtualDispatchUnknown | resolve-vcall | dispatch | 動的ディスパッチ解決 |
| AtomicOrderUnknown | refine-atomic-order | concurrency | メモリオーダ/HBの導入 |
| SyncContractMissing | add-contract | concurrency | 同期契約の追加 |
| ConcurrencyUnsupported | refine-concurrency | concurrency | 並行性解析の導入 |

## Consequences
- UNKNOWN が“開拓可能”として機能する。

## References
- SRS v1.1: REQ-CONC-011, REQ-UNK-001..003
