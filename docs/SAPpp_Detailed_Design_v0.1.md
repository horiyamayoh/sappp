# SAP++ 詳細設計書（DDD）v0.1

- 対象プロダクト: SAP++（Sound, Static Absence-Proving Analyzer for C++）
- 状態: Draft（Milestone A 実装のための詳細設計）
- 作成日: 2026-01-17
- 入力: SRS v1.1 / SAD v0.1 / 実現方式検討結果報告書 v1

> 本書は、SRS が要求する最上位原則（嘘SAFE/嘘BUGゼロ、Proof-Carrying、Validator確定、UNKNOWN開拓性、C++破綻点の“器”先行、決定性）を **実装可能な詳細設計**へ落とし込む。
> とくに Milestone A（器の完成）を成立させるため、スキーマ、ID規約、カノニカル化、Validator検証規則、pack/diff の入出力を先に固定する。

---

## 0. 用語・前提

- **PO**: Proof Obligation（証明課題）。IR上の sink から生成される。
- **SAFE / BUG / UNKNOWN**: 解析カテゴリ。SAFE/BUG は **Validator 検証済み** のみ確定。
- **Certificate**: PO単位の検証可能証拠。
- **TCB**: 正しいと仮定せざるを得ない要素（Validator・semantics_version・Tier0/1契約など）。

### 0.1 3種のバージョン（全成果物へ埋め込み）

すべての解析成果物（結果・証拠・pack）は以下を必ず保持する。

- `semantics_version` : 採用意味論モデル（Tool Semantics）
- `proof_system_version` : 証拠検証規則
- `profile_version` : 保証対象性質の集合（例: Safety.Core）

---

## 1. 目的とスコープ

### 1.1 目的

- Milestone A の受入デモ（analyze→validate→pack→diff）を **仕様どおり成立**させる。
- Analyzer がいくら複雑化しても、Validator が通らない限り SAFE/BUG は増えない構造を担保する。

### 1.2 対象

- 入力: C/C++ 翻訳単位（TU）。Make/CMake の **実コンパイル条件**を取り込み解析する。
- 出力: JSON（必須）+ SARIF（任意）、再現パック、差分、UNKNOWN台帳。

### 1.3 非スコープ

- 動的テスト、プロファイル、実行ログなどを SAFE/BUG確定根拠に用いない（Static-only）。

---

## 2. 全体アーキテクチャ（実装分解）

### 2.1 論理構成

```
[Build Capture]  ----> build_snapshot.json
      |
      v
[Frontend (Clang)] ----> nir.json + source_map.json
      |
      v
[PO Generator] --------> po_list.json
      |
      v
[Analyzer]  ------------> cert_candidates/ + unknown_ledger.json
      |
      v
[Certificate Store (CAS)] <---- shared nodes
      |
      v
[Validator] ------------> validated_results.json
      |
      v
[Report/CLI]  ----------> report.json / report.sarif / pack / diff / explain
```

### 2.2 リポジトリ（物理構成案）

```
repo/
  docs/
    DDD.md (本書)
    SAD.md
    ADR/
    schemas/
  schemas/                 # JSON schema（バージョン管理）
  tools/
    sappp/                 # CLI
  libs/
    build_capture/
    frontend_clang/
    ir/
    po/
    analyzer/
    specdb/
    certstore/
    validator/
    report/
  tests/
    validator/
    determinism/
    schemas/
    end_to_end/
```

---

## 3. データ設計（スキーマ v1 群）

> ここで定義するスキーマは、Milestone A で「I/F 契約」として固定する。

### 3.1 共通メタデータ（全JSON先頭に入れる）

各 JSON ルートには以下を（該当する範囲で）含める。

- `schema_version`: 例 `nir.v1`
- `tool`: `{name, version, build_id}`
- `generated_at`: ISO8601
- `semantics_version` / `proof_system_version` / `profile_version`
- `input_digest`: 入力（ソース/ビルド条件/設定/SpecDB）をまとめたハッシュ（packで参照）

