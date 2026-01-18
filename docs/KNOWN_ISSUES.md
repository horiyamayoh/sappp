# SAP++ 既知の問題一覧

このファイルは 2026-01-18 に検出された問題をまとめたものです。
各問題を個別に調査・修正してください。

---

## 問題1: Clang 18 の `std::expected` 不整合

### ステータス: 🟢 解決（Clang 19 へ移行）

### 症状
Clang 18 でビルドすると `std::expected` が見つからずコンパイルエラー：
```
/home/dhuru/04_SAP++/include/sappp/common.hpp:36:21: error: no template named 'expected' in namespace 'std'
   36 | using Result = std::expected<T, Error>;
```

### 原因
- Clang 18 + libstdc++ の組み合わせで `std::expected` が有効化されない
- GCC 14 では問題なし

### 検証コマンド
```bash
# Clang 19 のバージョン確認
/usr/bin/clang++-19 --version | head -1

# GCC 14 のバージョン確認
/usr/bin/g++-14 --version | head -1
```

### 影響範囲
1. `pre-commit-check.sh` の Clang 18 ビルドが失敗
2. clang-tidy 解析が失敗（同じ Clang パーサを使用）
3. ローカル開発環境での Clang ビルド不可

### 対応
1. Clang 19 を必須にし、Clang 18 のサポートを終了
2. `std::expected` の互換ヘッダ/ワークアラウンドを削除
3. Clang 19 を前提にビルド/clang-tidy を実行

### 関連ファイル
- `/home/dhuru/04_SAP++/include/sappp/common.hpp`
- `/home/dhuru/04_SAP++/scripts/pre-commit-check.sh`
- `/home/dhuru/04_SAP++/CMakeLists.txt`
- `/home/dhuru/04_SAP++/.github/workflows/ci.yml`

---

## 問題2: テストの決定性問題

### ステータス: 🟢 解決

### 症状
`PoGeneratorTest.PoIdIsDeterministic` テストが失敗：
```
Expected equality of these values:
  first_id
    Which is: "sha256:4c5560d4261905392efebf84064b1085b9a7e42db96ed66d36e9f467f6d3af14"
  second_id
    Which is: "sha256:01e2a56d629bdbd62b09b9dc2df942222e992aa370cd6da38e5de3472c9ab89b"
```

### 原因/対応
1. テストが同一の一時ファイル（`sample.cpp`）を共有しており、
   `ctest -j` の並列実行時に内容が競合して `po_id` が揺れる。
2. `generated_at` が欠落している場合に現在時刻を使う実装があり、
   入力が同一でも出力が非決定的になる可能性があった。

対応:
- テストごとに固有のファイル名を使うように変更（タグ付きファイル名）。
- `po_generator.cpp` で `generated_at` のフォールバックを削除。

### 検証コマンド
```bash
# テストを単体で実行して詳細を確認
cd /home/dhuru/04_SAP++ && ./build/bin/test_po_generator --gtest_filter=PoGeneratorTest.PoIdIsDeterministic
```

### 対策案
1. `po_generator.cpp` の `generate()` 関数を調査
2. `po_id` 計算に使用される要素を特定
3. 非決定的要素を排除

### 関連ファイル
- `/home/dhuru/04_SAP++/libs/po/po_generator.cpp`
- `/home/dhuru/04_SAP++/libs/po/po_generator.hpp`
- `/home/dhuru/04_SAP++/tests/po/test_po_generator.cpp`

---

## 問題3: clang-tidy 設定の問題

### ステータス: 🟢 解決

### 修正済み項目
`.clang-tidy` で以下を修正：
- 外部ライブラリ（nlohmann/json）のマクロ警告を抑制（`-cppcoreguidelines-macro-usage`）
- `Error` 構造体の public メンバーに `m_` prefix を要求しない（`PublicMemberPrefix: ''`）
- ローカル const 変数に `k` prefix を要求しない（`LocalConstantPrefix: ''`）
- 以下のチェックを無効化：
  - `misc-include-cleaner`
  - `misc-const-correctness`
  - `performance-enum-size`
  - `readability-function-cognitive-complexity`

### 対応
- Clang 19 を前提に clang-tidy を実行。
- `pre-commit-check.sh` で Clang ビルドの `compile_commands.json` を優先使用。

### 関連ファイル
- `/home/dhuru/04_SAP++/.clang-tidy`

---

## 問題4: pre-commit-check.sh のスクリプト問題

### ステータス: 🟢 解決

### 対応
1. `CLANG_BUILD_DIR` を `BUILD_CLANG_DIR` から明示的に設定。
2. `__cpp_concepts` の数値比較は廃止し、Clang で
   `sappp/common.hpp` を実際にコンパイルして判定する方式へ変更。
3. clang-tidy では Clang ビルドの `compile_commands.json` を優先使用。

### 関連ファイル
- `/home/dhuru/04_SAP++/scripts/pre-commit-check.sh`

---

## 優先順位

全件対応済み。再発時は **問題2 → 問題1 → 問題4 → 問題3** の順で再確認する。

---

## 調査用チェックリスト

### 問題2の調査手順
- [x] `po_generator.cpp` の `generate()` 関数を読む
- [x] `po_id` の計算ロジックを特定
- [x] ハッシュに含まれる要素を列挙
- [x] `generated_at` が含まれているか確認
- [x] テストで使用される一時ファイルパスを確認
- [x] `generated_at` フォールバックを削除して決定性を担保

### 問題1の調査手順
- [x] Clang + libstdc++ の `std::expected` 利用可否を確認
- [x] Clang 19 へ移行して Clang 18 のサポートを終了

---

## 参考情報

### 環境情報
```
OS: Ubuntu (WSL2)
GCC: 14.2.0
Clang: 19.x
libstdc++: GCC 14 版
```

### AGENTS.md の関連セクション
- セクション 0.3: 決定性（並列でも同じ結果）を壊さない
- セクション 1.5: Codex 完了ゲート（必須）
- セクション 6.3: 決定性の手動チェック
