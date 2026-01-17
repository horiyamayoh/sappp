# ADR-0103: semantics_version sem.v1 のベースライン規格と逸脱点リスト + litmus 配布

- Status: Proposed
- Date: 2026-01-17

## Context
SRS は semantics_version を「文章 + 最小例（litmus）」で固定し、逸脱点一覧を明示することを MUST としている。
ここが曖昧だと SAFE/BUG の意味が曖昧になり、ツールの信頼性が崩れる。

## Options
1. ベースラインを ISO C++23 とし、逸脱点を sem.v1.md に列挙
2. ベースラインを C++17 に固定（実装容易）し、将来 sem.v2 で更新
3. コンパイラ/標準ライブラリ差分を含めて“実装定義寄り”にする

## Decision (TBD)
- Milestone A では最小限の sem.v1.md を pack に含める。
- sem.v1 の基準規格、逸脱点一覧、litmusセットのフォーマットと格納場所を確定する。

## Consequences
- 逸脱点に起因する場合は UNKNOWN に倒すルールが明確になる。

## References
- SRS v1.1: REQ-SEM-001..004, REQ-SEM-002/003