> **注**: `input_digest` は UI 表示用途ではなく、運用上の参照（pack/diff/キャッシュキー）を想定する。

---

### 3.2 build_snapshot.v1（実コンパイル条件の完全取り込み）

#### 3.2.1 目的

- TU解析の前提を完全に再現し、再現性（REQ-IN-001/002, REQ-REP-001/002）と Soundness の土台を作る。

#### 3.2.2 出力ファイル

- `build_snapshot.json`

#### 3.2.3 主要フィールド（確定）

- `host.os`: `linux|windows|macos`（macosは将来）
- `compile_units[]`:
  - `tu_id`: `sha256:` + 64hex
  - `cwd`: 解析時点の絶対パス
  - `argv[]`: 実行されたコンパイラ呼出し（response file 展開後）
  - `env_delta`: 重要環境変数のみ（差分）
  - `response_files[]`: `.rsp` など（中身 hash と、任意で content）
  - `lang`: `c|c++`
  - `std`: `c11|c17|c++11|c++14|c++17|c++20|c++23`（文字列として保持）
  - `target.triple`, `target.abi`, `target.data_layout`（主要型size/align含む）
  - `frontend.kind`, `frontend.version`, `frontend.resource_dir`

#### 3.2.4 tu_id 生成規則（決定）

`tu_id = sha256( CanonicalJSON({cwd, argv, env_delta, response_files_sha, lang, std, target}))`

- `argv` は response file 展開後を使用
- `cwd` はパス正規化後（§10）
- `env_delta` はキーを辞書順で固定
- `target.data_layout` は Frontend が算出した結果を保持（clang -dM 等には依存しない）

---

### 3.3 source_map.v1（ソース位置復元）

#### 3.3.1 出力ファイル

- `source_map.json`

#### 3.3.2 目的

- マクロ展開やテンプレ展開後でも、原ソース位置へ戻す（REQ-IN-004）。

#### 3.3.3 フィールド

- `tu_id`
- `entries[]`:
  - `ir_ref`: `{function_uid, block_id, inst_id}`
  - `spelling_loc`: `{file, line, col, offset}`
  - `expansion_loc`: `{file, line, col, offset}`
  - `macro_stack[]`: `[{macro_name, def_loc, call_loc}]`

---

### 3.4 nir.v1（Normalized IR: CFG + C++イベント）

#### 3.4.1 出力ファイル

- `nir.json`

#### 3.4.2 設計方針

- コア解析・証拠・検証は **IRのみ** を対象（REQ-IR-001）。
- C++破綻点（寿命/例外/仮想/並行性/UB）を **命令（イベント）** として表現（REQ-IR-002）。
- 決定性・diff のため、ブロック/命令IDの付番規約を固定する（§10）。

#### 3.4.3 ルート構造（概略）

- `tu_id`
- `files[]`: `{path, sha256}`
- `types[]`: `{type_id, clang_canonical, layout:{size,align}}`
- `globals[]`: 任意（将来）
- `functions[]`:
  - `function_uid`: clang USR（主キー）
  - `mangled_name`
  - `signature`: 引数/戻り/例外仕様（noexcept）
  - `cfg`:
    - `entry`: `B0`
    - `blocks[]`:
      - `id`: `B<number>`
      - `insts[]`:
        - `id`: `I<number>`
        - `op`: 命令種別
        - `args`: オペランド（SSA名、定数、参照ID等）
        - `effects`: メモリ/寿命/例外/並行性の副作用（Validatorが再生可能な最小限）
        - `src`: `source_map` 参照用の最小位置
    - `edges[]`: `{from,to,kind}` kind=`normal|true|false|exception|callret|...`
  - `tables`:
    - `vcall_candidates[]`: `{id, methods[]}`

#### 3.4.4 IR命令セット（v1: 器としての最小核）

