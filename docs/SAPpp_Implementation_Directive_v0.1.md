# SAP++ 実装指示書（Implementation Directive）v0.1

- 対象プロダクト: SAP++（Sound, Static Absence-Proving Analyzer for C++）
- 目的: **Milestone A（器の完成）を最短で成立**させ、以後の精密化（Milestone B/C/D）を安全に積み上げられる「実装の骨格」を作る。
- 入力: SRS v1.1 / SAD v0.1 / 実現方式検討結果報告書 v1 / DDD v0.1 / Design Add-ons v0.1
- 想定読者: 生成AI（実装担当） + 人間レビュア

> **重要（最上位原則）**
> - Analyzer は不信でよいが、SAFE/BUG は **Validatorが通った場合のみ確定**。
> - 検証失敗は必ず UNKNOWN に降格。
> - 証拠は PO 単位。UNKNOWN は「不足理由＋不足補題＋開拓計画＋安定ID」を必ず出す。
> - 同一入力・同一設定・同一バージョンで結果（少なくともカテゴリとID）は **決定的**。

---

## 0. 本指示書で固定する「実装スコープ」

### 0.1 Milestone A の合格条件（Definition of Done）

以下のE2Eが **ローカルで再現可能**かつ **決定的**に動くこと。

1) `sappp capture` が `build_snapshot.v1` を生成できる
2) `sappp analyze` が以下を生成できる（すべてJSON Schemaに適合）
   - `nir.v1`（最低限: CFG + ub.check を出せる）
   - `po.v1`（少なくとも div0 / null / oob のいずれかをPO化）
   - `unknown.v1`（UNKNOWNは必須要素を満たす）
   - `cert.v1`（BUGまたはSAFEの証拠候補を最低1件以上）
3) `sappp validate` が以下を達成する
   - BUG証拠: BugTrace を再生し PO違反を確認できる（v1の最小意味論で良い）
   - SAFE証拠: 形式上の検証（帰納性/含意）が通るか、未対応なら UNKNOWNへ降格（嘘SAFE禁止）
4) `sappp pack` が `tar.gz + manifest.json(pack_manifest.v1)` を生成できる
5) `sappp diff` が before/after の `validated_results.v1` を PO ID キーで比較し `diff.v1` を出せる
6) `--jobs` を変えても、カテゴリとID（po_id / unknown_stable_id / cert hashes）が一致する

### 0.2 Milestone A の「非ゴール」

- 高精度な関数間解析（Analyzerの賢さ）は不要。**UNKNOWNが多くてよい**。
- 全C++機能の完全サポートは不要（ただし IR/スキーマとしての「器」は最初から持つ）。
- SMTをValidator内で回さない（proof.v1は決定的検証のみ）。

---

## 1. 実装方針の最重要チェックポイント

### 1.1 TCB境界（絶対に破らない）

- Validator は **証拠＋IR参照＋スキーマ＋ハッシュ**だけで検証する。
- Validator が Frontend（Clang）や Analyzer を再実行/再解釈し始めたら失格。
- Analyzer は「証拠候補」を作るだけ。**嘘SAFE/嘘BUGを出しても Validator が落とす構造**を崩さない。

### 1.2 決定性（実装で一番壊れやすい）

- **配列は必ず安定ソートして出力**。
- 並列実行は「並列計算 → 単一スレッドで安定マージ」。
- ハッシュの対象となる JSON は **カノニカル化（canonical JSON）**で完全一致。

### 1.3 v1で禁止するもの（ハッシュ一致のため）

- 証拠のハッシュ対象部分での **浮動小数**（NaN/Inf含め禁止）
- ハッシュ対象部分での「自由文」依存（pretty/notesは原則ハッシュ対象外）

---

## 2. リポジトリ初期セットアップ（生成AIに渡す前提）

### 2.1 推奨ディレクトリ構造

DDD案をそのまま採用する（差異を作らない）。

```
repo/
  docs/
    SAD.md
    DDD.md
    ADR/
    schemas/
    semantics/
    profiles/
    proof_system/
  schemas/
  tools/
    sappp/
  libs/
    common/
    canonical/
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
    schemas/
    validator/
    determinism/
    end_to_end/
      litmus_div0/
      litmus_null/
      litmus_oob/
  third_party/
```

### 2.2 言語と依存

- 第一候補: C++23（モダンC++機能の活用・型安全性・コード品質向上）
- JSON: `nlohmann::json`（または同等）
  - **ordered_json** または独自canonical serializerでキー順固定
- sha256: 依存追加を避けるなら単体実装を `libs/common/sha256.*` に同梱
- tar.gz: `libarchive` または最小実装（外部依存の有無はプロジェクト都合で選択）

