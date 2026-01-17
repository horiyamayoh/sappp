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

## ビルド

### 必要環境

- C++23対応コンパイラ (GCC 13+ / Clang 17+)
- CMake 3.16+
- LLVM/Clang (libTooling)
- nlohmann/json

### ビルド手順

```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

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

- [要求仕様書 (SRS)](docs/SAPpp_SRS_v1.1.md)
- [アーキテクチャ設計書 (SAD)](docs/SAPpp_Architecture_Design_v0.1.md)
- [詳細設計書 (DDD)](docs/SAPpp_Detailed_Design_v0.1.md)
- [実装指示書](docs/SAPpp_Implementation_Directive_v0.1.md)

## ライセンス

Private / Proprietary