- 制御: `branch`, `switch`, `ret`
- メモリ: `alloc`, `free`, `load`, `store`, `memcpy`, `memmove`, `memset`
- 寿命: `lifetime.begin`, `lifetime.end`, `ctor`, `dtor`, `move`
- 例外: `invoke`, `throw`, `landingpad`, `resume`
- 仮想: `vcall(receiver, candidateset_id, signature)`
- 並行性: `thread.spawn`, `thread.join`, `atomic.r(order)`, `atomic.w(order)`, `fence(order)`, `sync.event(kind,obj)`
- UB: `ub.check(kind, cond)`
- 網羅性補助: `sink.marker(kind, ast_hint)`

> **注**: v1 は “表現力の器” を優先し、未精密化は UNKNOWN へ倒す（嘘SAFE/嘘BUG回避）。

---

### 3.5 po.v1（Proof Obligation 定義）

#### 3.5.1 出力ファイル

- `po_list.json`

#### 3.5.2 ルート構造

- `tu_id`
- `pos[]`: PO配列（**必ず安定ソート**で出力、§10）

#### 3.5.3 POフィールド（確定）

- `po_id`: `sha256:` + 64hex（安定ID）
- `po_kind`: `OOB|NullDeref|UseAfterLifetime|UninitRead|DoubleFree|InvalidFree|UB.Shift|UB.DivZero|...`
- `profile_version`, `semantics_version`, `proof_system_version`
- `repo_identity`: `{path, content_sha256}`
- `function`: `{usr, mangled}`
- `anchor`: `{block_id, inst_id, src?}`
- `predicate`: `{expr, pretty}`

#### 3.5.4 po_id 生成規則（決定）

`po_id = sha256( CanonicalJSON({repo_identity, function.usr, anchor, po_kind, semantics_version, profile_version, proof_system_version}))`

- `anchor` は `block_id` と `inst_id`（nir.v1 で決定）
- `repo_identity.path` はパス正規化後（§10）

---

### 3.6 unknown.v1（UNKNOWN台帳）

#### 3.6.1 出力ファイル

- `unknown_ledger.json`

#### 3.6.2 ルート構造

- `tu_id`
- `unknowns[]`:
  - `unknown_stable_id`
  - `po_id`
  - `unknown_code`
  - `missing_lemma`: `{expr, pretty, symbols[], notes?}`
  - `refinement_plan`: `{message, actions[]}`
  - `depends_on`: 依存情報（契約、解析設定、意味論逸脱点、など）

#### 3.6.3 unknown_stable_id 生成規則（決定）

`unknown_stable_id = sha256( CanonicalJSON({po_id, unknown_code, semantics_version, profile_version, proof_system_version}))`

- 解析設定差で UNKNOWN の意味が変わる場合は `depends_on.analysis_profile_digest` を含める（v1では任意→v1.1で必須化検討）。

#### 3.6.4 unknown_code（v1 既定集合）

SRS 付録例をベースに、以下を初期セットとして採用（拡張可能）。

- `MissingInvariant`
- `MissingContract.Pre|Post|Frame|Ownership|Concurrency`
- `AliasTooWide|PointsToUnknown`
- `DomainTooWeak.Numeric|DomainTooWeak.Memory|DomainTooWeak.Concurrency`
- `VirtualDispatchUnknown`
- `ExceptionFlowConservative`
- `ProvenanceLost`
- `LifetimeUnmodeled`
- `RaceUnproven|AtomicOrderUnknown|SyncContractMissing|ConcurrencyUnsupported`
- `BudgetExceeded`
- `UnsupportedFeature`

---

### 3.7 cert.v1（Certificate: PO単位証拠 + 共有参照）

#### 3.7.1 物理配置

- `certstore/objects/<prefix>/<sha256>.json` : CAS オブジェクト
- `certstore/index/<po_id>.json` : PO→ルート証拠ハッシュ

#### 3.7.2 目的

- PO単位で検証可能（REQ-CRT-001）。
- 共有ノード参照で重複排除（REQ-CRT-002）。
- カノニカル化 + ハッシュ範囲固定で改ざん検出（REQ-CRT-011/012/013）。

#### 3.7.3 CASオブジェクト種（v1）