> ValidatorをRust等に分離するのは **将来**（ADR-0105）。Milestone AはC++単一Repoで良い。

---

## 3. 共通仕様（全モジュールが守る契約）

### 3.1 3種バージョン（全成果物へ埋め込み）

- `semantics_version` 例: `sem.v1`
- `proof_system_version` 例: `proof.v1`
- `profile_version` 例: `safety.core.v1`

### 3.2 出力の安定ソート規約（v1）

- `po.v1` の `pos[]` は `po_id` 昇順
- `unknown.v1` の `unknowns[]` は `unknown_stable_id` 昇順
- `validated_results.v1` の `results[]` は `po_id` 昇順
- `cert_index.v1` の `entries[]` は `po_id` 昇順
- `nir.v1` の `functions[]` は `function_uid` 昇順
  - `blocks[]` は `block_id` 昇順
  - `insts[]` は `inst_id` 昇順

### 3.3 パス正規化（v1）

- 内部表現の区切りは `/`
- repo-root 相対パスを優先（`--repo-root` が与えられれば必ず相対化）
- repo外パスは `external/<hash>/...` へ写像（または `external:` prefix）
- Windowsのドライブ/UNC/大小文字問題は **正規化関数に閉じ込める**

### 3.4 カノニカルJSON（v1）

- 参照: ADR-0101（Design Add-ons）
- ルール（実装必須）
  - UTF-8
  - オブジェクトキーは辞書順
  - 配列は「仕様で順序が意味を持つ」場合のみ順序維持（それ以外はソートしてから出力）
  - 数値は整数のみ（浮動小数禁止）
  - 改行/空白はハッシュ対象外（canonical serializerは最小表現を出力）

### 3.5 エラーと降格（Validator）

- `validated_results.v1` の downgrade reason は schemaのenumに固定。
- `--strict` が無い限り、検証できない/不一致は **UNKNOWNへ降格して継続**。

### 3.6 証拠DAG（certstore）の最小形（v1実装で迷わないための規約）

certstore は「**1ファイル = 1 CASオブジェクト**」で保存する。

#### 3.6.1 PO 1件（BUG候補）の最小オブジェクト集合

v1の実装を詰まらせないため、まずは以下の **5種** に限定して良い。

1. `PoDef`（当該POの定義）
2. `IrRef`（POアンカーの参照）
3. `BugTrace`（反例トレース。TraceStep内に IrRefObj を埋め込み可能）
4. `DependencyGraph`（依存グラフ。v1は「説明用」で良く、Validatorは必須扱いにしない）
5. `ProofRoot`（PO単位のルート。Validator入力の起点）

最小DAGのイメージ:

```
ProofRoot (root)
  ├─ po       -> PoDef
  ├─ ir       -> IrRef
  ├─ evidence -> BugTrace
  └─ depends  -> (contracts=[]; assumptions=["depgraph_ref=sha256:..."])  # v1の便宜

DependencyGraph
  nodes = [PoDef, IrRef, BugTrace]
  edges = [PoDef -("anchor")→ IrRef, PoDef -("evidence")→ BugTrace]
```

> **注**: `ProofRoot` スキーマに `depgraph` フィールドは無い（v1設計の都合）。
> v1では上記のように `depends.assumptions` に参照文字列として載せる運用で開始する。
> 将来 `cert.v1.1` などで明示フィールドを追加して良い。

#### 3.6.2 PO 1件（SAFE候補）の最小オブジェクト集合

SAFE側は `BugTrace` の代わりに `Invariant` を evidence として使う。

```
ProofRoot (root)
  ├─ po       -> PoDef
  ├─ ir       -> IrRef
  ├─ evidence -> Invariant
  └─ depends  -> ...
```

#### 3.6.3 循環参照を作らない

- `DependencyGraph.nodes` に **ProofRoot自身のhash** を入れると、
  - ProofRoot は DependencyGraph のhashを参照し
  - DependencyGraph は ProofRoot hash を含む
  という循環が発生し得る。
- v1では **DependencyGraphにProofRootは含めない**。

### 3.7 cert_index の意味

- `certstore/index/<po_id>.json` は「PO → ProofRoot hash」の対応。
- `validate` は index を列挙して各POの ProofRoot を検証する。
- analyze時点の ProofRoot は「候補」であり、validateが通るまで確定しない。

---

## 4. 実装ワークパッケージ（AIに渡す単位）

> 各WPは「入力→出力」「DoD（受入）」「テスト」を明記する。
> 生成AIには **WP単位で実装→テスト→レビュー**させる。

