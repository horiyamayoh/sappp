# sem.v1 — SAP++ Semantics (v1)

このドキュメントは、SAP++ の `sem.v1` における **基準規格** と **逸脱点** を明文化し、
SAFE/BUG/UNKNOWN の意味論を固定するための規格書です。

> 参照: SRS v1.1 REQ-SEM-001..004

---

## 1. 目的と適用範囲

- `sem.v1` は、SAP++ の解析・検証結果の意味論を固定するための **文章 + litmus** です。
- 本文書は、再現パックに同梱される `sem.v1.md` と同一内容であることを前提とします。

---

## 2. 基準規格（Baseline）

`sem.v1` の基準規格は以下とします。

- **言語規格**: ISO C++23
- **ツールチェーン依存**:
  - フロントエンドは Clang 系を前提（`clang/clang++ -std=c++23`）とし、
    具体的なターゲット・ABI・標準ライブラリ実装に依存する挙動は **明示的に逸脱点として扱う**。

> 重要: ABI/標準ライブラリ差分に依存する結論（SAFE/BUG）を正当化できない場合、
> 必ず UNKNOWN に降格する。

---

## 3. 逸脱点一覧（UNKNOWN への降格条件）

以下は、基準規格（C++23）に対する **意図的な抽象化/未対応/簡略化** の一覧です。
各項目に該当する場合、検証は **UNKNOWN へ降格**します。

| ID | 逸脱点 | UNKNOWN へ倒す条件（運用粒度） |
| --- | --- | --- |
| DEV-SEM-001 | 並行性・アトミクスの未対応 | `std::thread`/atomic/happens-before 等の並行性意味論に依存する場合。`ConcurrencyUnsupported`/`AtomicOrderUnknown`/`SyncContractMissing` を目安。 |
| DEV-SEM-002 | provenance の粗粒度 | pointer-int 変換、広域な pointer 算術、provenance 保持に依存する UB を正当化できない場合。 |
| DEV-SEM-003 | 例外ランタイムの詳細未モデル化 | `noexcept`/`std::terminate`/例外仕様/ABI 依存の unwind など、ランタイム仕様に依存する場合。例外制御フローのみの抽象で結論できない時は UNKNOWN。 |
| DEV-SEM-004 | 仮想ディスパッチの候補集合の保守的扱い | 動的型の特定や候補集合の完全性が必要な証明。`VirtualDispatchUnknown`/`VirtualCall.MissingContract.Pre` を目安。 |
| DEV-SEM-005 | 寿命モデルの対象限定 | placement new / union 活性メンバ / coroutine / setjmp-longjmp など、寿命規則が複雑なケースに依存する場合。 |
| DEV-SEM-006 | ABI/標準ライブラリ差分 | 標準ライブラリ実装差分・ABI差分に依存する挙動を SAFE/BUG の根拠にできない場合。 |

> 注: UNKNOWN へ倒す際のコードは `unknown.v1` の `unknown_code` に記録する。

---

## 4. Litmus セット（最小例）

以下は `sem.v1` の litmus セットです。**各カテゴリ最低1例**を満たすことを保証します。

| カテゴリ | Litmus | 期待される観測点 | ファイル |
| --- | --- | --- | --- |
| 寿命（lifetime） | Use-after-lifetime | `UseAfterLifetime` PO が生成され、BUG で確定できること | `tests/end_to_end/litmus_use_after_lifetime.cpp` |
| 例外（exception） | Exception + RAII | `invoke/landingpad/resume` 等の例外制御フローが NIR に現れること | `tests/end_to_end/litmus_exception_raii.cpp` |
| 仮想（virtual） | Virtual call | `vcall` と候補集合が NIR に現れること。候補不足は UNKNOWN として記録 | `tests/end_to_end/litmus_vcall.cpp` |

追加の litmus（UB 系や未初期化など）は `tests/end_to_end/` に継続的に追加する。

---

## 5. pack 同梱の扱い

- `sappp pack` は `sem.v1.md` を `pack/semantics/sem.v1.md` に同梱する。
- 本ドキュメントが同梱されることにより、解析結果の意味論を再現パックから参照できる。

---

## 6. 更新方針

- `sem.v1` の変更は **破壊的変更** として扱い、変更理由と影響範囲を明記する。
- 実装が逸脱点を解消した場合は、逸脱点一覧から削除し、
  対応する litmus と UNKNOWN コードの更新を行う。