- `PoDef` : PO定義（po.v1 の単体抜粋 + 参照整形）
- `IrRef` : IR参照（function_uid / block_id / inst_id / tu_id）
- `Invariant` : SAFE証拠用の不変条件（抽象状態）
- `BugTrace` : BUG証拠用の反例トレース（実行パス必須）
- `ContractRef` : 依存契約参照（SpecDB snapshot 内 ID と Tier 等）
- `DependencyGraph` : 証拠依存グラフ（ノード=CAS hash、辺=参照）
- `ProofRoot` : PO単位のルート（SAFE/BUG候補）

#### 3.7.4 ProofRoot 概略

```json
{
  "schema_version": "cert.v1",
  "kind": "ProofRoot",
  "po": {"ref": "sha256:..."},
  "ir": {"ref": "sha256:..."},
  "result": "SAFE|BUG",
  "evidence": {"ref": "sha256:..."},
  "depends": {
    "contracts": [{"ref": "sha256:..."}],
    "assumptions": ["..."],
    "semantics_version": "sem.v1",
    "proof_system_version": "proof.v1",
    "profile_version": "safety.core.v1"
  },
  "hash_scope": "hash_scope.v1"
}
```

---

### 3.8 validated_results.v1（Validator出力）

#### 3.8.1 出力ファイル

- `validated_results.json`

#### 3.8.2 ルート構造

- `tu_id`
- `results[]`（PO単位、安定ソート）
  - `po_id`
  - `category`: `SAFE|BUG|UNKNOWN`
  - `certificate_root`: `sha256:...`（SAFE/BUG時）
  - `validator_status`: `Validated|Downgraded|SchemaInvalid|...`
  - `downgrade_reason_code`: （UNKNOWNへ降格した場合）
  - `notes`: 任意

#### 3.8.3 downgrade_reason_code（最小セット）

- `SchemaInvalid`
- `VersionMismatch`
- `HashMismatch`
- `MissingDependency`
- `RuleViolation`
- `ProofCheckFailed`
- `UnsupportedProofFeature`

---

### 3.9 pack_manifest.v1（再現パック）

#### 3.9.1 出力

- `pack.tar.gz`
- `manifest.json`

#### 3.9.2 pack 構造（v1）

```
pack/
  manifest.json
  inputs/
    build_snapshot.json
    compile_units/
      <tu_id>/...
  frontend/
    nir.json
    source_map.json
  po/
    po_list.json
  analyzer/
    unknown_ledger.json
    cert_candidates/   # analyzer出力（任意、運用次第）
  certstore/
    objects/...        # 必須（Validatorが必要とする範囲）
    index/...
  specdb/
    snapshot.json
  semantics/
    sem.v1.md  # 基準規格・逸脱点・litmus一覧を含む意味論ドキュメント
  results/
    validated_results.json
  config/
    analysis_config.json
  repro_assets/
    ... (L0-L3)
```

#### 3.9.3 Repro Asset Level（L0〜L3）

- **L0**: Fingerprint only（include探索パス + toolchain識別 + 主要ヘッダhash一覧）
- **L1**: Used headers snapshot（TU依存ヘッダのみ収集）
- **L2**: Sysroot snapshot（sysroot/標準ライブラリディレクトリを収集）
- **L3**: Container pinning（Docker digest等）

---

### 3.10 diff.v1（差分出力）

#### 3.10.1 入力

- `validated_results.json`（before/after）
- `po_list.json`（必要なら）
- `unknown_ledger.json`（必要なら）

#### 3.10.2 出力（概略）

- `diff.json`

- `changes[]`:
  - `po_id`
  - `from`: `{category, certificate_root?}`
  - `to`: `{category, certificate_root?}`
  - `change_kind`: `New|Resolved|Regressed|Unchanged|Reclassified`
  - `reason`: `SemanticsUpdated|ProofRuleUpdated|ContractDisabled|ConfigChanged|BudgetChanged|...`
  - `details`: 任意

---

## 4. 処理フロー（シーケンス）

### 4.1 analyze（解析＋証拠候補生成）

