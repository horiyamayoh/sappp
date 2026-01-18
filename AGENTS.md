# AGENTS.md — SAP++ (sappp) Coding Agent Instructions

このファイルは、SAP++ リポジトリで作業する **AIコーディングエージェント向けの“必須”運用手順**です。  
**この指示に違反した変更は「未完了」扱い**です（たとえコードが動いて見えても）。

> ⚠️ 重要: 本リポジトリのワークフローは **PR作成を前提にしない**。  
> エージェントがPRを作れない環境でも、ここにある品質ゲート（ビルド/テスト/決定性/スキーマ）を満たしたうえで、  
> **差分・再現手順・実行ログを“完了報告”として提示**すること。

---

## 0) まず守るべき最上位原則（破ったら失格）

### 0.1 嘘SAFE/嘘BUGを絶対に作らない（最重要）
- **SAFE/BUG は Validator が検証して初めて確定**。
- 検証できない／検証に失敗したら **必ず UNKNOWN に降格**。
- Analyzer が自信満々でも、Validator が通らないなら確定してはいけない。

### 0.2 TCB境界を侵食しない（Validatorは“証拠だけ”）
Validator は **証拠（Certificate）+ IR参照 + スキーマ + ハッシュ**だけで検証する。

**Validator内で禁止**（= TCB侵食）:
- Frontend（Clang）再実行／再解析
- Analyzer 呼び出し
- ソースコード再解釈（証拠以外の追加情報に依存する検証）

### 0.3 決定性（並列でも同じ結果）を壊さない
- 同一入力・同一設定・同一バージョンで、出力（少なくともカテゴリとID）が一致すること。
- 並列処理は「並列計算 → 単一スレッドで安定マージ」。
- 出力配列は **仕様で定めたキーで安定ソート（stable sort）**。
- `unordered_map/set` の反復順など **不定順**に依存しない。

### 0.4 カノニカルJSONとハッシュの約束を破らない
- ハッシュ対象のJSONは **カノニカル化（canonical JSON）**で完全一致させる。
- v1では **浮動小数点（NaN/Inf含む）禁止**。必要なら整数や文字列で表現する。

### 0.5 スキーマ準拠は必須
- 生成するJSON（build_snapshot / nir / source_map / po / unknown / cert / validated_results / pack_manifest / diff 等）は、
  **保存前・読み込み後に必ずスキーマ検証**する。

---

## 1) 最優先コマンド（まずここだけは守る）

### 1.1 ビルド（必須・警告ゼロ）
**変更を加えたら、完了報告前に必ずビルドを通す。**

⚠️ **警告ゼロポリシー**: ビルド時に **コンパイラ警告が1つでも残っていたら「未完了」**。  
警告は潜在的なバグの兆候であり、放置すると品質が劣化する。自分が追加した警告だけでなく、**既存の警告も見つけたら修正**すること。

```bash
# 推奨（out-of-source build）
# -DSAPPP_WERROR=ON で警告をエラーとして扱う（CI同等の厳格チェック）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSAPPP_BUILD_TESTS=ON -DSAPPP_BUILD_CLANG_FRONTEND=OFF -DSAPPP_WERROR=ON
cmake --build build --parallel
```

