# ADR-0003: Frontend は Clang Tooling（clang/clang-cl）を採用

- Status: Accepted
- Date: 2026-01-17

## Context
C++ の名前解決（オーバーロード/テンプレ/cv-ref/noexcept）を安定・一意に扱い、契約（Spec DB）を適用可能にする必要がある。
また、ソース位置復元（source map）や、暗黙デストラクタの明示化など、SRSが要求するIR正規化要件を満たす必要がある。

## Decision
- TUフロントエンドは Clang LibTooling を用いる。
- Windows/MSVC 系は `clang-cl` をサポートする。
- target triple/ABI/主要型サイズなどを build_snapshot/pack に必ず記録する。

## Consequences
- LLVM IR ベースよりも「ソース準拠・契約適用・差分追跡」に有利。
- Clang バージョン差による微妙なAST/CFG差分が決定性の難所になり得る（別ADRで緩和策）。

## References
- SRS v1.1: REQ-IN-001..004, REQ-SPC-012, REQ-IR-010/011