1. 入力: `build_snapshot.json` + ソース（リポジトリ） + `specdb snapshot`（任意）
2. Frontend が TU ごとに `nir.json` と `source_map.json` を生成
3. PO Generator が `po_list.json` を生成（sink網羅）
4. Analyzer が PO ごとに
   - `SAFE` 候補: 不変条件（Invariant）と含意
   - `BUG` 候補: 反例トレース（BugTrace）
   - `UNKNOWN`: unknown_ledger へ（missing_lemma/refinement_plan 付）
5. Analyzer は `cert_candidates/` として ProofRoot（候補）を CAS に格納（ただし **確定はしない**）

> analyze の時点では SAFE/BUG は **候補**。確定は validate のみ。

### 4.2 validate（証拠検証）

1. 入力: `nir.json` / `po_list.json` / `certstore` / `specdb snapshot` / version triple
2. Validator が
   - スキーマ検証
   - バージョン整合
   - ハッシュ検証
   - 依存解決（CAS参照、契約参照）
   - proof_system v1 規則に従う検証
3. 成功: `SAFE|BUG` を確定
4. 失敗: `UNKNOWN` に降格し、理由コードを出す（REQ-PRIN-004, REQ-VAL-002）

### 4.3 explain（UNKNOWN開拓ガイド）

- 入力: `unknown_ledger.json`（必要なら validated_results でフィルタ）
- 出力: 人間可読（CLI表示） + 機械可読（explain.json 任意）

### 4.4 pack（再現パック生成）

- 入力: 解析一式
- 出力: `pack.tar.gz` + `manifest.json`

### 4.5 diff（差分）

- 入力: before/after pack または results
- 出力: `diff.json`

---

## 5. コンポーネント詳細設計

### 5.1 Build Capture（build_capture/）

#### 5.1.1 役割

- Make/CMake などから **実際に使われた** コンパイル条件を収集し、`build_snapshot.v1` を生成。

#### 5.1.2 CMake 対応

- `CMAKE_EXPORT_COMPILE_COMMANDS=ON` の `compile_commands.json` を読み込み
- 必要に応じて `-I/-isystem/-D/-std/-target/--sysroot` 等を抽出・正規化

#### 5.1.3 Make 対応

- ラッパ `sappp-cc / sappp-cxx` を提供
- 実行されたコンパイルをログ収集し、最終的に `build_snapshot.json` を生成
- response file（`.rsp`）は展開し、
  - `argv` には展開後を反映
  - `response_files[]` に path/hash を記録

#### 5.1.4 重要な決定性要件

- `compile_units` は `tu_id` 辞書順で安定ソートして出力
- 収集順（並列ビルド等）に依存しない

---

### 5.2 Frontend（frontend_clang/）

#### 5.2.1 役割

- Clang Tooling により TU を解析し、`nir.v1` と `source_map.v1` を生成。

#### 5.2.2 入力

- build_snapshot の compile_unit（cwd/argv/env/target）

#### 5.2.3 出力

- `nir.json`
- `source_map.json`

#### 5.2.4 C++イベントの lowering 規則（v1）

> v1 では「表現できる器」を優先し、精密化が必要な部分は UNKNOWN を厚くする。

- 寿命
  - 自動変数/一時オブジェクト: `lifetime.begin` を生成点で、`lifetime.end` をスコープ終端で生成
  - ctor/dtor: 呼出しを `ctor` / `dtor` として明示
  - 例外巻戻しで走る dtor: 例外エッジ側に `dtor` を配置（CFGに `exception` edge を保持）
- 例外
  - `try`/`catch` を CFG へ反映
  - 呼出し点で例外が投げ得る場合: `invoke` とし、normal/exception edge を張る
  - `throw` / `landingpad` / `resume` を明示
- 仮想
  - `virtual call` は `vcall` とし、候補集合（上界）を `tables.vcall_candidates` に保持
- 並行性
  - `std::thread` 等の基本イベント: `thread.spawn/join`
  - atomic/fence: `atomic.r/w(order)` と `fence(order)`
  - ロック等は当面 `sync.event(kind,obj)` として表現（精密化は後続）