> タスクが `frontend_clang` を触る/必要とする場合は、次で **ONでも確認**すること。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSAPPP_BUILD_TESTS=ON -DSAPPP_BUILD_CLANG_FRONTEND=ON -DSAPPP_WERROR=ON
cmake --build build --parallel
```

> `-DSAPPP_WERROR=ON` が未対応の場合でも、ビルドログを確認し **warning が0件であること**を目視確認すること。

### 1.2 テスト（必須）
**ビルド成功後、完了報告前に必ず全テストを回す。**

```bash
ctest --test-dir build --output-on-failure
```

### 1.3 決定性（必須ゲート）
Determinism系テストが存在するなら必ず実行する。

```bash
ctest --test-dir build -R determinism --output-on-failure
```

---

## 2) Milestone A（器の完成）: Definition of Done（E2E）

Milestone A は「器の完成」。Analyzer の精度は二の次でよい（UNKNOWNが多くても良い）が、
**I/F・証拠・検証・再現性が成立**していることが合格条件。

最低限成立させるE2E（ローカルで再現可能 & 決定的）:

1. `sappp capture` が `build_snapshot.v1` を生成できる
2. `sappp analyze` が以下を生成できる（全て Schema 適合）
   - `nir.v1`（最低限: CFG + ub.check）
   - `po.v1`（少なくとも div0 / null / oob のいずれかをPO化）
   - `unknown.v1`（UNKNOWN必須要素を満たす）
   - `cert.v1`（BUGまたはSAFEの証拠候補を最低1件以上）
3. `sappp validate` が以下を達成
   - BUG証拠: BugTrace を再生し PO違反を確認
   - SAFE証拠: 検証できないなら UNKNOWN 降格（嘘SAFE禁止）
4. `sappp pack` が `tar.gz + manifest.json(pack_manifest.v1)` を生成できる
5. `sappp diff` が before/after の `validated_results.v1` を PO ID キーで比較し `diff.v1` を出せる
6. `--jobs` を変えても **カテゴリとID**（po_id / unknown_stable_id / cert hashes）が一致する

---

## 3) CLI（出力I/F）の固定仕様（v0.1）

### 3.1 グローバル
- `--jobs <N>` : 並列度（デフォルト: ホストCPU）
- `--schema-dir <dir>` : JSON Schema dir（デフォルト: 同梱schemas/）
- バージョン三つ組（必ず成果物へ埋め込む）:
  - `--semantics`（例: `sem.v1`）
  - `--proof`（例: `proof.v1`）
  - `--profile`（例: `safety.core.v1`）

### 3.2 analyze 出力ディレクトリ（固定）
`--out out/` の場合、少なくとも以下のパスに出力すること:

- `out/frontend/nir.json`（`nir.v1`）
- `out/frontend/source_map.json`（`source_map.v1`）
- `out/po/po_list.json`（`po.v1`）
- `out/analyzer/unknown_ledger.json`（`unknown.v1`）
- `out/certstore/objects/...`（`cert.v1` objects）
- `out/certstore/index/...`（`cert_index.v1`）
- `out/config/analysis_config.json`（`analysis_config.v1`）

> analyze 時点の SAFE/BUG は「候補」。確定は validate のみ。

---

## 4) 出力・ID・ハッシュ規約（v1運用ルール）

### 4.1 安定ソート規約（v1）
少なくとも以下は **保存直前に stable sort** を適用すること:

- `po.v1` の `items[]` は `po_id` 昇順
- `unknown.v1` の `items[]` は `unknown_stable_id` 昇順
- `validated_results.v1` の `results[]` は `po_id` 昇順
- `cert_index.v1` の `entries[]` は `po_id` 昇順
- `nir.v1` の `functions[]` は `function_uid` 昇順
  - `blocks[]` は `block_id` 昇順
  - `insts[]` は `inst_id` 昇順

### 4.2 po_id / unknown_stable_id の設計原則
- `po_id` は **diff のキー**。生成要素（最低限）:
  - 解析対象同一性（path + content hash）
  - 関数同定（clang USR 等）
  - IRアンカー（block_id + inst_id）
  - po_kind
  - semantics/profile/proof の version triple
- `unknown_stable_id` は UNKNOWN台帳の追跡キー。po_id と紐付け、
  不足理由（unknown_code）や不足補題（missing_lemma）が変化しても追跡できるようにする。

### 4.3 パス正規化（v1）
- 区切りは `/`
- repo-root 相対パスを優先（`--repo-root` が与えられたら必ず相対化）
- repo外パスは `external/<hash>/...` 等に写像
- OS依存（Windowsドライブ/UNC/大小文字）は **normalize_path だけ**で吸収する

### 4.4 カノニカルJSON（ハッシュ一致の根幹）
- UTF-8
- オブジェクトキーは辞書順
- 配列は「仕様で順序が意味を持つ」場合のみ順序維持
  - それ以外は **ソートしてから出力**（またはID順）
- 数値は整数のみ（浮動小数禁止）
- 改行/空白/インデントはハッシュ対象外（canonical serializerは最小表現が望ましい）

**NG例（決定性が壊れる）**
```cpp
std::sort(items.begin(), items.end()); // 不安定ソートや比較が曖昧だと危険
out << j.dump(2); // pretty をハッシュ対象に混ぜるのは危険
```

**OK例（決定性が成立しやすい）**
```cpp
std::stable_sort(items.begin(), items.end(),
  [](const Item& a, const Item& b){ return a.id < b.id; });

