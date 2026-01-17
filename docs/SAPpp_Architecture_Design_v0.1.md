# SAP++ ソフトウェアアーキテクチャ設計書（SAD） v0.1

- 対象プロダクト: SAP++（Sound, Static Absence-Proving Analyzer for C++）
- 状態: Draft（アーキテクチャ設計 初版）
- 作成日: 2026-01-17
- 入力: 「SAP++ 要求仕様書（SRS）v1.1」および「SAP++ 実現方式検討結果報告書 v1」 

> 本書は、SRSの最上位原則（嘘SAFE/嘘BUGゼロ、Proof-Carrying、Validator確定、UNKNOWN開拓性、C++破綻点の“器”先行、決定性）を **アーキテクチャ不変条件**として満たすための設計を定義する。 

---

## 1. 目的と適用範囲

### 1.1 目的

- **Milestone A（器の完成）** を成立させるために、
  - コンポーネント分割
  - データスキーマ（IR/PO/証拠/UNKNOWN/pack/diff）
  - 決定性（並列でも同一結果）
  - proof_system v1（Validatorが検証可能な証拠規則）
  を先に固定する。 

- 以後の機能追加は Analyzer 側の精密化として単調に積み上げ、Validatorが通らない限り SAFE/BUG は増えない構造を保証する（嘘SAFE/嘘BUG回避）。 

### 1.2 適用範囲

- 対象言語: C/C++（主対象はC++、ただしCを見捨てない） 
- 入力: 実ビルド条件を取り込んだ TU 解析（Make/CMake必須） 
- 出力: 機械可読JSON（+任意でSARIF）、再現パック、差分、UNKNOWN台帳 

### 1.3 非スコープ

- SAFE/BUG確定根拠としての動的テスト・ログ等の利用は禁止（Static-only） 

---

## 2. 設計原則（アーキ不変条件）

SRSの「最上位原則」「アーキテクチャ不変条件」を、そのまま本設計の不変条件とする。 

1. **嘘SAFEゼロ / 嘘BUGゼロ**
   - SAFE/BUGは採用意味論モデルの下で常に真。 
2. **Proof-Carrying Results**
   - SAFE/BUGは必ず証拠（Certificate）を伴う。 
3. **Validator確定**
   - SAFE/BUGは Validator が検証した場合のみ確定。失敗は必ずUNKNOWNに降格。 
4. **UNKNOWN開拓性**
   - UNKNOWNは不足理由・不足補題・開拓計画・安定IDを必ず含む。 
5. **C++破綻点の器先行**
   - 寿命・例外・仮想・並行性・UBを表現できるIR/証拠体系を初期から持つ（精度は段階導入可）。 
6. **二分割（Analyzer/Validator） + TCB最小化**
   - Analyzerは複雑化してよいが、Validatorは小さく単純。 
7. **決定性（並列でも同一結果）**
   - 同一入力・同一設定・同一バージョンで、カテゴリとIDが一致（並列でも同一）。 

---

## 3. 想定ユースケースと受け入れデモ

### 3.1 受け入れデモ（最小）

- “明らかな間違いを含む main()” を入力して以下が成立すること。

1. `sappp analyze` で
   - 少なくとも1件以上の **BUG** が生成される
   - 残りは SAFE/UNKNOWN に分類される
   - UNKNOWNには `unknown_code / missing_lemma / refinement_plan / stable_id` が必ず付与される 
2. `sappp validate` で
   - BUG証拠（実行パス必須）を再生し BUG を確定する
   - 検証失敗は UNKNOWN に降格し、理由コードを出す 
3. `sappp pack` で再現パック（tar.gz + manifest.json）を生成する 
4. `sappp diff` で、PO安定IDをキーに差分が出る
   - 後退（SAFE→UNKNOWN等）は「逸脱点単位」の理由を機械可読に出す 

### 3.2 段階的スケール

- 単一関数 → 複数クラス → 実務コード（数十万LOC）へ段階的に適用
- 「ビルド時間+α」モード／「一晩徹底」モードを設定で切替可能とする

---

## 4. 全体アーキテクチャ

### 4.1 論理コンポーネント

```
[Build Capture]  ---->  build_snapshot.json / compile_units
      |
      v
[Frontend (Clang)] ----> normalized_ir.json + source_map
      |
      v
[PO Generator] --------> po_list.json (stable_id)
      |
      v
[Analyzer]  ------------> certificate_candidates + unknown_ledger
      |
      v
[Certificate Store (CAS/DAG)] <---- shared nodes
      |
      v
[Validator (TCB)] ------> validated_results.json (SAFE/BUG/UNKNOWN)
      |
      v
[Report/CLI]  ----------> JSON/SARIF + pack/diff/explain
```