- UB
  - UB 条件は `ub.check(kind, cond)` を挿入（PO生成対象）
- sink.marker
  - IR化が困難でも PO 漏れを防ぐために `sink.marker(kind, ast_hint)` を挿入

#### 5.2.5 ブロック/命令ID付番（決定）

- 付番は関数ごとに 0 から開始。
- ブロック順序は **entry からの Reverse Post Order (RPO)** を基本とする。
  - 同順位タイの場合は、ブロック代表ソース位置（spelling_loc の file/line/col）で辞書順 tie-break。
- 命令順序はブロック内の実行順（CFG構築後の順）
- `B{n}` / `I{n}` の `n` は上記順序の 0-based 連番

> **注意**: Clang 内部IDやアドレスに依存しないこと（決定性）。

---

### 5.3 PO Generator（po/）

#### 5.3.1 役割

- `nir.v1` 上の sink を網羅し `po.v1` を生成（REQ-PO-001）。

#### 5.3.2 sink 定義（v1: Safety.Core）

- メモリ安全:
  - `OOB`（配列/ポインタ演算/`operator[]` 等）
  - `NullDeref`
  - `UseAfterLifetime`
  - `DoubleFree` / `InvalidFree`
  - `UninitRead`
- UB:
  - `UB.DivZero`
  - `UB.Shift`
  - `UB.SignedOverflow`（扱いは semantics_version で固定）
  - `UB.Misaligned`（段階導入）

#### 5.3.3 PO生成アルゴリズム（概略）

- 各命令 `inst` に対し、`op` と `effects` を見て PO候補を列挙
- `ub.check` は必ず PO を生成（kind に対応）
- `sink.marker` も必ず PO を生成（kind に対応）
- PO 述語 `predicate.expr` は
  - Validator が評価可能な形（式AST）
  - かつ humans 向け `pretty`

---

### 5.4 Analyzer（analyzer/）

#### 5.4.1 役割

- POごとに SAFE/BUG/UNKNOWN の **候補** を作り、Certificate（候補）を生成。

#### 5.4.2 解析方針（v1）

- 抽象解釈ベース（停止性を満たす）
- 早期から関数間（summary 駆動）を優先
- SMT は初版で **実行しない**（refinement_plan の提案のみ）

#### 5.4.3 抽象ドメイン（v1 最小）

- 数値: interval
- nullness: `{Null|NonNull|MaybeNull}`
- lifetime: `{Alive|Dead|Maybe}`
- init: `{Init|Uninit|Maybe}`
- points-to: 上界集合（上限N、超過は PointsToUnknown）

#### 5.4.4 予算（停止性）

- TU予算: `max_time_ms`, `max_iterations`, `max_states`, `max_summary_nodes`
- 超過: 対象POは UNKNOWN（unknown_code=`BudgetExceeded`）

#### 5.4.5 証拠生成

- SAFE候補
  - CFG 各ブロック入口（または各命令位置）に不変条件（Invariant）を置く
  - PO地点で `Invariant ⇒ predicate` を示す
- BUG候補
  - 実行パス（block/inst の列） + 状態遷移（必要最小）を `BugTrace` として出力
  - 最終的に PO 違反に到達すること

#### 5.4.6 UNKNOWN生成

- どの条件が不足したかを `missing_lemma` として構造化
- 推奨精密化を `refinement_plan.actions[]` に列挙
- `unknown_code` は原因分類（§3.6.4）

---

### 5.5 Spec DB（specdb/）

#### 5.5.1 役割

- 契約（Pre/Post/Frame/Ownership/Lifetime/Failure/Concurrency）を格納し、解析・証拠・diff に供給。

#### 5.5.2 入力形式（v1 提案）

- ソース内注釈: `//@sappp contract ...`（詳細構文は ADR で確定）
- 外部サイドカー: `specdb/*.json`（v1）

#### 5.5.3 正規化（Contract IR）

