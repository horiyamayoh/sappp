# 実装上の課題・未検討事項・未決定事項（v0.1）

本書は、生成AIに実装を委譲する前提で「詰まりやすい点」「まだ決め切れていない点」を棚卸しする。

## A. 直ちに実装を詰まらせるリスク（Milestone A 直撃）

### A-1. カノニカルJSONとハッシュの“完全一致”問題
- **症状**: OS/言語/ライブラリ差でハッシュが一致せず、Validator が HashMismatch を量産する。
- **論点**: 数値表現、文字列エスケープ、改行、Unicode正規化、キー順、配列順序、パス正規化。
- **対策**:
  - ADR-0101 を早期に決める。
  - v1 は「浮動小数禁止」「pretty/notesはハッシュ対象外」を徹底。
  - 参照実装（canonicalize関数）を validator と analyzer で共通化 or golden test を持つ。

### A-2. パス正規化（Windows/WSL/UNIX混在）
- **症状**: repo_identity.path や cwd の正規化差で tu_id/po_id が揺れ、決定性が壊れる。
- **論点**: ドライブ表記、大小文字、UNC、symlink、区切り、repo-root相対化。
- **対策**:
  - “内部表現は `/`” “repo-root相対” を徹底。
  - 例外（repo外パス）は `external/` プレフィクスなど規約を決める。

### A-3. Clang差分による CFG/IR の揺れ
- **症状**: Clangバージョンやオプション差で CFG が微妙に変わり、block/inst id が変動。
- **対策**:
  - Milestone A は toolchain を固定し、pack に frontend version を必ず記録。
  - ADR-0102（ASTアンカー）を検討し、diff品質を改善。

### A-4. “証拠だけで検証”の境界設計
- **症状**: Validator が IR 生成や解析を再実行し始め、TCBが肥大化する。
- **対策**:
  - Validator はスキーマ＋ハッシュ＋proof規則のみ。
  - 解析（推論）は Analyzer に閉じる。

### A-5. BUGトレース（BugTrace）の設計負債
- **症状**: BugTrace の表現が曖昧で、Validator が再生できず ProofCheckFailed。
- **論点**: 値（モデル）の持ち方、メモリ表現、呼び出し/例外/RAIIのトレース。
- **対策**:
  - v1 は “単一関数 + 単純な整数/NULL” で成立する最小表現から開始。
  - 早めに litmus で BUG トレースを固定（例: div0, null-deref, OOB）。

### A-6. PO（sink）網羅の“抜け”
- **症状**: PO漏れがあると、そもそも不在証明の対象が欠け、ツールの意味が崩れる。
- **対策**:
  - Frontend に `sink.marker` を入れて“検出したがIR化困難”もPO化。
  - テストで「PO生成数がゼロでない」「期待sinkが必ずPO化」を入れる。

---

## B. 実装を進めながら決めるが、早期にログ化すべき未決事項（ADR化）

### B-1. カノニカルJSON厳密仕様（ADR-0101）
- 何をハッシュ対象に含め、何を除外するか（hash_scope.v1 の最終形）

### B-2. IRアンカーの微小編集耐性（ADR-0102）
- po_id の揺れをどう抑えるか

### B-3. semantics_version sem.v1（ADR-0103）
- ベースライン規格（C++23? C++17?）
- 意図的な逸脱点の明文化
- litmus（最小例）の形式とpackへの同梱方法

### B-4. Spec DB authoring（ADR-0104）
- 注釈構文（最小）
- サイドカーの編集UX
- 対象解決（USR生成）の厳密ルール

### B-5. Validator 言語/配布（ADR-0105）
- C++継続かRust分離か

### B-6. 例外/並行性のUNKNOWN細分化（ADR-0106）
- unknown_code の粒度と refinement actions

---

## C. 実装上の難所（設計はあるが工数/複雑性が高い）

### C-1. Spec DB の対象解決（テンプレ/オーバーロード/cv-ref/noexcept）
- USR は Clang で安定だが、
  - “契約がどの宣言/定義に当たるか”
  - version_scope 優先順位
  - 競合/矛盾時の扱い
  を実装する必要がある。

### C-2. RAII と例外巻き戻し
- IRに dtor イベントを明示化しても、
  - 例外経路での dtor 実行順
  - noexcept / terminate
  の意味論をどこまで sem.v1 で扱うかが重要。

### C-3. provenance の扱い
- SRS は provenance 状態を保持し、喪失操作をモデル化することを MUST とする。
- v1 は“器”として state を保持し、精密化は次マイルストーンでも良いが、
  - どの操作を ProvenanceLost とするか
  - それが SAFE 根拠に混入しないか
  の整理が必要。

### C-4. 再現パックの“ヘッダ/標準ライブラリ”問題
- L0〜L3 で段階導入するが、
  - Windows SDK / MSVC STL
  - sysroot の巨大さ
  - ライセンス/配布
  が難点。

### C-5. インクリメンタル/キャッシュ
- po_id をキーにしたキャッシュは魅力だが、依存（SpecDB/semantics/proof/config）をどこまでキーに含めるかが難しい。

---

## D. 生成AIに実装を渡す前に“合意”したい最小セット（おすすめ）

1. **カノニカルJSON/パス正規化**の厳密ルール（ADR-0101）
2. **BUGトレースの最小表現**（litmus 3つで固定）
3. **Spec DB サイドカー v1**（JSON + schema）
4. **po_id/unknown_stable_id の構成要素**（既定のまま + 変更時の移行方針）
5. **pack_manifest に含める必須ファイル一覧**

---

## 付録: 参考（SRSが要求する“器”の優先順位）

- Validator確定・証拠検証（TCB）
- UNKNOWN開拓性（台帳 + missing_lemma + refinement_plan）
- IRの表現力（寿命/例外/仮想/並行性/UB）
- 再現性（pack/diff/決定性）