- 構造は SRS の二分割（Analyzer/Validator）を満たす。 

### 4.2 物理コンポーネント（リポジトリ構造案）

```
repo/
  docs/
    SAD.md (本書)
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
  third_party/
  tests/
    validator/
    determinism/
    schemas/
    end_to_end/
```

### 4.3 実装言語・コンパイル方針

- 第一候補: **C++23**（モダンC++機能の活用・型安全性・コード品質向上）
- ただし、Validatorを別言語（例: Rust）に分離する余地を残す。
  - 具体的には、Validatorは **スキーマ入力（証拠）** のみで完結し、他コンポーネントへリンクしない設計とする。 

---

## 5. データモデル（最優先で固定するI/F）

> AIエージェントによる並列開発の前提として、**スキーマを最初に固定**し、各モジュールはスキーマを契約として実装する。

### 5.1 3種のバージョン

解析結果・証拠・再現パックは必ず以下を埋め込む。 

- `semantics_version`
- `proof_system_version`
- `profile_version`

### 5.2 Build Snapshot（実ビルド条件の完全取り込み）

- 目的: `compile_commands.json` 相当、あるいはビルドフックから **実コンパイル条件**を再現し、同一条件で解析する。 

**主要フィールド（概略）**

```json
{
  "schema_version": "build_snapshot.v1",
  "tool": {"name": "sappp", "version": "..."},
  "host": {"os": "linux|windows", "arch": "x86_64|..."},
  "compile_units": [
    {
      "tu_id": "sha256:...",
      "cwd": "/abs/path",
      "argv": ["clang++", "-D...", "-I...", "-std=c++17", "-c", "x.cpp"],
      "env_delta": {"INCLUDE": "...", "LIB": "..."},
      "response_files": [{"path": "...", "sha256": "...", "content": "optional"}],
      "lang": "c|c++",
      "std": "c11|c++11|c++17",
      "target": {
        "triple": "x86_64-pc-linux-gnu",
        "abi": "sysv|msvc",
        "data_layout": {"ptr_bits": 64, "long_bits": 64, "align": {"max": 16}}
      },
      "frontend": {"kind": "clang|clang-cl", "version": "...", "resource_dir": "..."}
    }
  ]
}
```

### 5.3 Source Map（必須）

- 目的: マクロ展開後でも原ソース位置へ戻す。 
- 仕様: `spelling_loc` / `expansion_loc` / `macro_stack` を保持

### 5.4 Normalized IR（CFG + C++イベント、JSON）

- コア解析・証拠・検証はIRのみを対象とする。 
- IRは寿命/例外/仮想/並行性/UBを表現できる。 

**IR最小構造（概略）**

```json
{
  "schema_version": "nir.v1",
  "tu_id": "sha256:...",
  "semantics_version": "sem.v1",
  "files": [{"path": "...", "sha256": "..."}],
  "types": [{"type_id": "T1", "clang_canonical": "...", "layout": {"size": 16, "align": 8}}],
  "functions": [
    {
      "function_uid": "c:@F@foo#I#",
      "mangled_name": "_Z3fooi",
      "cfg": {
        "entry": "B0",
        "blocks": [
          {"id": "B0", "insts": [
            {"id": "I0", "op": "lifetime.begin", "args": ["obj"], "src": {"file": "...", "range": "..."}},
            {"id": "I1", "op": "call", "callee": "...", "src": {"...": "..."}},
            {"id": "I2", "op": "ub.check", "kind": "shift", "cond": "..."}
          ]}
        ],
        "edges": [{"from": "B0", "to": "B1", "kind": "normal"}, {"from": "B0", "to": "Bex", "kind": "exception"}]
      },
      "tables": {
        "vcall_candidates": [{"id": "CS1", "methods": ["...", "..."]}]
      }
    }
  ]
}
```

#### 5.4.1 IR命令セット（器としての最小核）

- 制御: `branch/switch/ret`
- メモリ: `load/store/alloc/free/memcpy/memmove/memset`
- 寿命: `lifetime.begin/end`, `ctor`, `dtor`, `move`（暗黙デストラクタの明示化を満たす） 
- 例外: `invoke`, `throw`, `landingpad`, `resume`（例外エッジ保持） 
- 仮想: `vcall(receiver, candidateset_id, signature)`（候補集合上界） 
- 並行性: `thread.spawn/join`, `atomic.r/w(order)`, `fence(order)`, `sync.event(kind,obj)` 
- UB: `ub.check(kind, cond)`（UBはPOとして扱う） 
- 網羅性(B)のための補助: `sink.marker(kind, ast_hint)`（IR化困難でもPO漏れを作らない）

