# Contributing to SAP++

SAP++ への貢献ガイドです。開発を始める前に必ずお読みください。

## 🚀 クイックスタート

### 方法1: Dev Container（推奨）

最も簡単で確実な方法です。CI と完全同一の環境が即座に使えます。

1. **前提条件**
   - [VS Code](https://code.visualstudio.com/)
   - [Docker](https://docs.docker.com/get-docker/)
   - [Dev Containers 拡張機能](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)

2. **開始手順**
   ```bash
   git clone https://github.com/horiyamayoh/sappp.git
   cd sappp
   code .
   ```

3. VS Code で **F1** → **「Dev Containers: Reopen in Container」**

4. 自動的に以下が実行されます：
   - Docker イメージのビルド
   - 開発ツールのインストール（GCC 14, Clang 19, clangd, etc.）
   - CMake 設定とビルド
   - Git hooks のインストール

### 方法2: Docker CI（ローカル環境を汚さない）

Docker だけで CI と同等のチェックを実行できます。

```bash
# フルチェック（コミット前 / CI再現）
./scripts/docker-ci.sh --ci

# 高速チェック（作業中）
./scripts/docker-ci.sh --quick

# 変更内容に応じて最小化
./scripts/docker-ci.sh --smart

# デバッグ用シェル
./scripts/docker-ci.sh --shell
```

### 方法3: ローカル環境（上級者向け）

手動で環境を構築する場合：

```bash
# Ubuntu 24.04 LTS
sudo apt install gcc-14 g++-14 clang-19 clang-format-19 clang-tidy-19 cmake ninja-build ccache ripgrep

# ビルド
cmake -S . -B build -G Ninja \
    -DCMAKE_CXX_COMPILER=g++-14 \
    -DCMAKE_C_COMPILER=gcc-14 \
    -DSAPPP_BUILD_TESTS=ON \
    -DSAPPP_WERROR=ON
cmake --build build --parallel

# テスト
ctest --test-dir build --output-on-failure
```

CMake Presets を使う場合:

```bash
cmake --preset gcc-debug
cmake --build --preset gcc-debug
ctest --preset gcc-debug
```

---

## 📋 開発ワークフロー

### 1. Git Hooks のインストール（初回のみ）

```bash
make install-hooks
# または
./scripts/install-hooks.sh
```

これにより、コミット前にフルチェックが自動実行されます。

### 2. 開発サイクル

```bash
# コーディング...

# 高速チェック（30秒以内、任意）
make quick

# 変更内容に応じたチェック（任意）
make smart

# コミット（pre-commit hook が smart チェック + スタンプ保存）
git commit -m "変更内容"

# 必要ならDockerでCI互換チェック（CI再現）
make docker-ci

# プッシュ（pre-push hook はスタンプ確認のみ）
git push
```

オプション（パフォーマンス/再現性）:

```bash
# 並列度と ccache を明示
SAPPP_BUILD_JOBS=8 SAPPP_USE_CCACHE=1 make quick
```

### 3. 利用可能なコマンド

```bash
make help           # コマンド一覧
make quick          # 高速チェック（作業中）
make smart          # 変更内容に応じたチェック
make ci             # フルCIチェック（コミット前）
make docker-ci      # Docker環境でフルCIチェック
make build          # ビルドのみ
make test           # テストのみ
make format         # clang-format 適用
make tidy           # clang-tidy 実行
```

---

## ✅ 品質ゲート

すべての変更は以下のゲートを通過する必要があります：

| ゲート | タイミング | 内容 |
|-------|----------|------|
| **L0: Quick** | 任意（作業中） | format + build + quick test（30秒以内） |
| **L1: Commit Gate** | pre-commit | 変更内容に応じた build/test/tidy/スキーマ検証 |
| **L2: Remote CI** | push後 | GCC/Clang マトリクス + determinism + tidy + スキーマ検証 |

### 必須要件

- ✅ **ビルド通過**（警告ゼロ）
- ✅ **全テスト通過**
- ✅ **決定性テスト通過**
- ✅ **clang-format 準拠**
- ✅ **スキーマ検証通過**

---

## 📝 コーディング規約

詳細は [AGENTS.md](AGENTS.md) セクション8 および [docs/CODING_STYLE_CPP23.md](docs/CODING_STYLE_CPP23.md) を参照。

### 要点

- **C++23 必須**（GCC 14+ / Clang 19+）
- `std::print` を使用（`std::cout` 禁止）
- `std::expected` でエラー処理（例外禁止）
- 命名: 型は `PascalCase`、関数/変数は `snake_case`
- コミットメッセージは日本語

---

## 🐛 トラブルシューティング

### Docker が動かない（WSL2）

```bash
# Docker Engine をインストール
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker $USER
sudo service docker start
newgrp docker
```

### ビルドエラー: `<print>` が見つからない

GCC 14 以上が必要です：
```bash
sudo apt install gcc-14 g++-14
```

### pre-commit hook をスキップしたい

緊急時のみ：
```bash
SKIP_PRE_COMMIT=1 git commit -m "緊急修正"
```

速度優先で pre-commit を Quick にする場合：
```bash
SAPPP_PRE_COMMIT_MODE=quick git commit -m "変更内容"
```

pre-push の動作を切り替えたい場合：
```bash
# スタンプ確認のみ（デフォルト）
SAPPP_PRE_PUSH_MODE=stamp git push

# 完全に無効化
SAPPP_PRE_PUSH_MODE=off git push
```

---

## 📚 関連ドキュメント

- [AGENTS.md](AGENTS.md) - AI エージェント向け必須ガイド
- [README.md](README.md) - プロジェクト概要
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) - 詳細開発ガイド
- [docs/CODING_STYLE_CPP23.md](docs/CODING_STYLE_CPP23.md) - コーディング規約