### WP0: スキーマ同梱とスキーマ検証ハーネス

**目的**: すべてのJSONをJSON Schemaで機械検証できるようにする。

- 入力: `schemas/*.schema.json`（Design Add-ons v0.1をそのまま配置）
- 出力: `tools/sappp schema-check`（内部コマンドでも可）

**実装**
- `libs/common/schema_validate` を作る
- analyze/validate/pack/diffは、生成/読込時にスキーマ検証を行い、失敗を `SchemaInvalid` として扱う

**DoD**
- テストで、同梱schemaに対しサンプルJSONが通る/落ちるを確認

---

### WP1: common（ハッシュ・パス正規化・安定ソート）

**目的**: 全成果物の決定性の根幹を先に固定する。

- 出力: `libs/common` に以下を実装
  - `normalize_path(input, repo_root?) -> normalized`
  - `sha256(bytes) -> hex`
  - `stable_sort_*`（PO/UNKNOWN/RESULT等の比較関数群）

**DoD**
- Golden test: Windows風パス/UNIXパスを入力して同一の正規形になること

---

### WP2: canonical JSON serializer（ハッシュ一致の最難所）

**目的**: `cert.v1` と manifest 等のハッシュ一致を成立させる。

- 出力: `libs/canonical` に以下
  - `canonicalize_json(value, scope) -> bytes`
  - `hash_canonical_json(value, scope) -> sha256:...`

**hash_scope（v1）**
- `cert.v1` の `pretty` / `notes` 等は scope=ui としてハッシュ対象外
- 検証に必要な部分（PO参照/IR参照/不変条件/トレース/依存契約/前提）は scope=core

**DoD**
- Golden test（最重要）: 同一入力から常に同一bytesが出る（OS・並列度差で変わらない）

---

### WP3: CertStore（CAS + Index）

**目的**: PO単位証拠を content-addressed で保存・共有する。

- 入力: `cert.v1`（証拠オブジェクト）
- 出力:
  - `out/certstore/objects/<sha256prefix>/<sha256>.json`
  - `out/certstore/index/<po_id>.json`（cert_index.v1）

**実装**
- `put(cert_json) -> cert_hash`（canonical hashで格納）
- `get(cert_hash) -> cert_json`
- `bind_po(po_id, cert_root_hash)`（index更新）

**DoD**
- 同一certを2回putしても同一hash/同一パスになる

---

### WP4: Validator（proof.v1）

**目的**: TCBの核。証拠のみで SAFE/BUG/UNKNOWN を確定する。

- 入力: analyze出力ディレクトリ（またはpack展開）
- 出力: `validated_results.v1`

**v1の検証範囲（最小）**
- 共通:
  - schema/version一致
  - hash一致（certオブジェクトのハッシュ再計算）
  - 依存欠落は `MissingDependency`
- BUG:
  - BugTrace（単一関数、整数/NULL、分岐パス）を再生
  - 最終ステップでPO predicate が破れることを確認
- SAFE:
  - v1では「単純ドメイン（interval/nullness）」の帰納性チェックのみ
  - 未対応命令や未対応ドメインが混ざる場合は UNKNOWNへ降格（`UnsupportedProofFeature`）

**DoD**
- 3つのlitmus（div0/null/oob）で BUG が Validator により確定する
- 失敗ケース（改ざん/欠落/不一致）で UNKNOWN降格 + reason code が出る

---

### WP5: CLI骨格（sappp）

**目的**: analyze/validate/pack/diff/explain/capture を固定I/Fで提供する。

- 参照: CLI_Spec_v0.1

**コマンド**
- `capture`: compile_commands→build_snapshot
- `analyze`: build_snapshot→(nir,source_map,po,unknown,certstore)
- `validate`: certstore→validated_results
- `pack`: out→pack.tar.gz + manifest.json
- `diff`: before/after→diff.json
- `explain`: unknown_ledger→text/json

**DoD**
- `--help` `--version` `--schema-dir` `--jobs` が動作
- CLIの入出力パスが仕様どおり

---

### WP6: Build Capture（capture）

**目的**: 実コンパイル条件の再現（REQ-IN-001/002）。

- 入力: `compile_commands.json`
- 出力: `build_snapshot.v1`

**実装**
- compile_commands の各エントリを canonicalize して `tu_id` を生成
- response file（@file）展開は v1では **最小限**で良いが、入出力はスキーマに従う

**DoD**
- 同一compile_commandsで tu_id が決定的

---

### WP7: Frontend（Clang）→ NIR/SourceMap

**目的**: 「器としてのIR」を生成する。

- 入力: `build_snapshot.v1` の compile_units
- 出力:
  - `nir.v1`
  - `source_map.v1`