### 5.5 PO（Proof Obligation）

- IR上の sink を網羅してPOを生成（漏れ禁止）。 
- POには安定IDを付与し差分追跡する。 

**PO安定ID（推奨構成）** 

- `repo_identity`: **path + content hash**（両方）
- `function_key`:
  - 主: `clang_usr`（function_uid）
  - 副: `mangled_name`（表示/デバッグ用）
- `ir_anchor`: `block_id + inst_id`（正規化規約で安定化）
- `po_kind`
- `semantics_version / profile_version / proof_system_version`

**PO JSON（概略）**

```json
{
  "schema_version": "po.v1",
  "po_id": "sha256:...",
  "po_kind": "OOB|NullDeref|UseAfterLifetime|UninitRead|DoubleFree|UB.Shift|...",
  "profile_version": "safety.core.v1",
  "semantics_version": "sem.v1",
  "proof_system_version": "proof.v1",
  "repo_identity": {"path": "src/a.cpp", "content_sha256": "..."},
  "function": {"usr": "c:@F@foo#I#", "mangled": "_Z3fooi"},
  "anchor": {"block_id": "B12", "inst_id": "I7", "src": {"...": "..."}},
  "predicate": {"expr": {"op": "lt", "lhs": "i", "rhs": "len"}, "pretty": "i < len"}
}
```

### 5.6 UNKNOWN（開拓可能性）

UNKNOWNは成果物であり、必ず以下を持つ。 

- `unknown_code`（粒度は可能な限り詳細）
- `missing_lemma`（ハイブリッド: 機械可読AST + 人間可読pretty）
- `refinement_plan`（人間向けメッセージ + パラメータ列）
- `unknown_stable_id`

```json
{
  "schema_version": "unknown.v1",
  "unknown_stable_id": "sha256:...",
  "po_id": "sha256:...",
  "unknown_code": "MissingContract.Pre|DomainTooWeak.Numeric|VirtualDispatchUnknown|ExceptionFlowConservative|...",
  "missing_lemma": {
    "expr": {"op": "and", "args": [/*...*/]},
    "pretty": "0 <= i && i < len",
    "symbols": ["ssa:%12", "mem:obj#3"],
    "notes": "..."
  },
  "refinement_plan": {
    "message": "推奨: 数値ドメイン強化(octagon) もしくは i に関する分岐分割",
    "actions": [
      {"action": "domain.numeric.octagon", "params": {"max_vars": 64}},
      {"action": "path_split", "params": {"on": "i < len"}}
    ]
  }
}
```

### 5.7 Certificate（証拠）とCAS/DAG

- SAFE/BUGは **PO単位**で証拠を持つ。 
- 共有ノード（不変条件/契約/補題等）を参照共有し重複排除できる。 
- 証拠はカノニカル化規約を持ち、ハッシュ対象範囲を固定する。 

**CASオブジェクト種（例）**

- `PoDef`
- `IrRef`
- `Invariant`
- `BugTrace`（実行パス必須）
- `ContractRef`
- `DependencyGraph`

**CAS保存形式（例）**

- `objects/aa/bb/<sha256>.json`
- `index/<po_id>.json`（PO→ルートハッシュ）

### 5.8 再現パック（pack）

- `tar.gz + manifest.json` を生成する。 
- 必須: 入力ソース、ビルド条件、フロントエンド識別、target、Spec DBスナップショット、各種version、設定、結果digest、証拠hash 

加えて、本設計では「ヘッダ/標準ライブラリ再現」を拡張可能にするため、Repro Assetを導入する（§7参照）。

---

## 6. 決定性設計（並列でも同一結果）

SRSの決定性要求を満たすため、以下を固定する。 

### 6.1 出力の安定化規約

- すべての集合は **安定ソート**して出力する
  - 原則キー: `po_id` → `unknown_stable_id` → `function_uid` → `ir_anchor` → `po_kind`
- 並列実行での集約は「並列で計算 → 単一スレッドで安定マージ」を原則とする

### 6.2 カノニカルJSON

