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
| Clang | 18.x | セカンダリコンパイラ |
| clangd | 18.x | LSP（コード補完・診断） |
| clang-format | 18.x | コードフォーマット |
| clang-tidy | 18.x | 静的解析 |
| CMake | 3.28+ | ビルドシステム |
| Ninja | 1.11+ | ビルドツール |
| ajv-cli | latest | JSONスキーマ検証 |

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

# フルチェック
./scripts/docker-ci.sh

# 高速チェック
./scripts/docker-ci.sh --quick

# インタラクティブシェル
./scripts/docker-ci.sh --shell
```

### ローカル環境構築（Ubuntu 24.04）

```bash
# 必須パッケージ
sudo apt update
sudo apt install -y \
    gcc-14 g++-14 \
    clang-18 clang-format-18 clang-tidy-18 clangd-18 \
    cmake ninja-build \
    nodejs npm

# ajv-cli（スキーマ検証）
sudo npm install -g ajv-cli ajv-formats

# Git hooks
./scripts/install-hooks.sh
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

### Makefile ターゲット

```bash
make help           # ヘルプ表示
make build          # ビルド
make test           # テスト実行
make clean          # クリーン
make format         # フォーマット適用
make format-check   # フォーマットチェック
make tidy           # clang-tidy 実行
```

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

## CI/CD

### 3段階ゲート

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│   L1: Quick │───▶│  L2: CI     │───▶│ L3: Remote  │
│  (30秒以内) │    │ (ローカル)  │    │  (GitHub)   │
└─────────────┘    └─────────────┘    └─────────────┘
   pre-commit         push前            push後
```

### GitHub Actions ジョブ

| ジョブ | 内容 |
|-------|------|
| `build-gcc` | GCC 14 でビルド＋テスト |
| `build-clang` | Clang 18 でビルド＋テスト |
| `format-check` | clang-format チェック |
| `tidy-check` | clang-tidy チェック |
| `schema-check` | JSON Schema 検証 |

### ローカルでの CI 再現

```bash
# Docker で完全再現（推奨）
./scripts/docker-ci.sh

# ローカル環境で実行
./scripts/pre-commit-check.sh
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
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++-18
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
