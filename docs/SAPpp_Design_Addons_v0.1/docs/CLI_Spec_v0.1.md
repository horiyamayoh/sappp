# SAP++ CLI 仕様（v0.1）

本書は Milestone A（器の完成）に必要な CLI の入出力・オプションを確定する。

- 必須コマンド（SRS準拠）: `analyze`, `validate`, `explain`, `diff`, `pack`
- 補助コマンド: `capture`（build_snapshot生成）

## 1. 共通

### 1.1 グローバルオプション

- `--help` : ヘルプ表示
- `--version` : バージョン表示（tool.version）
- `-v, --verbose` : 詳細ログ
- `-q, --quiet` : 最小ログ（エラーのみ）
- `--json-logs <path>` : ログをJSONLで出力（任意）
- `--jobs <N>` : 並列度（デフォルト: ホストCPU）
- `--schema-dir <dir>` : JSON Schema ディレクトリ（デフォルト: 同梱schemas/）

### 1.2 バージョン三つ組

以下の3つは、結果・証拠・packに **必ず埋め込む**。CLI では明示指定もできる。

- `--semantics <semantics_version>`（例: `sem.v1`）
- `--proof <proof_system_version>`（例: `proof.v1`）
- `--profile <profile_version>`（例: `safety.core.v1`）

> 既定値はリリースごとに固定し、変更する場合はリリースノート/ADRに残す。

### 1.3 入出力の決定性

- 同一入力・同一設定・同一バージョンで、出力（少なくともカテゴリとID）は決定的でなければならない。
- すべての配列出力は、スキーマで定めたキーで安定ソートする。

### 1.4 終了コード（v0.1）

- `0` : コマンド実行成功（解析結果にBUGが含まれても 0）
- `1` : CLI引数/設定エラー
- `2` : 入力不正（スキーマ不一致、ファイル欠落等）
- `3` : 内部エラー（例外・クラッシュ相当）

CIゲート用途は `sappp gate`（将来）または `--policy` を別途導入する。
（Milestone A では exit code に BUG/UNKNOWN を反映しない）

---

## 2. `sappp capture`（補助）

Build Capture により `build_snapshot.json`（`build_snapshot.v1`）を生成する。

### 2.1 使い方

- CMake（推奨）
  - `sappp capture --compile-commands <path/to/compile_commands.json> --out build_snapshot.json`

### 2.2 オプション

- `--compile-commands <path>` : `compile_commands.json` を入力
- `--out <path>` : 出力（既定: `build_snapshot.json`）
- `--repo-root <path>` : リポジトリルート（相対パス化に使用、任意）

### 2.3 出力

- `build_snapshot.json`（schema: `build_snapshot.v1`）

---

## 3. `sappp analyze`

Build Snapshot とソースから IR/PO/証拠候補/UNKNOWN台帳を生成する。

### 3.1 使い方

- `sappp analyze --build build_snapshot.json --out out/`
- `sappp analyze --build build_snapshot.json --spec specdb/snapshot.json --out out/`

### 3.2 オプション

- `--build <path>` : 入力 build_snapshot（必須）
- `--spec <path>` : Spec DB snapshot（任意）
- `--out <dir>` : 出力ディレクトリ（必須）
- `--jobs <N>` : 並列度（任意）
- `--analysis-config <path>` : 解析設定（任意。未指定時は既定）
- `--emit-sarif <path>` : SARIF 出力（任意）
- `--repro-level <L0|L1|L2|L3>` : repro_assets の収集レベル（pack時にも使用、任意）

### 3.3 出力ディレクトリ構成（固定）

`--out out/` の場合:

- `out/frontend/nir.json`（`nir.v1`）
- `out/frontend/source_map.json`（`source_map.v1`）
- `out/po/po_list.json`（`po.v1`）
- `out/analyzer/unknown_ledger.json`（`unknown.v1`）
- `out/certstore/objects/...`（`cert.v1`）
- `out/certstore/index/...`（`cert_index.v1`）
- `out/config/analysis_config.json`（`analysis_config.v1`）

> analyze 時点の SAFE/BUG は「候補」。確定は validate のみ。

---

## 4. `sappp validate`

証拠（Certificate）を入力として検証し、SAFE/BUG を確定、失敗は UNKNOWN に降格する。

### 4.1 使い方

- `sappp validate --in out/ --out out/results/validated_results.json`
- packからの再検証: `sappp validate --in pack/`（packを展開したものを想定）

### 4.2 オプション

- `--in <dir>` : 入力ディレクトリ（必須。analyze出力 or pack展開）
- `--out <path>` : 出力 validated_results（既定: `<in>/results/validated_results.json`）
- `--strict` : schema/version/hash のいずれか不一致で即エラーにする（既定: 降格して継続）

### 4.3 出力

- `validated_results.json`（`validated_results.v1`）

---

## 5. `sappp explain`

UNKNOWN の開拓ガイドを出力する。

### 5.1 使い方

- `sappp explain --unknown out/analyzer/unknown_ledger.json`
- `sappp explain --unknown out/analyzer/unknown_ledger.json --po <po_id>`

### 5.2 オプション

- `--unknown <path>` : unknown_ledger（必須）
- `--validated <path>` : validated_results（任意。UNKNOWNのみフィルタ等に使用）
- `--po <po_id>` : 特定POに絞る
- `--unknown-id <unknown_stable_id>` : 特定UNKNOWNに絞る
- `--format <text|json>` : 出力形式（既定: text）
- `--out <path>` : json出力先（format=jsonのとき）

---

## 6. `sappp pack`

再現パック（`pack.tar.gz`）と `manifest.json` を生成する。

### 6.1 使い方

- `sappp pack --in out/ --out pack.tar.gz`

### 6.2 オプション

- `--in <dir>` : 入力ディレクトリ（必須。analyze+validate成果物）
- `--out <path>` : 出力 tar.gz（既定: `pack.tar.gz`）
- `--manifest <path>` : manifest出力（既定: `manifest.json`）
- `--repro-level <L0|L1|L2|L3>` : 収集レベル
- `--include-analyzer-candidates` : cert_candidates を同梱（任意）

---

## 7. `sappp diff`

before/after の結果差分を出力する。

### 7.1 使い方

- `sappp diff --before <dir_or_pack> --after <dir_or_pack> --out diff.json`

### 7.2 オプション

- `--before <path>` : before（pack.tar.gz または展開ディレクトリ）
- `--after <path>` : after（pack.tar.gz または展開ディレクトリ）
- `--out <path>` : diff 出力（既定: `diff.json`）

### 7.3 出力

- `diff.json`（`diff.v1`）

---

## 8. 追加（将来）

- `sappp gate --policy policy.json` : CIゲート（REQ-OPS-003）
- `sappp schema check` : schema 単体検証
- `sappp cache` : キャッシュの参照/削除