- 証拠（Certificate）とpack manifest はカノニカル化規約に従ってシリアライズする。 
- カノニカル化規約（v1案）
  - 文字列はUTF-8
  - オブジェクトキーは辞書順
  - 数値表現は10進・余計な0なし（ただし将来の仕様化をADR化）
  - pathは正規化（区切り、`.`/`..`除去、ドライブ表現等）

### 6.3 ハッシュ対象範囲

- `hash_scope.v1` として固定し、Validatorも同規約で検証する。 
- UI向け説明文など「見た目のみ」は原則としてハッシュ対象外（改ざん検出に必要な意味部分は必ず対象）

---

## 7. 入力・ビルド整合（Build Capture / Frontend）

### 7.1 Build Capture

#### 7.1.1 取り込み方式

- CMake:
  - `CMAKE_EXPORT_COMPILE_COMMANDS=ON` を利用し compile_commands を取得
- Make:
  - コンパイララッパ（`CC=sappp-cc`, `CXX=sappp-cxx`）を提供し、実行されたコンパイルを収集
  - response file（`.rsp`）を展開し内容も記録

#### 7.1.2 必須取り込み項目

- `-D/-I/-isystem/-std=/-target/--sysroot` 等
- target triple / ABI / 主要型size/align（結果・証拠・packに記録） 

### 7.2 ヘッダ/標準ライブラリの再現（拡張可能）

- 目的: 解析品質の向上と再現性の段階的強化
- Repro Level（提案）
  - **L0**: Fingerprint only（include探索パス + ツールチェーン識別 + 主要ヘッダhash一覧）
  - **L1**: Used headers snapshot（TUの依存ヘッダのみ収集）
  - **L2**: Sysroot snapshot（sysrootや標準ライブラリディレクトリをスナップショット）
  - **L3**: Container pinning（Docker digest等）

packは `repro_assets/` に任意追加できる設計とし、精度向上に合わせて段階導入する。

### 7.3 Frontend（Clang Tooling）

- Linux: `clang` driver
- Windows/MSVC: `clang-cl`（driver-mode=cl）
- 役割
  - AST/CFGから Normalized IR を生成
  - Source Mapを生成（必須）
  - C++破綻点イベントの明示化（寿命/例外/仮想/並行性/UB） 
  - `operator[]` 等を関数呼び出しに正規化し、契約適用可能化 

---

## 8. PO生成（Safety.Core）

### 8.1 初期のPO種別（提案）

> 初期の網羅範囲は段階導入で良いが、導入した範囲については“漏れ禁止”。 

Safety.Core（初期セット案）:

- メモリ安全:
  - OOB（配列/ポインタ演算/`operator[]`）
  - Null dereference
  - Use-after-lifetime（寿命切れ参照）
  - Double free / Invalid free
  - Uninitialized read（初期化状態）
- UB（BUGクラス）:
  - Divide by zero
  - Shift UB
  - Signed overflow（方針はsemantics_versionで固定）
  - misaligned access など（段階導入）

### 8.2 網羅性(B)の実現

- ソースでsinkを検出したがIR化が不完全な場合でも、Frontendが `sink.marker` をIRに挿入してPO生成を保証する。
- 正当化不能（semantics逸脱や未対応）は **UNKNOWNに倒す**（嘘SAFE/嘘BUG回避）。 

---

## 9. Analyzer（関数間優先・Anytime導線）

### 9.1 基本戦略

- 抽象解釈を基盤とする（停止性を満たす）。 
- 早期から **関数間**を優先する（summary駆動）。

### 9.2 Summary設計

- `FunctionSummary` をCASノードとして表現し、PO証拠が参照できるようにする。
- Validatorが検証可能な範囲に落とせないsummaryは、SAFE根拠に使用しない（UNKNOWNへ）。 

### 9.3 停止性（TU予算）

- 予算単位はTU。
- 超過は必ずUNKNOWN（BudgetExceeded） 

### 9.4 局所SMT（初版は実行しない）

- Analyzerは `refinement_plan` に局所SMTを提案できる器を持つ。 
- ただし初版は実行しない（proof_systemやTCB肥大化の回避）

---

## 10. Spec DB（契約）

### 10.1 入力

- ソース内注釈 + 外部サイドカーを受理し、内部で単一Contract IRへ正規化する。 

### 10.2 対象解決

- オーバーロード/テンプレ/cv-ref/noexceptを含め、適用対象を一意に解決する。 
- 解決不能（競合/矛盾）は保守的にUNKNOWNへ倒す。 

### 10.3 Tier運用

- Tier0/1/2/Disabled を実装し、Tier2はSAFEに使用禁止。 
- Validatorは、SAFE証拠がTier2に依存していないことを検査できるよう、証拠に依存契約情報を含める。