out << sappp::canonical::canonicalize(j); // canonical bytes を出力
```

---

## 5) UNKNOWN は“台帳”である（開拓可能性が価値）

UNKNOWN は「失敗ログ」ではない。**改善バックログ**として機能させる。

unknown.v1 の1件ごとに、少なくとも以下を必須にする:
- `unknown_code`（分類。例外/並行性などは保守的にUNKNOWNへ倒す）
- `missing_lemma`（機械可読 + pretty）
- `refinement_plan`（推奨精密化：パラメータ付き actions）
- `unknown_stable_id`（差分追跡）

Validator 側の基本姿勢:
- 不一致/不足/未対応 → **UNKNOWNへ降格**して継続（`--strict` のときのみ即エラー）

---

## 6) テスト方針（“AI実装の自作自演”を潰す）

### 6.1 追加・変更に対する最低要件
- 新しい機能/クラス/分岐を追加したら **対応するテストも追加**。
- 既存テストを削除・スキップして通すのは禁止（直せ）。

### 6.2 最低限そろえるテストの型
- Unit/Golden:
  - canonical JSON（最重要）
  - sha256
  - path normalization
  - schema validation（valid/invalid）
- Validator:
  - BUG（div0/null/oob の最小3ケース）
  - 改ざん（HashMismatch → UNKNOWN降格 または strictでエラー）
  - 欠落（MissingDependency → UNKNOWN降格）
- Determinism:
  - `--jobs=1` と `--jobs=8` で
    - po_id集合一致
    - unknown_stable_id集合一致
    - validated_results一致
    - pack manifest の digest 一致
- E2E:
  - `capture → analyze → validate → pack → diff` を自動化

### 6.3 決定性の手動チェック（テストが未整備な場合の最低限）
E2Eを2回回して、ID集合が一致することを確認する（例）:

```bash
# 例: out_j1 / out_j8 に出して比較（jq があるなら簡単）
rm -rf out_j1 out_j8
sappp analyze --build build_snapshot.json --out out_j1 --jobs 1
sappp analyze --build build_snapshot.json --out out_j8 --jobs 8

# validate も同様に
sappp validate --in out_j1
sappp validate --in out_j8

# po_id / unknown_stable_id / results の集合一致を確認（jq想定）
jq -r '.items[].po_id' out_j1/po/po_list.json | sort > /tmp/po_j1.txt
jq -r '.items[].po_id' out_j8/po/po_list.json | sort > /tmp/po_j8.txt