- 内部表現は JSON で統一（`contract_ir.v1`）
- 契約対象は clang USR を主キーとして一意解決（REQ-SPC-012）

#### 5.5.4 version_scope の評価順（決定）

1. `abi`（target triple/abi）一致
2. `library_version`（semantic version）一致
3. `conditions`（フラグ、マクロ、OS等）評価

同率の場合は `priority`（数値）で決定、未指定は 0。
それでも複数残る場合は `contract_id` で安定ソートし、全てを適用候補として扱う。

#### 5.5.5 Tier 運用

- Tier0: 実装適合が検証済み
- Tier1: 根拠メタデータ付き採用
- Tier2: 推定（AI含む）→ **SAFE禁止**
- Disabled: 失効

Validator は SAFE 証拠が Tier2 に依存していないことを検査する（REQ-SPC-021）。

---

### 5.6 Certificate Store（certstore/）

#### 5.6.1 役割

- CAS（content-addressed storage）として証拠オブジェクトを保存し、共有参照で重複排除。

#### 5.6.2 カノニカル JSON（hash の前提）

- UTF-8
- オブジェクトキーは辞書順
- 配列は意味順序を持つ場合のみ（それ以外はソートして固定）
- パスは正規化（§10）

#### 5.6.3 ハッシュ範囲（hash_scope.v1）

- 意味に影響するフィールドのみをハッシュ対象
- 人間向け説明（`pretty` や `notes` など）は原則対象外（ただし将来の方針次第）

---

### 5.7 Validator（validator/）

#### 5.7.1 入力

- `nir.json`
- `po_list.json`
- `certstore/`（objects + index）
- `specdb snapshot`

#### 5.7.2 出力

- `validated_results.json`

#### 5.7.3 検証パイプライン（v1）

1. **Schema Check**: すべての入力を JSON Schema で検証
2. **Version Check**: version triple 整合
3. **Hash Check**: CAS オブジェクトの再ハッシュ
4. **Dependency Check**: 参照解決（欠落は MissingDependency）
5. **Proof Check**: proof_system v1 に従う

失敗時は UNKNOWN へ降格し理由コードを付与（REQ-VAL-002）。

#### 5.7.4 proof_system v1（検証規則）

##### SAFE 証拠

- Invariant が CFG 上で帰納的に保たれ、PO地点で predicate を含意すること。

Validator が行うチェック:

1. entry 不変条件が成立（初期状態の表現は v1 では「トップ」または build_snapshot/契約により与える）
2. 各エッジで transfer が不変条件を保存
3. PO地点で `Inv ⇒ predicate` が成立

> v1 では、Validator 内の抽象ドメインは **interval + null/lifetime/init** のみ（Analyzer と同等かそれ以下）。

##### BUG 証拠

- BugTrace が示す実行パスが IR 意味論に従って再生でき、PO違反へ到達すること。

Validator が行うチェック:

1. パスが CFG 上で接続していること（edge 整合）
2. 各命令の効果を v1 意味論で逐次適用
3. 最終地点で predicate が偽（UB条件成立など）であること

#### 5.7.5 UnsupportedProofFeature

- v1 で扱えない証拠要素（SMT proof object 等）が含まれる場合は UNKNOWN に降格。

---

### 5.8 Report/CLI（tools/sappp, report/）

#### 5.8.1 必須コマンド（SRS準拠）

- `sappp analyze`
- `sappp validate`
- `sappp explain`
- `sappp diff`
- `sappp pack`

（補助）
- `sappp capture`（build_snapshot 生成）

#### 5.8.2 I/O 規約

- すべてのコマンドは入出力に `schema_version` を含む JSON を用いる
- 出力は安定ソート + カノニカル化（§10）

#### 5.8.3 analyze（例）

- 入力: `--build build_snapshot.json --spec specdb/snapshot.json`
- 出力ディレクトリ: `--out out/`
  - `nir.json`, `source_map.json`, `po_list.json`, `unknown_ledger.json`, `certstore/`

#### 5.8.4 validate（例）

- 入力: `--in out/`
- 出力: `validated_results.json`

