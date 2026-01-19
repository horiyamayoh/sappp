# 品質ゲート戦略 (SAP++)

本書は、品質ゲートのプロファイル定義、ゲート階層への対応関係、
およびローカル/CI で使用する正のコマンドを定義します。
決定性と CI パリティを担保しつつ、開発者フィードバックを速く、
予測可能に保つことが目的です。

## 目的

- CI パリティ: ローカルの `ci` チェックは GitHub Actions と同等にする
- 迅速なフィードバック: `quick` と `smart` で反復速度を確保する
- 決定性: warning-as-error、決定性テスト、スキーマ検証は `full/ci` で常に実施
- 透明性: スタンプにモード、tidy 範囲、スキップ項目を記録する

## ゲートプロファイル（単一の正）

下記スクリプトが正の実装です。

- `quick`: `./scripts/quick-check.sh`（または `make quick`, `./scripts/docker-ci.sh --quick`）
  - clang-format: 変更された C++ のみ
  - GCC ビルド: Debug + WERROR
  - テスト: quick ラベルがあればそれのみ、なければ全テスト
  - 決定性テスト/clang-tidy/スキーマ検証/Clang ビルドは実行しない

- `smart`: `./scripts/pre-commit-check.sh --smart`（または `make smart`, `./scripts/docker-ci.sh --smart`）
  - 変更内容に応じたゲート
  - clang-format: C++/ヘッダ変更時のみ
  - GCC ビルド/テスト + 決定性: ビルド影響のある変更時のみ
  - clang-tidy: 変更された C++ のみ
  - ヘッダのみの変更では tidy 範囲は拡張しない（`--tidy-all` または full/ci を使用）
  - スキーマ検証: schemas 変更時のみ
  - Clang ビルド: デフォルトでスキップ（`--with-clang` で強制）
  - ツール不足（clang-tidy/ajv）は smart ではスキップ扱い

- `full`: `./scripts/pre-commit-check.sh`（デフォルト）または `./scripts/docker-ci.sh`
  - clang-format: 対象ソース全体
  - GCC ビルド/テスト + 決定性
  - Clang ビルド/テスト
  - clang-tidy: 対象全体（libs/tools/include）
  - スキーマ検証
  - skip フラグは許可されるが、スタンプは `partial` に降格

- `ci`: `./scripts/pre-commit-check.sh --ci`（または `make ci`, `./scripts/docker-ci.sh --ci`）
  - `full` と同一のチェックだが skip フラグは拒否
  - tidy_scope は強制的に `all`
  - CI パリティの厳格モード

## ゲート階層

- L0（Quick）: ローカル反復
  - プロファイル: `quick`
  - コマンド: `make quick`

- L1（Commit）: 変更内容に応じた pre-commit
  - プロファイル: `smart`（pre-commit hook のデフォルト）
  - コマンド: `make smart`

- L2（Release/CI）: push 前/CI でのフルパリティ
  - プロファイル: `ci`（厳格）または `full`（ローカルフル、skip 可）
  - コマンド: `make ci`（ローカル）または `./scripts/docker-ci.sh --ci`（Docker）

CI はローカルと同じ Docker ベースのゲートを使用し、乖離を防ぎます。
CI のゲート選択は `SAPPP_CI_GATE_MODE`（`smart`/`full`/`ci`）
または workflow_dispatch 入力で制御します。

## スタンプ

成功したチェックはスタンプファイル（既定: `.git/sappp/ci-stamp.json`）を出力します。
主なフィールドは以下です。

- `check_mode`: quick/smart/full/ci/partial
- `tidy_scope`: changed/all
- `skipped_steps`: スキップ項目のカンマ区切り

pre-push はスタンプを検証し、許可リストにないモードは拒否します
（既定: `full,ci`）。上書き例:

- `SAPPP_PRE_PUSH_REQUIRE=full,ci,smart`
- `./scripts/pre-push-check.sh --require=full,ci,smart`

フックは一度だけインストールします（`make install-hooks`）。
pre-push hook は必須扱いで、未インストールは `make check-env` で検知されます。

## 推奨ワークフロー

1. `make quick`（作業中）
2. `make smart`（コミット前）
3. `make ci`（push 前または CI パリティが必要なとき）

## Docker 利用

- Full（ローカルフル）: `./scripts/docker-ci.sh`
- CI パリティ（厳格）: `./scripts/docker-ci.sh --ci`
- Smart: `./scripts/docker-ci.sh --smart`
- Quick: `./scripts/docker-ci.sh --quick`
- スタンプ出力: `./scripts/docker-ci.sh --stamp`
- キャッシュは既定で有効（ccache + host build）
- 無効化: `./scripts/docker-ci.sh --no-cache`
- tmpfs 利用: `./scripts/docker-ci.sh --tmpfs`
- Docker ビルドは既定で `build-docker` / `build-docker-clang` を使用
  （上書き: `SAPPP_BUILD_DIR` / `SAPPP_BUILD_CLANG_DIR`）
