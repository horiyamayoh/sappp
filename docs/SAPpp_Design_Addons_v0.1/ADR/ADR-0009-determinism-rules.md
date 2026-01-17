# ADR-0009: 決定性（並列でも同一結果）を I/F 規約で担保する

- Status: Accepted
- Date: 2026-01-17

## Context
SRS は「同一入力・同一設定・同一バージョンで、カテゴリとIDが一致（並列でも同一）」を必須とする。

## Decision
- すべての出力は安定ソート規約を持ち、並列処理の集約は安定マージで確定順序を固定する。
- パス正規化（区切り、.`/..`、Windowsドライブ等）を統一する。
- カノニカルJSON規約を定め、証拠・manifest などのハッシュ対象を固定する。

## Consequences
- 再現性（pack/diff）とキャッシュキーが安定する。
- Windows/UNIX混在環境ではパス規約の実装が難所になり得る。

## References
- SRS v1.1: REQ-REP-002, REQ-PO-002a, REQ-CRT-012/013