#### 5.8.5 explain（例）

- 入力: `--unknown out/unknown_ledger.json`
- 出力: stdout（既定）、`--out explain.json`（任意）

---

## 6. 決定性・正規化・ハッシュ

### 6.1 安定ソート規約（全出力共通）

- `po_list.pos[]` は `po_id` 昇順
- `unknowns[]` は `unknown_stable_id` 昇順（同値なら `po_id`）
- `validated_results.results[]` は `po_id` 昇順

### 6.2 パス正規化（v1）

- 区切りを `/` に統一（内部表現）
- `.`/`..` を解決
- Windows のドライブ表現は `//?/C:/...` ではなく `C:/...` へ正規化（実装で統一）
- 可能なら repo ルート相対に変換（`repo_identity.path`）

### 6.3 カノニカル JSON（v1）

- UTF-8
- object key の辞書順
- 数値は 10進（余計な 0 なし）
- 浮動小数は v1 では使用禁止（必要なら文字列）

### 6.4 ハッシュ

- `sha256:` 表記
- `hash_scope.v1` に従い、Validator は再計算して一致確認

---

## 7. エラー処理・ログ

### 7.1 Analyzer 側

- 解析失敗/未対応/予算超過は **UNKNOWN** で表現（unknown_code）
- 例外・クラッシュはツール品質問題として別扱い（ただし運用上は UNKNOWN にまとめてもよい）

### 7.2 Validator 側

- 失敗理由は downgrade_reason_code に標準化
- 可能な限り「どの規則が破れたか」を `notes` に残す（ハッシュ対象外）

---

## 8. セキュリティ・堅牢性

- 外部プロセス（将来SMT等）を含め、時間・メモリ制限を適用可能にする。
- 不正入力でもクラッシュ/情報漏洩/任意コード実行を招きにくい設計。
- 解析のサンドボックス化（コンテナ・seccomp等）は段階導入。

---

## 9. 性能・運用（インクリメンタル/キャッシュ）

- TU単位で IR キャッシュ（tu_id をキー）
- PO単位で証拠/検証結果キャッシュ（po_id + version triple + 依存契約digest）
- Spec DB 変更時は影響分析で再検証範囲を限定

---

## 10. テスト設計（Milestone A 必須）

### 10.1 Validator テスト（TCB中心）

- スキーマ検証テスト
- カノニカル化/ハッシュ検証テスト
- SAFE/BUG の最小例（litmus）
- Tier2 混入検査

### 10.2 決定性テスト

- 並列度（jobs）を変えても `po_id` と `category` が一致

### 10.3 E2E テスト

- analyze→validate→pack→diff の一連
- pack から validate が再実行できる（再現性）

---

## 11. 要求トレーサビリティ（要約）

| SRS要求 | 詳細設計要素 |
|---|---|
| REQ-PRIN-001/002 | SAFE/BUG確定は Validator のみ、失敗は UNKNOWN |
| REQ-PRIN-003 / REQ-CRT-001 | PO単位の Certificate（cert.v1） |
| REQ-VAL-001/002/003 | Validator は証拠のみ検証、小さく単純 |
| REQ-IR-001/002/010.. | nir.v1（CFG+イベント、dtor明示等） |
| REQ-PO-001/002 | sink網羅 + po_id 規則 |
| REQ-UNK-001..005 | unknown.v1（missing_lemma/refinement_plan/安定ID） |
| REQ-REP-001/002/003 | pack_manifest.v1 + diff.v1 + 決定性規約 |
| REQ-IN-001/002 | build_snapshot.v1（実コンパイル条件＋target前提） |

---

## 12. 未決事項（次のADRで確定）

1. Spec DB の具体構文（注釈/サイドカー）
2. semantics_version v1 の逸脱点一覧と litmus セットの整備
3. IR 正規化のさらなる微小編集耐性（アンカー強化）
4. 証拠の hash_scope の厳密化（pretty/notes の扱い）
5. Concurrency/Exception の v1 保守的 UNKNOWN 条件の細分化
