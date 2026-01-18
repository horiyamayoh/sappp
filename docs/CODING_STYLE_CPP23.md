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

本プロジェクトは **C++23 を全面採用**（GCC 14+ / Clang 19+）。  
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

### 2.4 Feature-Test Macro によるビルド時検証（SHOULD）

C++23 機能の利用可否は標準ライブラリ実装によって異なるため、`<version>` の feature-test macro で検出し、欠けている場合はビルドを失敗させることを推奨します。

```cpp
// sappp/compat/require_cpp23.h
#pragma once
#include <version>

#if !defined(__cpp_lib_print) || __cpp_lib_print < 202207L
#error "SAP++ requires std::print/std::println (__cpp_lib_print >= 202207L)."
#endif

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "SAP++ requires std::expected (__cpp_lib_expected >= 202202L)."
#endif

#if !defined(__cpp_lib_ranges_enumerate) || __cpp_lib_ranges_enumerate < 202302L
#error "SAP++ requires std::views::enumerate (__cpp_lib_ranges_enumerate >= 202302L)."
#endif

#if !defined(__cpp_lib_bitops) || __cpp_lib_bitops < 201907L
#error "SAP++ requires <bit> bit operations (__cpp_lib_bitops >= 201907L)."
#endif

#if !defined(__cpp_lib_byteswap) || __cpp_lib_byteswap < 202110L
#error "SAP++ requires std::byteswap (__cpp_lib_byteswap >= 202110L)."
#endif

#if !defined(__cpp_lib_to_underlying) || __cpp_lib_to_underlying < 202102L
#error "SAP++ requires std::to_underlying (__cpp_lib_to_underlying >= 202102L)."
#endif
```

**推奨する下限値:**

| 機能 | マクロ | 要求値 |
|-----|--------|--------|
| `std::print/println` | `__cpp_lib_print` | `>= 202207L` |
| `std::expected` | `__cpp_lib_expected` | `>= 202202L` |
| `std::views::enumerate` | `__cpp_lib_ranges_enumerate` | `>= 202302L` |
| `std::rotl/rotr` | `__cpp_lib_bitops` | `>= 201907L` |
| `std::byteswap` | `__cpp_lib_byteswap` | `>= 202110L` |
| `std::to_underlying` | `__cpp_lib_to_underlying` | `>= 202102L` |

---

## 3. 命名規約（MUST）

Google C++ Style Guide に準拠し、以下を採用。

### 3.1 識別子

| 種別 | スタイル | 例 |
|-----|---------|-----|
| namespace | `snake_case` | `sappp::validator` |
| 型（class/struct/enum） | `PascalCase` | `PoGenerator`, `ErrorInfo` |
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

### 5.3 依存追加時のルール（MUST）

新しい外部依存（ライブラリ、ツール）を追加する場合は、以下を README または ADR に記録すること:

| 項目 | 説明 |
|-----|------|
| **目的** | なぜこの依存が必要か |
| **代替案** | 検討した他の選択肢と採用しなかった理由 |
| **再現性・決定性への影響** | ビルド再現性、出力の決定性に悪影響がないか |
| **ライセンス** | 依存のライセンスと SAP++ との互換性 |

特に Validator / schema / hash 周りは **TCB（Trusted Computing Base）近傍**であり、依存追加には慎重を期すこと。

### 5.4 `auto` の使用指針（MUST/SHOULD）

`auto` は適切に使えばコードを簡潔にするが、型の意味が重要な場面では避ける。

#### 推奨される使用（SHOULD）

```cpp
// イテレータ（型名が長い）
auto it = container.find(key);

// ラムダ式
auto predicate = [](const Item& x) { return x.valid; };

// make_* 系（右辺で型が明確）
auto ptr = std::make_unique<Config>();

// 構造化束縛
auto [key, value] = *map.find(id);
```

#### 禁止される使用（MUST NOT）

