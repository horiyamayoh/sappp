# SAP++ (Sound, Static Absence-Proving Analyzer for C++)

静的解析によりC/C++コードの安全性を証明する解析器。

## 概要

SAP++は、生成AIが出力するコードの品質を**静的解析のみ**で保証することを目的とした解析ツールです。

### コア原則

- **嘘SAFEゼロ / 嘘BUGゼロ**: 確定した結論は常に真
- **Proof-Carrying Results**: SAFE/BUGは必ず検証可能な証拠を伴う
- **Validator確定**: SAFE/BUGはValidatorが検証した場合のみ確定
- **UNKNOWN開拓性**: UNKNOWNは「不足理由＋不足補題＋開拓計画」を含む
- **決定性**: 同一入力・同一設定で結果は常に同一

## 🚀 クイックスタート

### 方法1: Dev Container（推奨）

VS Code + Docker で最も簡単に開発を始められます。

```bash
git clone https://github.com/horiyamayoh/sappp.git
cd sappp
code .
# VS Code で F1 → 「Dev Containers: Reopen in Container」
```

### 方法2: Docker CI

```bash
git clone https://github.com/horiyamayoh/sappp.git
cd sappp
./scripts/docker-ci.sh --quick  # 高速チェック
./scripts/docker-ci.sh          # フルチェック
```

### 方法3: ローカルビルド

```bash
# Ubuntu 24.04 LTS
sudo apt install gcc-14 g++-14 cmake ninja-build

cmake -S . -B build -G Ninja \
    -DCMAKE_CXX_COMPILER=g++-14 \
    -DCMAKE_C_COMPILER=gcc-14 \
    -DSAPPP_BUILD_TESTS=ON \
    -DSAPPP_WERROR=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## 開発環境

## 開発環境

| 方法 | 特徴 | 推奨度 |
|-----|------|-------|
| **Dev Container** | CI と完全同一環境、VS Code 統合 | ⭐⭐⭐ |
| **Docker CI** | ローカル環境を汚さない、CI 再現 | ⭐⭐⭐ |
| **ローカル** | 高速だが環境差異のリスク | ⭐⭐ |

### 開発コマンド

```bash
make help           # コマンド一覧
make quick          # 高速チェック（作業中）
make ci             # ローカルフルチェック（コミット前）
make docker-ci      # Docker CI（フルチェック）
make install-hooks  # Git hooks インストール
```

詳細は [CONTRIBUTING.md](CONTRIBUTING.md) を参照してください。

## ビルド

### 必要環境

- **C++23対応コンパイラ**:
  - GCC 14+ (推奨) - `<print>` ヘッダが必要
  - Clang 19+
- CMake 3.16+
- LLVM/Clang (libTooling) - `frontend_clang` ビルド時
- nlohmann/json (自動取得)

### ビルド手順

```bash
# Ubuntu 24.04 LTS では GCC 14 をインストール
sudo apt install gcc-14 g++-14

# ビルド (GCC 14 を明示)
cmake -S . -B build \
    -DCMAKE_CXX_COMPILER=g++-14 \
    -DCMAKE_C_COMPILER=gcc-14 \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSAPPP_BUILD_TESTS=ON \
    -DSAPPP_WERROR=ON
cmake --build build --parallel

# テスト実行
ctest --test-dir build --output-on-failure
```

### コーディングスタイル

本プロジェクトは **C++23 を全面採用** しています。
詳細は [AGENTS.md](AGENTS.md) のセクション8「コーディング規約」を参照してください。

主な C++23 機能:
- `std::print` / `std::println` - コンソール出力（`std::cout` は禁止）
- `std::expected` - エラーハンドリング
- `std::views::enumerate` - インデックス付きループ
- `std::rotr` / `std::byteswap` - ビット操作

## 使い方

```bash
# ビルド条件をキャプチャ
sappp capture --compile-commands build/compile_commands.json -o out/

# 解析実行
sappp analyze --snapshot out/build_snapshot.json -o out/

# 検証（SAFE/BUG確定）
sappp validate --input out/ -o out/validated_results.json

# 再現パック生成
sappp pack --input out/ -o out/pack.tar.gz

# 差分比較
sappp diff --before before/validated_results.json --after out/validated_results.json
```

## プロジェクト構造

```
sappp/
├── docs/           # 設計書・ADR
├── schemas/        # JSON Schema定義
├── tools/sappp/    # CLIツール
├── libs/           # コアライブラリ
│   ├── common/     # 共通ユーティリティ (hash, path, sort)
│   ├── canonical/  # Canonical JSON serializer
│   ├── build_capture/
│   ├── frontend_clang/
│   ├── ir/
│   ├── po/
│   ├── analyzer/
│   ├── certstore/
│   ├── validator/
│   └── report/
├── tests/          # テスト
└── third_party/    # 外部依存
```

## ドキュメント

- [CONTRIBUTING.md](CONTRIBUTING.md) - 開発を始める方へ
- [AGENTS.md](AGENTS.md) - AI エージェント向けガイド
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) - 詳細開発ガイド
- [要求仕様書 (SRS)](docs/SAPpp_SRS_v1.1.md)
- [アーキテクチャ設計書 (SAD)](docs/SAPpp_Architecture_Design_v0.1.md)
- [詳細設計書 (DDD)](docs/SAPpp_Detailed_Design_v0.1.md)
- [実装指示書](docs/SAPpp_Implementation_Directive_v0.1.md)

## ライセンス

Private / Proprietary
