# ADR-0101: カノニカルJSONの厳密仕様（数値/浮動小数/Unicode/改行）

- Status: Proposed
- Date: 2026-01-17

## Context
証拠・manifest の改ざん検出や決定性のため、カノニカル化規約を厳密に固定する必要がある。
一方で、JSONの「数値表現」「浮動小数」「Unicode正規化」「改行」などは実装差が出やすい。

## Options
1. **浮動小数禁止**（v1方針）: 数値は整数のみ。必要なら文字列で表現。
2. 浮動小数を許すが、表現を固定（IEEE754/NaN/Infの扱い等）
3. JSONをやめ、Canonical CBOR/MessagePack 等へ移行

## Decision (TBD)
- v1 では (1) を採用済みだが、
  - 文字列の Unicode 正規化（NFC等）
  - 改行（LF固定）
  - エスケープ規則
  - オブジェクトキーの辞書順ルール
  をどこまで規範化するかを確定する。

## Consequences
- ここが曖昧だと hash mismatch が頻発し、運用で詰む。
- 厳密化しすぎると実装負荷が上がる。

## References
- SRS v1.1: REQ-CRT-012/013, REQ-REP-002
