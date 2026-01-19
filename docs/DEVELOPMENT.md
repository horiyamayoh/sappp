# SAP++ 開発ガイド

このドキュメントは、SAP++ の開発環境と開発プロセスの詳細を説明します。

## 目次

1. [開発環境](#開発環境)
2. [ディレクトリ構成](#ディレクトリ構成)
3. [ビルドシステム](#ビルドシステム)
4. [テスト](#テスト)
5. [CI/CD](#cicd)
6. [トラブルシューティング](#トラブルシューティング)

---

## 開発環境

### 推奨環境: Dev Container

Dev Container を使用すると、CI と完全に同一の環境で開発できます。

#### 含まれるツール

| ツール | バージョン | 用途 |
|-------|-----------|------|
| GCC | 14.x | メインコンパイラ |
| Clang | 19.x | セカンダリコンパイラ |
| clangd | 19.x | LSP（コード補完・診断） |
| clang-format | 19.x | コードフォーマット |
| clang-tidy | 19.x | 静的解析 |
| CMake | 3.28+ | ビルドシステム |
| Ninja | 1.11+ | ビルドツール |
| ajv-cli | latest | JSONスキーマ検証 |
| ccache | latest | ビルドキャッシュ（任意） |

#### VS Code 拡張機能（自動インストール）

- **clangd** - IntelliSense（C/C++ 拡張機能より高速）
- **CMake Tools** - CMake 統合
- **GitLens** - Git 拡張
- **GitHub Copilot** - AI 支援

### Docker CI 環境

Dev Container を使わない場合でも、Docker で CI 同等の環境を利用できます。

```bash
# イメージビルド（初回のみ、約2分）
./scripts/docker-ci.sh --build-only

# full（ローカルフル）
./scripts/docker-ci.sh

# ci（CI互換の厳格チェック）
./scripts/docker-ci.sh --ci

# smart（変更内容に応じて最小化）
./scripts/docker-ci.sh --smart

# quick（高速）
./scripts/docker-ci.sh --quick

# インタラクティブシェル
./scripts/docker-ci.sh --shell

# スタンプを書き込む（pre-push 連携用）
./scripts/docker-ci.sh --stamp

# ビルドキャッシュはデフォルト有効（ccache + build/）
# 無効化: tmpfs で隔離
./scripts/docker-ci.sh --no-cache

# ccache のみ無効化
./scripts/docker-ci.sh --no-ccache

# build/ のみ tmpfs にしたい場合
./scripts/docker-ci.sh --tmpfs
```

Docker CI はホストの `build/` と競合しないように、
`build-docker/` と `build-docker-clang/` を使用します。
必要なら `SAPPP_BUILD_DIR` / `SAPPP_BUILD_CLANG_DIR` で変更できます。

### ローカル環境構築（Ubuntu 24.04）

```bash
# 必須パッケージ
sudo apt update
sudo apt install -y \
    gcc-14 g++-14 \
    clang-19 clang-format-19 clang-tidy-19 clangd-19 \
    cmake ninja-build \
    ccache ripgrep \
    nodejs npm

# ajv-cli（スキーマ検証）
sudo npm install -g ajv-cli ajv-formats

# Git hooks
./scripts/install-hooks.sh

pre-push hook は必須です（未インストールの場合は `make check-env` でエラー）。
```

---

## ディレクトリ構成

```
sappp/
├── .devcontainer/      # Dev Container 設定
│   ├── devcontainer.json
│   └── Dockerfile
├── .github/workflows/  # GitHub Actions CI
├── docker/             # CI 用 Dockerfile
├── docs/               # 設計書・ADR
├── include/sappp/      # 公開ヘッダ
├── libs/               # コアライブラリ
│   ├── common/         # 共通ユーティリティ
│   ├── canonical/      # Canonical JSON
│   ├── build_capture/  # ビルド情報キャプチャ
│   ├── frontend_clang/ # Clang フロントエンド
│   ├── ir/             # 中間表現 (NIR)
│   ├── po/             # Proof Obligation 生成
│   ├── analyzer/       # 解析エンジン
│   ├── certstore/      # 証明書ストア
│   ├── validator/      # 検証器
│   └── report/         # レポート生成
├── schemas/            # JSON Schema 定義
├── scripts/            # ビルド・CI スクリプト
├── tests/              # テストコード
└── tools/sappp/        # CLI ツール
```

---

## ビルドシステム

### CMake オプション

| オプション | デフォルト | 説明 |
|-----------|-----------|------|
| `SAPPP_BUILD_TESTS` | OFF | テストをビルド |
| `SAPPP_BUILD_CLANG_FRONTEND` | OFF | Clang フロントエンドをビルド |
| `SAPPP_WERROR` | OFF | 警告をエラーとして扱う |

### 環境変数一覧

品質ゲートスクリプトで使用される環境変数の完全一覧：

#### ビルド設定

| 変数 | デフォルト | 説明 |
|------|-----------|------|
| `SAPPP_BUILD_JOBS` | `nproc` | ビルド/テストの並列度 |
| `SAPPP_USE_CCACHE` | `1` | ccache 使用（`0` で無効化） |
| `SAPPP_BUILD_DIR` | `build` | GCC ビルドディレクトリ |
| `SAPPP_BUILD_CLANG_DIR` | `build-clang` | Clang ビルドディレクトリ |
| `SAPPP_LOG_DIR` | `$BUILD_DIR/ci-logs` | CI ログ出力先 |

#### 品質ゲート制御

| 変数 | デフォルト | 説明 |
|------|-----------|------|
| `SAPPP_GATE_MODE` | `full` | ゲートモード（`quick`/`smart`/`full`/`ci`） |
| `SAPPP_CI_GATE_MODE` | `ci` | CIのゲートモード（`smart`/`full`/`ci`） |
| `SAPPP_CI_STAMP_FILE` | `.git/sappp/ci-stamp.json` | CI スタンプファイルパス |
| `SAPPP_CI_ENV` | (なし) | CI 環境識別（`docker` など） |

#### Git Hooks 制御

| 変数 | デフォルト | 説明 |
|------|-----------|------|
| `SAPPP_PRE_COMMIT_MODE` | `smart` | pre-commit hook のモード（`quick`/`smart`/`full`/`ci`） |
| `SAPPP_PRE_PUSH_MODE` | `stamp` | pre-push hook のモード（`stamp`/`quick`/`full`/`ci`/`off`） |
| `SAPPP_PRE_PUSH_REQUIRE` | `full,ci` | pre-push のスタンプ許可モード |

#### 使用例

```bash
# 並列度を指定してビルド
SAPPP_BUILD_JOBS=8 make build

# ccache を無効化
SAPPP_USE_CCACHE=0 ./scripts/pre-commit-check.sh

# 高速モードでコミット
SAPPP_PRE_COMMIT_MODE=quick git commit -m "WIP"

# Docker 環境で別のビルドディレクトリを使用
SAPPP_BUILD_DIR=build-docker ./scripts/docker-ci.sh
```

### ビルド例

```bash
# 開発用（推奨）
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=gcc-14 \
    -DCMAKE_CXX_COMPILER=g++-14 \
    -DSAPPP_BUILD_TESTS=ON \
    -DSAPPP_WERROR=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build build --parallel

# リリース用
cmake -S . -B build-release -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSAPPP_WERROR=ON

cmake --build build-release --parallel
```

### CMake Presets（推奨）

```bash
cmake --preset gcc-debug
cmake --build --preset gcc-debug
ctest --preset gcc-debug
```

### Makefile ターゲット

```bash
make help           # ヘルプ表示
make quick          # 高速チェック
make smart          # 変更内容に応じたチェック
make ci             # CI互換の厳格チェック
make docker-ci      # Docker環境でCI互換チェック
make build          # ビルド
make test           # テスト実行
make clean          # クリーン
make format         # フォーマット適用
make format-check   # フォーマットチェック
make tidy           # clang-tidy 実行
```

### clang-tidy 対象範囲

clang-tidy の対象範囲は `scripts/run-clang-tidy.sh` で**一元管理**されています。
`make tidy`、`pre-commit-check.sh`、CI はすべてこのスクリプトを呼び出します。

| ディレクトリ | 対象 | 理由 |
|------------|------|------|
| `libs/` | ✅ 対象 | プロダクションコード |
| `tools/` | ✅ 対象 | CLI ツール |
| `include/` | ✅ 対象 | 公開ヘッダ |
| `tests/` | ❌ 除外 | テストコードは柔軟性優先 |
| `libs/frontend_clang/` | ❌ 除外 | Clang AST API の制約で警告対応困難 |

```bash
# 全ファイルチェック
./scripts/run-clang-tidy.sh

# 変更ファイルのみ
./scripts/run-clang-tidy.sh --changed

# 対象ファイル一覧を確認
./scripts/run-clang-tidy.sh --list

# 特定ファイルのみ
./scripts/run-clang-tidy.sh libs/common/hash.cpp
```

### clang-format 対象範囲

clang-format の対象範囲は `scripts/run-clang-format.sh` で**一元管理**されています。

| ディレクトリ | 対象 | 理由 |
|------------|------|------|
| `libs/` | ✅ 対象 | プロダクションコード |
| `tools/` | ✅ 対象 | CLI ツール |
| `include/` | ✅ 対象 | 公開ヘッダ |
| `tests/` | ✅ 対象 | テストもフォーマット統一 |

```bash
# 全ファイルをチェック
./scripts/run-clang-format.sh --check

# 全ファイルを修正
./scripts/run-clang-format.sh --fix

# 変更ファイルのみチェック
./scripts/run-clang-format.sh --changed --check

# 対象ファイル一覧を確認
./scripts/run-clang-format.sh --list
```

### 共通ライブラリ

品質ゲートスクリプトは `scripts/lib/common.sh` で共通設定・関数を一元管理しています：

- 対象ディレクトリ設定 (`SAPPP_SOURCE_DIRS`, `SAPPP_TEST_DIR`)
- 除外パターン (`SAPPP_TIDY_EXCLUDE_PATTERNS`)
- ツール検出関数 (`detect_clang_format`, `detect_gcc14` など)
- 警告検出関数 (`has_compiler_warnings`, `print_compiler_warnings`)

対象範囲を変更する場合は `scripts/lib/common.sh` を編集してください。

---

## テスト

### テスト構成

| ディレクトリ | 内容 |
|------------|------|
| `tests/determinism/` | 決定性テスト（ハッシュ・ソート・パス正規化） |
| `tests/schemas/` | スキーマ検証テスト |
| `tests/validator/` | Validator テスト |
| `tests/po/` | PO 生成テスト |
| `tests/build_capture/` | ビルドキャプチャテスト |

### テスト実行

```bash
# 全テスト
ctest --test-dir build --output-on-failure

# 高速テスト（quick ラベルのみ）
ctest --test-dir build -L quick --output-on-failure

# quick ラベルの対象: schemas / determinism / build_capture / po / validator

# 決定性テストのみ
ctest --test-dir build -R determinism --output-on-failure

# 特定テスト
ctest --test-dir build -R ValidatorTest --output-on-failure

# 並列実行
ctest --test-dir build -j$(nproc) --output-on-failure
```

### テスト追加ガイドライン

1. **新機能には必ずテストを追加**
2. **決定性に関わる変更は決定性テストを追加**
3. **スキーマ変更時は検証テストを更新**

---

## カバレッジレポート

テストカバレッジを測定し、未テストのコードを特定できます。

### 基本的な使い方

```bash
# カバレッジビルド＆テスト＆HTMLレポート生成
./scripts/run-coverage.sh --html

# ブラウザで開く
./scripts/run-coverage.sh --html --open

# または Makefile から
make coverage         # HTMLレポート生成
make coverage-open    # ブラウザで開く
```

### オプション

| オプション | 説明 |
|-----------|------|
| `--html` | HTMLレポートを生成 |
| `--open` | レポート生成後にブラウザで開く |
| `--clean` | カバレッジデータをクリア |
| `--gcc` | GCC (gcov) を使用（デフォルト） |
| `--clang` | Clang (llvm-cov) を使用 |

### 必要なツール

- **GCC**: `gcov`, `lcov`, `genhtml`
  ```bash
  sudo apt install lcov
  ```
- **Clang**: `llvm-profdata`, `llvm-cov`（Clang パッケージに含まれる）

### 出力

- `build-coverage/coverage/coverage-filtered.info`: カバレッジデータ
- `build-coverage/coverage/report/index.html`: HTMLレポート

---

## ベンチマーク

性能回帰を検出するためのベンチマーク基盤を提供します。

### 基本的な使い方

```bash
# ベンチマーク実行
./scripts/run-benchmarks.sh

# ベースラインとして保存
./scripts/run-benchmarks.sh --baseline

# ベースラインと比較（回帰検出）
./scripts/run-benchmarks.sh --compare

# または Makefile から
make benchmarks           # 実行
make benchmarks-baseline  # ベースライン保存
make benchmarks-compare   # 比較
```

### オプション

| オプション | 説明 |
|-----------|------|
| `--baseline` | 結果をベースラインとして保存 |
| `--compare` | ベースラインと比較して回帰を検出 |
| `--json` | JSON形式で出力 |
| `--threshold=N` | 性能劣化の許容閾値（%、デフォルト: 10） |

### ベンチマーク項目

現在以下のベンチマークが含まれています：

- **Canonical JSON**: `canonicalize()` の性能（小/中/大規模JSON）
- **SHA256**: ハッシュ計算の性能（各種サイズ）
- **Canonical Hash**: カノニカル化 + SHA256 の複合性能

### 出力

- `.benchmarks/baseline.json`: ベースライン結果
- `.benchmarks/metadata.json`: ベースラインのメタデータ（コミットハッシュ等）

### 性能回帰ワークフロー

```bash
# 1. リリース前にベースラインを保存
make benchmarks-baseline

# 2. 変更後に比較
make benchmarks-compare

# 3. 10%以上の劣化があれば警告
#    閾値を変更する場合:
./scripts/run-benchmarks.sh --compare --threshold=5
```

---

## 品質ゲートスクリプトアーキテクチャ

### 設計原則: 単一ソース（Single Source of Truth）

品質ゲートの設定は `scripts/lib/common.sh` に集約されています。
これにより、ツールの対象範囲や検出ロジックが一箇所で管理され、
Makefile、pre-commit、CI 間での不整合を防止します。

```
scripts/
├── lib/
│   ├── common.sh          # 共通設定・関数（Single Source）
│   └── test-common.sh     # common.sh のセルフテスト
├── run-clang-format.sh    # clang-format 単一エントリポイント
├── run-clang-tidy.sh      # clang-tidy 単一エントリポイント
├── run-schema-validation.sh # スキーマ検証 単一エントリポイント
├── pre-commit-check.sh    # pre-commit フック（上記を呼び出し）
├── quick-check.sh         # 高速チェック（上記を呼び出し）
└── ...
```

### common.sh で定義される設定

| 変数/関数 | 説明 |
|-----------|------|
| `SAPPP_SOURCE_DIRS` | ソースコードディレクトリ（libs/ tools/ include/） |
| `SAPPP_TEST_DIR` | テストディレクトリ |
| `SAPPP_TIDY_EXCLUDE_PATTERNS` | clang-tidy 除外パターン |
| `SAPPP_WARNING_PATTERN` | コンパイラ警告検出パターン |
| `detect_clang_format()` | clang-format コマンド検出 |
| `detect_clang_tidy()` | clang-tidy コマンド検出 |
| `detect_gcc14()` | GCC 14 コマンド検出 |
| `detect_ajv()` | ajv-cli コマンド検出 |
| `get_build_jobs()` | 並列ビルド数取得 |
| `has_compiler_warnings()` | 警告検出関数 |

### 単一エントリポイントスクリプト

各品質ツールには専用のエントリポイントスクリプトがあります：

```bash
# clang-format
./scripts/run-clang-format.sh --check   # チェックのみ
./scripts/run-clang-format.sh --fix     # 自動修正
./scripts/run-clang-format.sh --changed # 変更ファイルのみ

# clang-tidy
./scripts/run-clang-tidy.sh             # 全対象
./scripts/run-clang-tidy.sh --changed   # 変更ファイルのみ

# スキーマ検証
./scripts/run-schema-validation.sh      # 全スキーマ
```

### セルフテスト

共通ライブラリの動作確認：

```bash
./scripts/lib/test-common.sh
# または
make test-scripts
```

---

## CI/CD

品質ゲートのプロファイル定義とコマンドは
`docs/QUALITY_GATE_STRATEGY.md` を正とします。

### 3段階ゲート

```
┌─────────────┐    ┌─────────────┐    ┌────────────────┐
│   L0: Quick │───▶│ L1: Commit  │───▶│ L2: Release/CI │
│  (quick)    │    │  (smart)    │    │  (full/ci)     │
└─────────────┘    └─────────────┘    └────────────────┘
    作業中           pre-commit         push前/CI
```

推奨するローカルゲート:

- L0: `./scripts/quick-check.sh`（最速）
- L1: `./scripts/pre-commit-check.sh --smart`（変更内容に応じて最小化）
- L2: `./scripts/pre-commit-check.sh`（full）または `./scripts/pre-commit-check.sh --ci`（CI互換）
- Docker: `./scripts/docker-ci.sh`（full）/ `./scripts/docker-ci.sh --ci`（CI互換）

### GitHub Actions ジョブ

| ジョブ | 内容 |
|-------|------|
| `quality-gate` | Docker で `docker-ci.sh` を実行（`SAPPP_CI_GATE_MODE` に従う） |

CI のゲートモードは `SAPPP_CI_GATE_MODE` で制御できます（`smart` / `full` / `ci`）。
workflow_dispatch からも同じ指定が可能です。

### ローカルでの CI 再現

```bash
# Docker で完全再現（推奨）
./scripts/docker-ci.sh --ci

# ローカル環境で実行（CI互換）
./scripts/pre-commit-check.sh --ci

# ローカルの full（CI相当だが skip 可能）
./scripts/pre-commit-check.sh

# pre-push のスタンプ確認（高速）
./scripts/pre-push-check.sh

pre-push hook は必須です（`make check-env` で未インストールはエラー）。
```

pre-commit hook はデフォルトで `smart` モードを使用します。必要に応じて環境変数で切り替え可能です:

```bash
SAPPP_PRE_COMMIT_MODE=quick  # 高速
SAPPP_PRE_COMMIT_MODE=smart  # 変更内容に応じて最小化（デフォルト）
SAPPP_PRE_COMMIT_MODE=full   # フルチェック
SAPPP_PRE_COMMIT_MODE=ci     # CI互換の厳格モード
```

---

## トラブルシューティング

### ビルドエラー

#### `fatal error: print: No such file or directory`

GCC 14 以上が必要です：
```bash
sudo apt install gcc-14 g++-14
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++-14 -DCMAKE_C_COMPILER=gcc-14
```

#### `std::views::enumerate` が見つからない

Clang + libc++ の組み合わせでは未実装です。libstdc++ を使用してください：
```bash
# Clang でも libstdc++ を使う（CI のデフォルト）
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++-19
```

### テストエラー

#### 決定性テストが失敗する

- `unordered_map/set` の反復順に依存していないか確認
- 出力配列が安定ソートされているか確認
- 浮動小数点を使用していないか確認

### Docker エラー

#### `Cannot connect to the Docker daemon`

```bash
# WSL2 の場合
sudo service docker start

# または systemd
sudo systemctl start docker
```

#### イメージビルドが遅い

キャッシュを活用：
```bash
# イメージが最新かどうかは自動判定されます
./scripts/docker-ci.sh
```

---

## 関連ドキュメント

- [CONTRIBUTING.md](../CONTRIBUTING.md) - 貢献ガイド
- [AGENTS.md](../AGENTS.md) - AI エージェント向けガイド
- [CODING_STYLE_CPP23.md](CODING_STYLE_CPP23.md) - コーディング規約
- [SAPpp_SRS_v1.1.md](SAPpp_SRS_v1.1.md) - 要求仕様書
- [SAPpp_Architecture_Design_v0.1.md](SAPpp_Architecture_Design_v0.1.md) - アーキテクチャ設計書