```cpp
// ❌ 公開 API の戻り値型（テンプレートを除く）
auto load_config(const Path& p);  // 型が不明

// ✅ 明示的な型
Result<Config> load_config(const Path& p);
```

#### 具体型を書くべき場面（MUST）

型の意味が重要な境界では、`auto` を避けて具体型で書く:

- ID（`po_id`, `tu_id`, `function_uid` など）
- サイズ / インデックス（`std::size_t`, `std::int32_t`）
- ハッシュ値（`std::uint64_t`, `std::array<std::byte, 32>`）
- 座標 / 時刻 / 位置情報

```cpp
// ❌ 型の意味が不明
auto id = generate_po_id(...);
auto size = items.size();

// ✅ 意図が明確
std::string po_id = generate_po_id(...);
std::size_t size = items.size();
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

### 7.2 Error 型（実装に合わせて固定）

`Error.code` は **安定した文字列**（PascalCase推奨）とし、無秩序な追加を避ける。
新しいコードを増やす場合は、共通ヘッダへ集約するか ADR でルール化すること。

```cpp
namespace sappp {

struct Error {
    std::string code;     // Machine-readable error code
    std::string message;  // Human-readable error message

    [[nodiscard]] static Error make(std::string code, std::string message) {
        return Error{std::move(code), std::move(message)};
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
            "IoError",
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
| `po.v1.pos[]` | `po_id` 昇順 |
| `unknown.v1.unknowns[]` | `unknown_stable_id` 昇順 |
| `validated_results.v1.results[]` | `po_id` 昇順 |
| `nir.v1.functions[]` | `function_uid` 昇順 |

```cpp
std::ranges::stable_sort(items, {}, &Item::id);
```

`cert_index.v1` は 1ファイル=1PO。**列挙時は `po_id` 昇順**で安定化すること。

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

- `reinterpret_cast`（`std::bit_cast` / `std::as_bytes` / `std::to_integer` を使用）
- ポインタ演算（`std::span` を使用。`from_chars` など pointer ペア必須APIは
  `std::to_address(container.end())` で回避する）
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

ハッシュ対象の JSON は **必ず** `sappp::canonical::canonicalize()` でバイト列化し、
`sappp::canonical::hash_canonical()` でハッシュすること（`dump()` を直接使わない）。

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

### 14.3 Lint 抑制（MUST）

- `NOLINTNEXTLINE(<check>)` + **理由コメント**のみ許可
- `NOLINTBEGIN/END` は最小範囲で使用し、必ず理由を書く
- `NOLINT` だけの抑制・無理由の抑制は禁止

### 14.4 完了条件

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
### D. ファイル読み込みパターン（推奨）

```cpp
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

Result<std::string> read_file_contents(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(Error::make(
            "IoError",
            std::format("Failed to open file: {}", path.string())
        ));
    }
    
    std::string contents{
        std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{}
    };
    
    if (file.bad()) {
        return std::unexpected(Error::make(
            "IoError",
            std::format("Failed to read file: {}", path.string())
        ));
    }
    
    return contents;
}
```

### E. JSON 読み込み・書き込みパターン（推奨）

```cpp
#include <nlohmann/json.hpp>

#include <sappp/canonical_json.hpp>

Result<nlohmann::json> load_json(const std::filesystem::path& path) {
    auto contents = read_file_contents(path);
    if (!contents) {
        return std::unexpected(contents.error());
    }
    
    try {
        return nlohmann::json::parse(*contents);
    } catch (const nlohmann::json::parse_error& e) {
        return std::unexpected(Error::make(
            "SchemaInvalid",
            std::format("JSON parse error ({}): {}", path.string(), e.what())
        ));
    }
}

VoidResult save_json(const std::filesystem::path& path, const nlohmann::json& j) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(Error::make(
            "IoError",
            std::format("Failed to create file: {}", path.string())
        ));
    }
    
    auto canonical = sappp::canonical::canonicalize(j);
    if (!canonical) {
        return std::unexpected(canonical.error());
    }
    file << *canonical;
    
    if (!file) {
        return std::unexpected(Error::make(
            "IoError",
            std::format("Failed to write file: {}", path.string())
        ));
    }
    
    return {};
}
```

### F. エラー伝播パターン（推奨）

```cpp
// 基本: 早期リターン
Result<Output> process(const Input& input) {
    auto step1 = do_step1(input);
    if (!step1) {
        return std::unexpected(step1.error());
    }
    
    auto step2 = do_step2(*step1);
    if (!step2) {
        return std::unexpected(step2.error());
    }
    
    return Output{*step1, *step2};
}

// マクロを使う場合（オプション）
#define SAPPP_TRY(expr) \
    ({ \
        auto&& _result = (expr); \
        if (!_result) return std::unexpected(_result.error()); \
        std::move(*_result); \
    })

// 使用例
Result<Output> process(const Input& input) {
    auto v1 = SAPPP_TRY(do_step1(input));
    auto v2 = SAPPP_TRY(do_step2(v1));
    return Output{v1, v2};
}
```

---

## 付録2: 禁止パターンと理由

AIエージェントが「なぜダメか」を理解すると、応用が効きやすい。

### P1. `std::cout` / `std::cerr` の使用

```cpp
// ❌ 禁止
std::cout << "Processing: " << name << std::endl;

// ✅ 正しい
std::println("Processing: {}", name);
```

**理由**: `std::endl` はフラッシュを伴い性能劣化。`<<` 連鎖は型安全性が低い。

### P2. 手動インデックスループ

```cpp
// ❌ 禁止
for (size_t i = 0; i < items.size(); ++i) {
    process(i, items[i]);
}

// ✅ 正しい
for (auto [i, item] : std::views::enumerate(items)) {
    process(i, item);
}
```

**理由**: off-by-one エラーを防ぐ。符号混在の比較を避ける。意図が明確。

### P3. 例外を投げる関数

```cpp
// ❌ 禁止（ライブラリ内）
Config load_config(const Path& p) {
    if (!exists(p)) throw std::runtime_error("not found");
    // ...
}

// ✅ 正しい
Result<Config> load_config(const Path& p) {
    if (!exists(p)) {
        return std::unexpected(Error::make("IoError", "not found"));
    }
    // ...
}
```

**理由**: 例外は制御フローを不明瞭にする。`expected` は呼び出し側に処理を強制。

### P4. `std::hash` を ID/ハッシュに使用

```cpp
// ❌ 禁止
std::string generate_id(const Data& d) {
    return std::to_string(std::hash<std::string>{}(d.name));
}

// ✅ 正しい
std::string generate_id(const Data& d) {
    return sappp::canonical::sha256_hex(d.to_canonical_json());
}
```

**理由**: `std::hash` は実装依存。同じ入力でも環境によって結果が変わり、決定性が壊れる。

### P5. `unordered_map` の反復順に依存

```cpp
// ❌ 禁止
std::unordered_map<std::string, int> map;
for (const auto& [k, v] : map) {
    output.push_back(k);  // 順序が不定
}

// ✅ 正しい
std::vector<std::pair<std::string, int>> sorted(map.begin(), map.end());
std::ranges::sort(sorted, {}, &std::pair<std::string, int>::first);
for (const auto& [k, v] : sorted) {
    output.push_back(k);
}
```

**理由**: 反復順は実装依存。並列化時にも結果がブレる。

### P6. 浮動小数点をハッシュ対象に含める

```cpp
// ❌ 禁止
json j = {{"score", 0.95}};  // ハッシュ対象の場合
auto hash = sha256(j.dump());

// ✅ 正しい（整数スケーリング）
json j = {{"score_permille", 950}};  // 0.95 * 1000
auto hash = sha256(j.dump());
```

**理由**: 浮動小数点の文字列表現は環境依存（`0.95` vs `0.9500000000000001`）。

### P7. C スタイルキャスト

```cpp
// ❌ 禁止
int* p = (int*)void_ptr;

// ✅ 正しい（意図を明示）
auto* p = static_cast<int*>(void_ptr);     // 安全な変換
auto* p = reinterpret_cast<int*>(void_ptr); // 危険だが意図的
auto val = std::bit_cast<int>(float_val);   // ビット再解釈
```

**理由**: C キャストは何でも通すため、意図しない変換が起きる。

### P8. 所有を表す生ポインタ

```cpp
// ❌ 禁止
class Manager {
    Resource* m_resource;  // 誰が delete する？
public:
    Manager() : m_resource(new Resource()) {}
    ~Manager() { delete m_resource; }
};

// ✅ 正しい
class Manager {
    std::unique_ptr<Resource> m_resource;
public:
    Manager() : m_resource(std::make_unique<Resource>()) {}
    // デストラクタ不要（Rule of 0）
};
```

**理由**: 生ポインタはリーク/二重解放の温床。RAII で自動管理。

---

## 付録3: ファイル構成テンプレート

### ヘッダファイル（`.h`）

```cpp
// libs/module_name/include/sappp/module_name/component.h
#pragma once

#include <expected>
#include <string>
#include <vector>

#include <sappp/common/error.h>

namespace sappp::module_name {

/// @brief コンポーネントの簡潔な説明
///
/// 詳細な説明（必要に応じて）
class Component
{
public:
    /// @brief コンストラクタの説明
    /// @param config 設定オブジェクト
    explicit Component(const Config& config);

    // コピー禁止、ムーブ許可
    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;
    Component(Component&&) = default;
    Component& operator=(Component&&) = default;

    /// @brief 処理の説明
    /// @param input 入力データ
    /// @return 成功時は Output、失敗時は Error
    [[nodiscard]] Result<Output> process(const Input& input) const;

private:
    Config m_config;
    std::vector<Item> m_items;
};

}  // namespace sappp::module_name
```

### 実装ファイル（`.cpp`）

```cpp
// libs/module_name/component.cpp
#include "sappp/module_name/component.h"

#include <algorithm>
#include <format>

#include <sappp/common/logging.h>

namespace sappp::module_name {

Component::Component(const Config& config)
    : m_config(config)
{
    // 初期化ロジック
}

Result<Output> Component::process(const Input& input) const
{
    // 入力検証
    if (input.empty()) {
        return std::unexpected(Error::make(
            "InvalidInput",
            "Input cannot be empty"
        ));
    }

    // 処理ロジック
    std::vector<Item> results;
    results.reserve(input.size());

    for (const auto& [i, item] : std::views::enumerate(input)) {
        auto processed = process_item(item);
        if (!processed) {
            return std::unexpected(processed.error());
        }
        results.push_back(*processed);
    }

    // 決定性のため安定ソート
    std::ranges::stable_sort(results, {}, &Item::id);

    return Output{std::move(results)};
}

}  // namespace sappp::module_name
```

### テストファイル（`test_*.cpp`）

```cpp
// tests/module_name/test_component.cpp
#include <gtest/gtest.h>

#include <sappp/module_name/component.h>

namespace sappp::module_name {
namespace {

class ComponentTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_config = Config{/* ... */};
        m_component = std::make_unique<Component>(m_config);
    }

    Config m_config;
    std::unique_ptr<Component> m_component;
};

TEST_F(ComponentTest, ProcessesValidInput)
{
    Input input{/* ... */};
    
    auto result = m_component->process(input);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->items.size(), 3);
}

TEST_F(ComponentTest, RejectsEmptyInput)
{
    Input input{};
    
    auto result = m_component->process(input);
    
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, "InvalidInput");
}

TEST_F(ComponentTest, OutputIsDeterministic)
{
    Input input{/* ... */};
    
    auto result1 = m_component->process(input);
    auto result2 = m_component->process(input);
    
    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result1->items, result2->items);  // 順序も一致
}

}  // namespace
}  // namespace sappp::module_name
```

---

## 付録4: 自動ツール設定

### clang-format

プロジェクトルートの `.clang-format` で統一。手整形は禁止。

```bash
# 単一ファイル
clang-format -i path/to/file.cpp

# 全ファイル
find libs tests tools -name '*.cpp' -o -name '*.h' | xargs clang-format -i

# チェックのみ（CI用）
clang-format --dry-run --Werror path/to/file.cpp
```

### clang-tidy

プロジェクトルートの `.clang-tidy` で統一。

```bash
# 単一ファイル（compile_commands.json 必要）
clang-tidy -p build path/to/file.cpp

# 全ファイル
find libs -name '*.cpp' | xargs -P4 -I{} clang-tidy -p build {}

# CMake 統合（ビルド時に自動実行）
cmake -S . -B build -DCMAKE_CXX_CLANG_TIDY="clang-tidy"
```

### 主要な clang-tidy チェック

| チェック | 効果 |
|---------|------|
| `modernize-use-nullptr` | `NULL` → `nullptr` |
| `modernize-loop-convert` | C配列ループ → range-based |
| `modernize-use-auto` | 明らかな場合に `auto` 推奨 |
| `bugprone-use-after-move` | ムーブ後使用を検出 |
| `cppcoreguidelines-owning-memory` | 所有権違反を検出 |
| `performance-unnecessary-copy-initialization` | 不要コピーを検出 |
| `readability-identifier-naming` | 命名規約を強制 |

### CI での自動チェック（推奨）

```yaml
# .github/workflows/lint.yml
name: Lint
on: [push, pull_request]

jobs:
  format-check:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Check clang-format
        run: |
          find libs tests tools -name '*.cpp' -o -name '*.h' | \
            xargs clang-format --dry-run --Werror

  tidy-check:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
      - name: Run clang-tidy
        run: |
          find libs -name '*.cpp' | \
            xargs clang-tidy -p build --warnings-as-errors='*'
```

---

## 付録5: AIエージェント向けチェックリスト（完全版）

コード生成時に以下を確認すること。

### 構文・スタイル

- [ ] `std::print` / `std::println` を使用（`cout` 禁止）
- [ ] `std::views::enumerate` を使用（手動インデックス禁止）
- [ ] `std::expected` でエラーを返す（例外禁止）
- [ ] `std::to_underlying` で enum 変換（`static_cast` 非推奨）
- [ ] `[[nodiscard]]` を `Result`/`optional` 戻り値に付与
- [ ] 命名規約に準拠（型: PascalCase、関数/変数: snake_case）
- [ ] インデント 4 スペース
- [ ] 1 行 100 文字以内

### 決定性

- [ ] 出力配列は仕様キーで `std::ranges::stable_sort`
- [ ] `unordered_map/set` の反復順に依存しない
- [ ] `std::hash` を ID/ハッシュに使わない
- [ ] ハッシュ対象に浮動小数点を含めない
- [ ] 乱数を使わない（使うなら seed 固定）
- [ ] 時刻/PID/スレッドID を決定性データに混入しない

### メモリ・安全性

- [ ] 所有は `std::unique_ptr`（生ポインタ禁止）
- [ ] `new`/`delete` を直接使わない
- [ ] `string_view`/`span` は所有しない（メンバ保持時は所有型へコピー）
- [ ] Rule of 0 または 5 を遵守

### エラー処理

- [ ] 失敗は `std::expected<T, Error>` で返す
- [ ] `Error` に `code` と `message` を含める
- [ ] 例外はモジュール境界で捕捉して `expected` に変換

### テスト

- [ ] 新機能にはテストを追加
- [ ] 決定性テスト: `--jobs=1` と `--jobs=N` で結果一致
- [ ] スキーマ検証テスト: 出力が schema に適合
