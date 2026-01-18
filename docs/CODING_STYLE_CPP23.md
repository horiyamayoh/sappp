# SAP++ C++23 Coding Style

本書は SAP++（Sound, Static Absence‑Proving Analyzer for C++）の **C++23** コーディング規約です。  
**Google C++ Style Guide** を下敷きに、SAP++ 固有の要件（決定性・再現性・スキーマ遵守・TCB最小化）を加えています。

> **対象読者**: AI コーディングエージェント、および人間の開発者  
> **強制レベル**: MUST = 必須（違反は即リジェクト）、SHOULD = 推奨、MAY = 任意

---

## 目次

1. [最上位原則](#1-最上位原則must)
2. [C++23 必須機能](#2-c23-必須機能must)
3. [命名規約](#3-命名規約must)
4. [フォーマット](#4-フォーマットmust)
5. [ヘッダとインクルード](#5-ヘッダとインクルードmust)
6. [所有権とメモリ安全](#6-所有権とメモリ安全must)
7. [エラー処理](#7-エラー処理must)
8. [決定性ルール](#8-決定性ルール最重要must)
9. [整数・境界・UB回避](#9-整数境界ub回避must)
10. [並行性](#10-並行性mustshould)
11. [JSON・スキーマ実装](#11-jsonスキーマ実装must)
12. [テスト](#12-テストmustshould)
13. [コメント・ドキュメント](#13-コメントドキュメントshould)
14. [AIエージェント運用ルール](#14-aiエージェント運用ルールmust)

---

## 1. 最上位原則（MUST）

### 1.1 嘘 SAFE / 嘘 BUG を出さない

- Analyzer は候補を作るだけ。SAFE/BUG は **Validator が通った場合のみ確定**。
- 検証不能・不一致・未対応は必ず **UNKNOWN** に降格。
- 「仕様に書いていない推論（LLM 由来を含む）」を SAFE/BUG の根拠に混ぜない。

### 1.2 決定性（REQ-REP-002）

- 同一入力・同一設定・同一バージョンで、結果カテゴリと ID が一致すること。
- 並列化しても同じ結果になる設計にする（「並列計算 → 単一スレッドで安定マージ」）。

### 1.3 スキーマ駆動

- JSON は常にスキーマ（`schemas/*.schema.json`）に一致させる。
- フィールド追加・意味変更・ID 規則変更は、スキーマ更新＋バージョン管理が前提。

---

## 2. C++23 必須機能（MUST）

本プロジェクトは **C++23 を全面採用**（GCC 14+ / Clang 18+）。  
以下の機能は「使える場面では必ず使う」こと。旧スタイルはレビューで指摘対象。

### 2.1 必須機能一覧

| 機能 | 用途 | 必須度 | 旧スタイル（禁止） |
|-----|------|--------|------------------|
| `std::print` / `std::println` | コンソール出力 | **MUST** | `std::cout <<` |
| `std::expected<T, E>` | エラーハンドリング | **MUST** | 例外スロー |
| `std::views::enumerate` | インデックス付きループ | **MUST** | `for (size_t i = 0; ...)` |
| `std::views::zip` | 複数範囲の同時走査 | SHOULD | 手動インデックス |
| `std::ranges::to<Container>` | パイプライン→コンテナ | SHOULD | 手動変換 |
| `std::rotr` / `std::rotl` | ビット回転 | **MUST** | 手書きビット操作 |
| `std::byteswap` | エンディアン変換 | **MUST** | 手書きシフト |
| `std::to_underlying` | enum→整数 | **MUST** | `static_cast<int>` |
| `std::unreachable()` | 到達不能マーク | SHOULD | `assert(false)` |
| `[[nodiscard]]` | 戻り値無視防止 | **MUST** | （なし） |
| `[[maybe_unused]]` | 意図的未使用 | SHOULD | （なし） |
| `constexpr` 拡張 | コンパイル時計算 | SHOULD | 実行時計算 |

### 2.2 禁止パターンと正しい書き方

#### 出力

```cpp
// ❌ 禁止
std::cout << "message" << std::endl;
std::cerr << "error: " << msg << "\n";

// ✅ 必須
std::println("message");
std::println(stderr, "error: {}", msg);
```

#### インデックス付きループ

```cpp
// ❌ 禁止
for (size_t i = 0; i < vec.size(); ++i) {
    process(i, vec[i]);
}

// ✅ 必須
for (auto [i, elem] : std::views::enumerate(vec)) {
    process(i, elem);
}
```

#### ビット操作

```cpp
// ❌ 禁止: 手書きビット回転
uint32_t rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

// ✅ 必須
auto result = std::rotr(x, n);
```

#### エンディアン変換

```cpp
// ❌ 禁止
uint32_t be_to_native(const uint8_t* p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

// ✅ 必須
uint32_t val;
std::memcpy(&val, p, 4);
if constexpr (std::endian::native == std::endian::little) {
    val = std::byteswap(val);
}
```

#### enum 変換

```cpp
// ❌ 非推奨
auto value = static_cast<int>(my_enum);

// ✅ 必須
auto value = std::to_underlying(my_enum);
```

### 2.3 必須ヘッダ

```cpp
#include <bit>        // std::rotr, std::byteswap, std::endian
#include <expected>   // std::expected
#include <print>      // std::print, std::println
#include <ranges>     // std::views::enumerate, etc.
#include <utility>    // std::to_underlying, std::unreachable
```

---

## 3. 命名規約（MUST）

Google C++ Style Guide に準拠し、以下を採用。

### 3.1 識別子

| 種別 | スタイル | 例 |
|-----|---------|-----|
| namespace | `snake_case` | `sappp::validator` |
| 型（class/struct/enum） | `PascalCase` | `PoGenerator`, `ErrorCode` |
| 関数/メソッド | `snake_case` | `validate_certificate()` |
| 変数 | `snake_case` | `po_count`, `file_path` |
| メンバ変数 | `m_` + `snake_case` | `m_config`, `m_items` |
| 定数（`const`/`constexpr`） | `k` + `PascalCase` | `kMaxRetries`, `kDefaultTimeout` |
| 列挙子 | `k` + `PascalCase` | `kSuccess`, `kNotFound` |
| マクロ | `UPPER_SNAKE_CASE` | `SAPPP_VERSION` |
| テンプレートパラメータ | `PascalCase` | `typename Container` |

### 3.2 ファイル

| 種別 | スタイル | 例 |
|-----|---------|-----|
| ヘッダ | `snake_case.h` | `po_generator.h` |
| 実装 | `snake_case.cpp` | `po_generator.cpp` |
| テスト | `test_snake_case.cpp` | `test_po_generator.cpp` |

### 3.3 名前空間

```cpp
namespace sappp {
namespace validator {
// ...
}  // namespace validator
}  // namespace sappp

// または C++17 以降
namespace sappp::validator {
// ...
}  // namespace sappp::validator
```

- 内部実装用: `sappp::detail`

---

## 4. フォーマット（MUST）

`.clang-format` を正とする。手整形で差分を増やさない。

### 4.1 基本設定

| 項目 | 設定 |
|-----|-----|
| インデント | **4スペース** |
| タブ | 使用禁止 |
| 1行最大 | 100文字 |
| 波括弧 | 関数・クラスは次行、制御文は同一行（K&R 変形） |

### 4.2 波括弧スタイル

```cpp
// 関数・クラス: 次行
class MyClass
{
public:
    void do_something()
    {
        // ...
    }
};

// 制御文: 同一行
if (condition) {
    // ...
} else {
    // ...
}

for (auto& item : items) {
    // ...
}
```

### 4.3 ポインタ・参照

```cpp
// ✅ 正しい（型に寄せる）
int* ptr;
const std::string& ref;

// ❌ 誤り
int *ptr;
const std::string &ref;
```

---

## 5. ヘッダとインクルード（MUST）

### 5.1 インクルードガード

```cpp
// ✅ 推奨
#pragma once

// ✅ 許容（従来スタイル）
#ifndef SAPPP_MODULE_NAME_H
#define SAPPP_MODULE_NAME_H
// ...
#endif  // SAPPP_MODULE_NAME_H
```

### 5.2 インクルード順序

1. 対応する `.h`（実装ファイルの場合）
2. C 標準ライブラリ（`<cstdint>` など）
3. C++ 標準ライブラリ（`<vector>`, `<string>` など）
4. サードパーティ（`<nlohmann/json.hpp>` など）
5. プロジェクトヘッダ（`<sappp/...>` または `"..."`)

```cpp
// po_generator.cpp
#include "po_generator.h"

#include <cstdint>

#include <algorithm>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <sappp/common/error.h>
#include <sappp/ir/nir.h>
```

---

## 6. 所有権とメモリ安全（MUST）

### 6.1 禁止事項

| 禁止 | 代替 |
|-----|-----|
| 所有を表す生ポインタ | `std::unique_ptr` |
| `new` / `delete` | `std::make_unique` |
| `malloc` / `free` | 標準コンテナ |
| 安易な `std::shared_ptr` | `std::unique_ptr`（共有が本質の場合のみ `shared_ptr`） |

### 6.2 ポインタの意味

```cpp
// 所有権あり
std::unique_ptr<Resource> m_resource;

// 借用（nullable）
Resource* borrowed;  // 所有しない、nullチェック必須

// 借用（non-null）
Resource& ref;       // 所有しない、常に有効
```

### 6.3 `string_view` / `span` の注意

```cpp
// ❌ 危険: 寿命を越えて保持
class Bad {
    std::string_view m_name;  // 参照先が消えたら未定義動作
};

// ✅ 安全: 所有型にコピー
class Good {
    std::string m_name;
};
```

### 6.4 Rule of 0/5

```cpp
// ✅ 推奨: Rule of 0（スマートポインタに任せる）
class Resource {
    std::unique_ptr<Impl> m_impl;
    // 特殊メンバ関数は書かない → コンパイラ生成に任せる
};

// ✅ 必要な場合: 明示的に = default / = delete
class NonCopyable {
public:
    NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
    NonCopyable(NonCopyable&&) = default;
    NonCopyable& operator=(NonCopyable&&) = default;
};
```

---

## 7. エラー処理（MUST）

### 7.1 基本方針

- ライブラリ層は **例外を投げない**
- エラーは `std::expected<T, Error>` で返す
- CLI の `main()` は最上位で catch して終了コードへ変換

### 7.2 Error 型

```cpp
namespace sappp {

enum class ErrorCode {
    kSuccess = 0,
    kIoError,
    kSchemaInvalid,
    kVersionMismatch,
    kBudgetExceeded,
    kInternalError,
};

struct Error {
    ErrorCode code;
    std::string message;
    std::string context;  // path, po_id など

    static Error make(ErrorCode code, std::string_view msg) {
        return Error{code, std::string(msg), {}};
    }

    static Error make(ErrorCode code, std::string_view msg, std::string_view ctx) {
        return Error{code, std::string(msg), std::string(ctx)};
    }
};

template <typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;

}  // namespace sappp
```

### 7.3 使用例

```cpp
Result<Config> load_config(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return std::unexpected(Error::make(
            ErrorCode::kIoError,
            std::format("Failed to open: {}", path.string())
        ));
    }
    // ...
    return config;
}

// 呼び出し側
auto result = load_config(path);
if (!result) {
    std::println(stderr, "Error: {}", result.error().message);
    return 1;
}
use(*result);
```

---

## 8. 決定性ルール（最重要）（MUST）

### 8.1 順序の決定性

```cpp
// ❌ 危険: 反復順が不定
std::unordered_map<std::string, int> map;
for (const auto& [key, value] : map) {  // 順序が実行ごとに変わりうる
    output.push_back(key);
}

// ✅ 安全: キーでソート
std::vector<std::pair<std::string, int>> sorted(map.begin(), map.end());
std::ranges::sort(sorted, {}, &std::pair<std::string, int>::first);
for (const auto& [key, value] : sorted) {
    output.push_back(key);
}
```

### 8.2 安定ソート規約

出力配列は仕様で定めたキーで **安定ソート** すること：

| 出力 | ソートキー |
|-----|----------|
| `po.v1.items[]` | `po_id` 昇順 |
| `unknown.v1.items[]` | `unknown_stable_id` 昇順 |
| `validated_results.v1.results[]` | `po_id` 昇順 |
| `nir.v1.functions[]` | `function_uid` 昇順 |

```cpp
std::ranges::stable_sort(items, {}, &Item::id);
```

### 8.3 ハッシュの決定性

```cpp
// ❌ 禁止: std::hash は実装依存
auto h = std::hash<std::string>{}(s);

// ✅ 必須: プロジェクト定義のハッシュ（SHA-256など）
auto h = sappp::canonical::sha256(canonical_json);
```

### 8.4 禁止事項

- `std::hash` を ID/ハッシュに使用
- ハッシュ対象に浮動小数点を含める
- 乱数を使用（使う場合は seed を固定し入力に含める）
- 時刻・PID・スレッドID・アドレスを決定性データに混入

---

## 9. 整数・境界・UB回避（MUST）

### 9.1 整数型

```cpp
// ✅ 推奨: 固定幅型
std::int32_t count;
std::uint64_t hash;

// ✅ サイズ/インデックス
std::size_t index;
```

### 9.2 符号混在の比較

```cpp
// ❌ 危険
if (signed_val < unsigned_val) { ... }

// ✅ 明示的キャスト
if (static_cast<std::size_t>(signed_val) < unsigned_val) { ... }
// または比較前に範囲チェック
```

### 9.3 禁止事項

- `reinterpret_cast`（必要なら `std::bit_cast` を使用）
- ポインタ演算（`std::span` を使用）
- C スタイルキャスト

---

## 10. 並行性（MUST/SHOULD）

### 10.1 基本方針

```cpp
// ✅ 推奨: std::jthread
std::jthread worker([](std::stop_token st) {
    while (!st.stop_requested()) {
        // ...
    }
});
```

### 10.2 決定性との両立

```cpp
// 並列計算 → 単一スレッドでマージ
std::vector<Result> results(num_threads);

// 並列実行
std::vector<std::jthread> threads;
for (auto [i, chunk] : std::views::enumerate(chunks)) {
    threads.emplace_back([&results, i, chunk] {
        results[i] = process(chunk);
    });
}
threads.clear();  // join

// 単一スレッドで安定マージ
std::vector<Item> merged;
for (const auto& r : results) {
    merged.insert(merged.end(), r.items.begin(), r.items.end());
}
std::ranges::stable_sort(merged, {}, &Item::id);
```

---

## 11. JSON・スキーマ実装（MUST）

### 11.1 スキーマ準拠

- 生成する JSON は `schemas/*.schema.json` に一致させる
- 保存前・読み込み後にスキーマ検証を行う

### 11.2 Canonical JSON（v1運用規約）

| 項目 | ルール |
|-----|-------|
| 文字コード | UTF-8 |
| オブジェクトキー | 辞書順 |
| 配列 | 仕様キーで安定ソート |
| 数値 | **整数のみ**（浮動小数禁止） |
| 空白 | 最小表現（ハッシュ対象） |

---

## 12. テスト（MUST/SHOULD）

### 12.1 必須テスト

- **決定性テスト**: `--jobs=1` と `--jobs=N` で結果一致
- **スキーマ検証テスト**: 出力が schema に適合

### 12.2 テスト命名

```cpp
TEST(PoGenerator, GeneratesValidPoIds) { ... }
TEST(Validator, RejectsInvalidCertificate) { ... }
```

---

## 13. コメント・ドキュメント（SHOULD）

### 13.1 コメントスタイル

```cpp
// 単一行コメント（推奨）

/*
 * 複数行コメント
 * （必要な場合のみ）
 */

/// Doxygen スタイル（公開 API）
/// @param path 設定ファイルのパス
/// @return 成功時は Config、失敗時は Error
Result<Config> load_config(const std::filesystem::path& path);
```

### 13.2 TODO

```cpp
// TODO(SAPpp#123): バッファオーバーフロー対策を追加
// TODO(username): 一時的な回避策、後で修正
```

---

## 14. AIエージェント運用ルール（MUST）

### 14.1 変更前チェックリスト

- [ ] 変更がスキーマ/ID/決定性に影響するか評価した
- [ ] 出力配列はキーで安定ソートされている
- [ ] `unordered_*` の順序に依存していない
- [ ] `std::hash` を ID/ハッシュに使っていない
- [ ] ハッシュ対象に浮動小数を入れていない
- [ ] 例外はモジュール境界で `expected` に変換されている
- [ ] C++23 必須機能を使っている（`std::print`, `enumerate` など）
- [ ] テスト（特に determinism）が追加/更新されている

### 14.2 禁止事項

- 証拠・検証ロジックへ LLM の推論結果を混ぜる
- スキーマ外フィールドを勝手に出力する
- 既存の ID 生成規則を黙って変更する（ADR/バージョン更新必須）
- "ついで変更"（1 PR = 1 目的）

### 14.3 完了条件

```bash
# ビルド（警告ゼロ）
cmake --build build --parallel 2>&1 | grep -i warning

# テスト（全パス）
ctest --test-dir build --output-on-failure

# 決定性テスト
ctest --test-dir build -R determinism
```

---

## 付録: クイックリファレンス

### A. 必須 C++23 機能

```cpp
#include <bit>       // std::rotr, std::byteswap
#include <expected>  // std::expected
#include <print>     // std::print, std::println
#include <ranges>    // std::views::enumerate
#include <utility>   // std::to_underlying
```

### B. Result 型テンプレート

```cpp
namespace sappp {
template <typename T>
using Result = std::expected<T, Error>;
using VoidResult = std::expected<void, Error>;
}
```

### C. 決定性マージパターン

```cpp
std::vector<Item> merged;
merged.reserve(a.size() + b.size());
std::ranges::copy(a, std::back_inserter(merged));
std::ranges::copy(b, std::back_inserter(merged));
std::ranges::stable_sort(merged, {}, &Item::id);
```
