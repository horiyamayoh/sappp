# 品質ゲート改善の概要

本書は品質ゲートの更新点と、その背景にある課題を要約します。
定義と正のコマンドは `docs/QUALITY_GATE_STRATEGY.md` を参照してください。

## 対応した課題

- ローカルで成功しても CI で失敗する（clang-tidy のパリティとツール判定）
- clang-tidy の厳格さによる過剰な摩擦
- 小さな変更での繰り返し作業の増大
- 統一戦略のないアドホックなゲート挙動

## 主要な変更

- ゲートプロファイルを追加: `quick`, `smart`, `full`, `ci`
- pre-commit のデフォルトを `smart` に変更（変更最適・高速）
- `full` は `pre-commit-check.sh` / `docker-ci.sh` のデフォルト（ローカルフル）
- `ci` は CI パリティを強制し、skip フラグを禁止
- clang-tidy は CI で Clang の compile_commands を優先使用
- Docker CI でスタンプ出力が可能に（`--stamp`）
- Docker キャッシュは既定で有効（ccache + host build）
  - 必要に応じて `--no-cache` / `--tmpfs` で無効化
- Docker は専用ビルドディレクトリ（`build-docker`, `build-docker-clang`）を使用
- pre-push hook は必須扱い（未インストールは check-env で検知）
- スタンプにモード/範囲/スキップ項目を記録
- スキップ項目がある場合、full/ci のスタンプは `partial` に降格
- GitHub CI はローカルと同じ Docker ゲートを使用してパリティを確保
- CI のゲートモードは `SAPPP_CI_GATE_MODE` / workflow_dispatch で制御可能

## 互換性メモ

- CI は常に厳格。ローカル `smart` は反復速度重視。
- push 前は `make ci`（ローカル）または `./scripts/docker-ci.sh --ci`（Docker）で
  パリティを確認すること。
- `full/ci` でツールが不足している場合は失敗とする（偽の成功は許可しない）。