**v1で必須にするIR生成**
- CFGブロック列挙
- 主要命令（最低限）
  - `assign`, `binop`, `cmp`, `branch`, `ret`
  - `load`, `store`（アドレスが明示できる範囲）
  - `ub.check`（div0/shift/oobのどれかを表現できる）
- “器”として存在させるだけでよい命令（生成は後回し可）
  - `lifetime.*`, `invoke/throw/landingpad`, `vcall`, `atomic.*`, `thread.*`

**DoD**
- litmusのTUから nir.json が生成され、po generator が読み取れる

---

### WP8: PO Generator（sink網羅）

**目的**: IR上のsinkからPOを **漏れなく**作る（漏れ禁止）。

- 入力: `nir.v1`
- 出力: `po.v1`

**v1のPO対象（最低）**
- `UB.DivZero` か `NullDeref` か `OOB` のいずれか（litmusをBUG確定させるため）
- それ以外は `sink.marker` から PO化して UNKNOWNへ誘導しても良い

**DoD**
- litmusで少なくとも1件以上の PO が生成される

---

### WP9: Analyzer（v0: stubでOK）

**目的**: 「証拠候補＋UNKNOWN台帳」を生成し、Validatorが確定する流れを成立させる。

- 入力: nir + po + specdb
- 出力:
  - cert objects（BUG候補を最低1件）
  - unknown_ledger（必須情報付き）

**v1で許される最小Analyzer**
- 例: `ub.check` の条件がコンパイル時に常にfalseと判定できる場合のみBUG証拠を出す
- それ以外はUNKNOWN（missing_lemma/refinement_plan付き）

**DoD**
- BUG候補の証拠が Validator で通る
- UNKNOWNがスキーマ必須項目を満たす

---

### WP10: pack / diff / explain

**目的**: SDD運用（再現・回帰・差分）を成立させる。

- pack入力: analyze+validate成果物
- pack出力: `pack.tar.gz` と `manifest.json(pack_manifest.v1)`

**pack必須同梱（v1）**
- build_snapshot
- nir + source_map
- po_list
- unknown_ledger
- validated_results
- certstore objects + index
- specdb_snapshot（空でもよいがスキーマ適合）
- 3種バージョン情報

**diff**
- before/after の `validated_results` を PO ID で突合
- 回帰理由（SAFE→UNKNOWN等）は `diff.v1` の reason に落とす（設定/バージョン差はmanifestから拾う）

**explain**
- unknown_ledger を human-readable へ整形

**DoD**
- litmusで pack が生成でき、展開して validate が再実行できる
- diffが安定出力される

---

## 5. テスト計画（Milestone Aに必須）

### 5.1 Unit / Golden

- canonical JSON golden（最重要）
- sha256 golden
- path normalization golden
- schema validation（valid/invalid）

### 5.2 Validator テスト

- BUG: div0/null/oob の3ケース
- 改ざん: cert JSONの一部を書き換え → HashMismatch → UNKNOWN降格
- 欠落: 依存certが無い → MissingDependency → UNKNOWN

### 5.3 Determinism

- `--jobs=1` と `--jobs=8` で
  - po_id集合が一致
  - unknown_stable_id集合が一致
  - validated_resultsが一致
  - pack manifest の digest が一致

### 5.4 E2E（受入デモ）

- `capture → analyze → validate → pack → diff` を tests/end_to_end で自動化

---

## 6. 生成AIに渡す「作業指示テンプレ」

生成AIには、以下のテンプレで指示すると事故が減る。

- 目的: WP番号と達成条件（DoD）
- 変更してよいファイル範囲: `libs/<module>/`, `tools/sappp/`, `tests/<scope>/` などを限定
- 禁止事項:
  - ValidatorからFrontend/Analyzerを呼ばない
  - スキーマ/CLI仕様の変更（変更したい場合はADRとschema version更新が先）
- 追加テスト: 変更に対応するunit/e2eを必ず追加

---

## 7. 未決事項（実装と並走で決めるが、最低限ログ化する）

- sem.v1 のベースライン規格と逸脱点一覧（REQ-SEM-001/002/003）
- BugTraceのモデルの拡張（関数呼び出し/例外/RAII）
- IRアンカーの微小編集耐性（po_id揺れの緩和）
- SpecDB authoringフォーマット（注釈/サイドカーのv1確定）

---

## 8. 付録: 参考ドキュメント

- SRS v1.1（要求）
- SAD v0.1（アーキ）
- DDD v0.1（詳細設計）
- Design Add-ons v0.1（JSON Schema / ADR / CLI / Open Issues）