diff -u /tmp/po_j1.txt /tmp/po_j8.txt
```

> jq が無い場合は、同等の抽出を C++ テスト or 最小スクリプトで用意すること。

---

## 7) 触ってよい範囲 / 触ってはいけない範囲（境界）

### 7.1 触ってよい（通常）
- `libs/**`（common/canonical/build_capture/frontend_clang/ir/po/analyzer/specdb/certstore/validator/report/...）
- `tools/sappp/**`（CLI）
- `tests/**`
- `schemas/**`（ただし破壊的変更は原則禁止、やるならバージョン更新）

### 7.2 原則触らない（変更が必要なら理由と影響を明記）
- CI設定（`.github/workflows/**` 等）
- third_party の大規模差し替え
- 既存スキーマの破壊的変更

### 7.3 依存追加（要注意）
- 新規ライブラリ追加は慎重に。
  「決定性」「ビルド再現性」「TCB最小化」に悪影響がないことを説明できる場合のみ。

---

## 8) コーディング規約（C++23 徹底）

**詳細規約は [`docs/CODING_STYLE_CPP23.md`](docs/CODING_STYLE_CPP23.md) を参照。**

本プロジェクトは **C++23 を全面採用**（GCC 14+ / Clang 18+）。  
以下は最重要ポイントの要約。違反はレビューで即指摘対象。

### 8.1 C++23 必須機能（抜粋）

| 機能 | 用途 | 旧スタイル（禁止） |
|-----|------|------------------|
| `std::print` / `std::println` | 出力 | `std::cout <<` |
| `std::expected<T, E>` | エラー | 例外スロー |
| `std::views::enumerate` | インデックス付きループ | `for (size_t i = 0; ...)` |
| `std::rotr` / `std::byteswap` | ビット操作 | 手書き |
| `std::to_underlying` | enum→整数 | `static_cast` |
| `[[nodiscard]]` | 戻り値無視防止 | （なし） |

### 8.2 命名規約（Google C++ Style Guide ベース）

| 種別 | スタイル | 例 |
|-----|---------|-----|
| namespace | `snake_case` | `sappp::validator` |
| 型（class/struct/enum） | `PascalCase` | `PoGenerator` |
| 関数/変数 | `snake_case` | `validate_cert()` |
| メンバ変数 | `m_` + `snake_case` | `m_config` |
| 定数 | `k` + `PascalCase` | `kMaxRetries` |
| 列挙子 | `k` + `PascalCase` | `kSuccess` |
| マクロ | `UPPER_SNAKE_CASE` | `SAPPP_VERSION` |

### 8.3 フォーマット

- インデント: **4スペース**
- 1行最大: **100文字**
- `clang-format` を正とする（手整形禁止）

### 8.4 コミットメッセージ

- **日本語で記述**
- 1行目: 変更の要約（50文字以内目安）
- 3行目以降: 詳細（なぜ・何を・どう変えたか）

### 8.5 ビルド環境要件

| 項目 | 最小要件 |
|-----|---------|
| C++ 標準 | C++23 |
| GCC | 14.0+ |
| Clang | 18.0+ |
| CMake | 3.16+ |

```bash
# Ubuntu 24.04 LTS
sudo apt install gcc-14 g++-14
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++-14 -DCMAKE_C_COMPILER=gcc-14
```

---

## 9) 禁止事項（やったら即「未完了」）

1. ビルド/テスト未実行での完了宣言
2. **コンパイラ警告を残したままの完了宣言**（警告ゼロが必須）
3. 既存テストの削除・スキップ・無効化
4. 決定性破壊（不定順の出力、安定ソート無し、jobs差で結果が変わる）
5. ハッシュ対象への浮動小数追加
6. Validator内での Analyzer/Frontend 呼び出し
7. スキーマ不適合なJSONを"とりあえず"出す
8. 検証できないのに SAFE/BUG を確定させる（嘘SAFE/嘘BUG）

---

## 10) 完了報告フォーマット（PR不要でも品質を担保する）

完了時は、次を **必ず** 提示すること（“やったつもり”防止）:

1. 変更概要（何を、なぜ、どう変えた）
2. 変更ファイル一覧
3. 実行したコマンドと結果（ログを貼る）
   - `cmake -S ... -B ...`
   - `cmake --build ...`
   - `ctest ...`
4. 仕様への対応箇所（SRS/Directive/CLI Spec/ADR の該当章）
5. 決定性への配慮点（どこで順序固定/ハッシュ固定をしたか）
6. 既知の未対応（あれば）と、その場合に **UNKNOWNへ倒す理由**（unknown_code / missing_lemma / refinement_plan）
7. 可能なら `git diff`（またはパッチ）を提示

---

## 付録: 仕様の一次ソース（必読）
- **`docs/CODING_STYLE_CPP23.md`** — C++23 コーディング規約（詳細）
- `docs/SAPpp_Implementation_Directive_v0.1.md`
- `docs/SAPpp_SRS_v1.1.md`
- `docs/SAPpp_Detailed_Design_v0.1.md`
- `docs/SAPpp_Architecture_Design_v0.1.md`
- `docs/ADR/`（特に determinism / po_id / unknown / canonical json）
- `docs/CLI_Spec_v0.1.md`
- `schemas/*.schema.json`