### 10.4 反証（Implementation ⊭ Contract）

- 契約反証を検出した場合、契約をDisabled（またはTier2へ降格）し、依存SAFEをUNKNOWNへ降格する。 

---

## 11. Certificate / Validator（proof_system v1）

### 11.1 proof_system v1 の狙い

- Validatorが **証拠のみ**で決定的に検証できること。 
- TCB最小化（Validatorは小さく単純）。 

### 11.2 SAFE証拠（v1）

- 証拠は「CFG各点の不変条件（抽象状態）」と「PO含意」から成る。
- Validatorは以下をチェックする。
  1. 入口条件から不変条件が成立
  2. 各命令のtransferで不変条件が保存（帰納性）
  3. PO地点で不変条件がPO述語を含意

### 11.3 BUG証拠（v1）

- BUG証拠は **実行パスを必須**とする。
- ValidatorはIR意味論に沿ってパスを再生し、PO違反へ到達することを確認する。 

### 11.4 検証失敗理由コード（最小セット）

- `SchemaInvalid`
- `VersionMismatch`
- `HashMismatch`
- `MissingDependency`
- `RuleViolation`
- `ProofCheckFailed`
- `UnsupportedProofFeature`

検証失敗は必ずUNKNOWNへ降格する。 

---

## 12. CLI / 出力 / diff / explain

### 12.1 CLI（最低限）

SRSの最低限CLIを提供する。 

- `sappp analyze`
- `sappp validate`
- `sappp explain`
- `sappp diff`
- `sappp pack`

（実装上は補助として `sappp capture` を追加して良い）

### 12.2 出力

- 結果は機械可読JSON（スキーマはバージョン管理） 
- 任意でSARIFを出力可能とする 
- explainはUNKNOWN開拓ガイド（分類、missing_lemma、refinement_plan、依存情報）を人間可読で出す 

### 12.3 diff

- PO安定IDをキーに、SAFE/BUG/UNKNOWNの推移を出力
- SAFE→UNKNOWN等の後退理由を機械可読に出す（逸脱点単位） 

---

## 13. セキュリティ・堅牢性

- 外部プロセスを含め時間・メモリ制限を適用できること。 
- 不正入力でもクラッシュ/情報漏洩/任意コード実行を招きにくい設計。
- 解析のサンドボックス化は推奨（社内運用前提のため段階導入可）。 

---

## 14. テスト戦略（TCB中心）

- Validator
  - スキーマ検証テスト
  - カノニカル化/ハッシュ検証テスト
  - SAFE/BUGの最小例（litmus）での検証テスト
- 決定性
  - 並列度を変えても結果が同一であることのテスト（REQ-REP-002） 
- E2E
  - analyze→validate→pack→diff のシナリオ

---

## 15. ADR一覧（本書での決定事項）

- ADR-01: Build Capture（CMake/Make）と実コンパイル条件の完全取り込み
- ADR-02: FrontendはClang Tooling（clang/clang-cl）
- ADR-03: 自前Normalized IR（CFG + C++イベント）、JSON、SourceMap必須
- ADR-04: PO生成はIR上のsink網羅（漏れ禁止）。網羅性(B)をsink.markerで担保
- ADR-05: PO安定IDは path+hash + USR中心（mangled併記） + IRアンカー
- ADR-06: 証拠はPO単位 + CAS/DAG共有 + カノニカル化 + ハッシュ範囲固定
- ADR-07: Validatorは証拠のみ検証、失敗はUNKNOWN、proof_system v1を決定的規則に限定
- ADR-08: Analyzerは関数間優先。局所SMTは初版で実行しない（拡張点のみ）
- ADR-09: ヘッダ/標準ライブラリ再現はRepro Asset（L0〜L3）で段階導入

---

## 16. 未決事項（次の設計深化で確定する）

1. カノニカルJSONの厳密仕様（数値表現、浮動小数、NaNの扱い等）
2. IR正規化規約の詳細（ブロック順序付け、命令ID付与の完全ルール）
3. semantics_version v1 の逸脱点一覧と最小例セット（C++11/17差も含む） 
4. Spec DBの具体フォーマット（YAML/JSON/TOML）とソース注釈構文
5. Concurrency/Exception の初期“保守的UNKNOWN”判定条件と unknown_code 体系の確定

---

## 付録A. 用語

- PO, SAFE, BUG, UNKNOWN, Certificate, Validator, TCB, Spec DB 等はSRSの定義に従う。 
