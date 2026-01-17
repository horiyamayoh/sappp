# ADR-0105: Validator 実装言語（C++/Rust分離）と配布形態

- Status: Proposed
- Date: 2026-01-17

## Context
Validator は TCB の中心であり、小さく単純であるべき。
実装言語を Analyzer から分離することで、依存関係を削減できる可能性がある。

## Options
1. Validator も C++ で実装（ビルド容易・共通コード再利用）
2. Validator を Rust で分離（安全性/依存削減/配布性）
3. Validator を WASM にしてサンドボックス化

## Decision (TBD)
- Milestone A は C++ で開始し、インタフェースを安定させた後に Rust/WASM を検討する。

## Consequences
- 分離する場合は、IR/証拠スキーマ以外の依存を持たない設計を厳守する。

## References
- SRS v1.1: REQ-ARCH-002, REQ-VAL-003
