# SAP++ 実現方式検討結果報告書（要求仕様 v1.1 入力）

- 文書種別: 実現方式検討結果報告書（アーキ設計インプット）
- 対象プロダクト: SAP++（Sound, Static Absence-Proving Analyzer for C++）
- 入力仕様: SAP++ 要求仕様書（SRS）v1.1
- 本書の位置づけ: 次工程「ソフトウェアアーキテクチャ設計」開始に向けた、方式（構造・データ・検証・運用）方針の確定

---

## 1. 背景と目的

SRSが定義するSAP++の本質的価値は、静的解析の「賢さ」そのものではなく、次の契約を守り切ることにあります。

- **嘘SAFEゼロ／嘘BUGゼロ**
- **SAFE/BUGは必ず証拠（Certificate）を伴う**
- **SAFE/BUG確定はValidatorが通った場合のみ**（検証失敗はUNKNOWNへ降格）
- **UNKNOWNは“開拓可能”であること（不足理由・不足補題・開拓計画・安定ID）**
- **C++の破綻点（寿命/例外/仮想/並行性/UB）を“表現できる器”を初期から持つ**

本方式検討では、上記契約を満たすために **「Analyzerは不信でよいが、Validator（TCB）を小さく保てる証拠体系」** を先に固定し、以後の機能追加が **Analyzer側の精密化でUNKNOWNを減らす方向に単調に積み上がる**構造を選定します。

---

## 2. スコープ

本報告書で扱う方式検討対象は以下です（アーキ設計に直結する領域）。

- フロントエンド／ビルド条件再現方式
- 正規化IR（Normalized IR: CFGベース＋C++イベント表現）
- PO（Proof Obligation）生成と安定ID
- Analyzer方式（停止性・Anytime/Refinement含む）
- UNKNOWN仕様のデータモデル（不足補題・開拓計画・台帳）
- Spec DB（契約）方式（入力、対象解決、Tier、差分・影響）
- Certificate方式（PO単位、共有、カノニカル化、ハッシュ範囲）
- Validator方式（TCB最小化、証拠のみ検証）
- 再現性（pack/diff/決定性）と大規模運用（キャッシュ/CI）
- セキュリティ（リソース制限等）

---

## 3. 要求から導かれる「方式上の不変条件」

方式を選ぶ前に、SRSから「方式で絶対に守るべき制約」を抜き出します。

### 3.1 二分割（Analyzer / Validator）とTCB最小化

- Analyzerは複雑化してよいが、SAFE/BUG確定はValidatorでの検証を通過した場合のみ
- Validatorは小さく単純（TCB最小化）で、検証失敗はUNKNOWNへ降格

→ **「証拠がValidatorでチェック可能である」ことが方式の中心要件**になります。

### 3.2 静的根拠のみ（Static-only）

- 動的テストやログ等は、SAFE/BUG確定の根拠に使えない

→ **証拠は完全に静的成果物で完結**しなければなりません。

### 3.3 “器”先行：C++破綻点を表現できるIR/証拠体系

- 寿命・例外・仮想・並行性・UBを表現できるIRと証拠体系が初期から必要（精度は段階導入可）

→ 初期から **表現力（データモデル）を欠く方式**は採用できません。

### 3.4 PO単位の証拠と安定ID・決定性

- POは漏れなく生成され、PO単位でSAFE/BUG確定できる
- POには安定IDが付き、差分追跡できる
- 同一条件でカテゴリとIDが決定的（並列でも同一）

→ **IRと証拠の正規化・カノニカル化・順序の固定**が方式として必須です。

---

## 4. 方式結論サマリ（結論一覧）

| 領域 | 結論（推奨方式） | 目的（要求意図） |
|---|---|---|
| フロントエンド | **Clang ToolingでTU解析**（compile_commands再現） | 実コンパイル条件の再現／契約対象解決の一意化 |
| 正規化IR | **自前Normalized IR（CFG＋イベント）**を採用 | C++破綻点を“器”として表現、PO安定IDと決定性確保 |
| PO生成/ID | IR上のsink網羅＋**PO安定ID規則**を固定 | diff/台帳/再現性の根幹 |
| Analyzer | **抽象解釈を基盤**に、局所SMT等で精密化（Anytime） | 停止性を担保しつつUNKNOWNを減らす導線 |
| UNKNOWN | **不足理由コード＋不足補題＋開拓計画＋安定ID**を必ず出す | UNKNOWN開拓可能性（プロダクト価値） |
| Spec DB | 注釈＋サイドカー→単一Contract IR、Tier運用、影響分析 | 契約駆動・大規模導入・AI契約(Tier2)の統制 |
| Certificate | **PO単位**＋共有参照（DAG/CAS）＋カノニカル化＋ハッシュ範囲固定 | Proof-Carrying Results／改ざん検出／運用可能性 |
| Validator | 証拠のみで検証、**小さく単純**、失敗はUNKNOWN | 嘘SAFE/BUGゼロ、TCB最小化 |
| 再現/運用 | pack/diff/決定性、キャッシュ、CIポリシー | SDD運用（回帰・差分・CI統合） |

---

## 5. 方式検討：主要選択肢と採否

### 5.1 IR生成方式（最重要の方式分岐）

#### 選択肢A：Clang AST/CFG → 自前Normalized IR（CFGベース）

- **採用（推奨）**
- 根拠:
  - C++固有イベント（寿命/例外/仮想/並行性/UB）を **解析・証拠・検証の共通言語**としてIRに埋め込める
  - PO安定ID・決定性・diff/packの要件を満たしやすい（正規化規約を主導できる）
  - 契約対象解決（オーバーロード/テンプレ/修飾）の一意性をClangの名前解決結果に基づき保証しやすい

#### 選択肢B：LLVM IRを解析IRに採用

- **不採用（当面）**
- 不採用理由（要求との整合が悪い）:
  - 最適化やフラグ差でIRが大きく変動し、PO安定ID・決定性・差分追跡の難度が上がる
  - ソースレベルの契約適用や候補集合（仮想呼び出し）等、SRSが求める“器”を説明可能な形で保持しづらい

#### 選択肢C：既存解析器をベースに証拠化だけ後付け

- **不採用**
- 不採用理由:
  - “Proof-Carrying + 小さなValidator” を後付けするとTCBが肥大化しやすく、最上位原則に反しやすい

---

## 6. 推奨実現方式（詳細）

### 6.1 全体構造（Analyzer / Validator 二分割）

#### 6.1.1 コンポーネント構成（論理）

- **Frontend**
  - compile_commands入力
  - TU解析（Clang）
  - Source map（可能なら）
  - Normalized IR生成
- **PO Generator**
  - IR上のsink抽出
  - PO生成（漏れ禁止）
  - PO安定ID付与
- **Analyzer**
  - POごとに SAFE/BUG/UNKNOWN を「証拠候補」付きで生成
  - UNKNOWNは不足理由・不足補題・開拓計画を必ず付与
- **Certificate Store（CAS/DAG）**
  - PO単位の証拠を保存
  - 共有ノード（不変条件・契約参照等）を重複排除
- **Validator（TCB）**
  - 証拠のみで検証し、SAFE/BUGを確定
  - 失敗時はUNKNOWNへ降格＋理由分類
- **Report/CLI**
  - analyze / validate / explain / diff / pack
  - JSON出力（必要ならSARIF）

#### 6.1.2 データフロー（概念）

```text
compile_commands + sources
        |
     [Frontend: Clang]
        |
   Normalized IR  ----->  (cache)
        |
   [PO Generator]
        |
     PO List (stable_id)
        |
     [Analyzer] -----> (UNKNOWN ledger)
        |
  Certificate Candidates
        |
     [Validator]  ---> SAFE/BUG/UNKNOWN(降格含む)
        |
   Reports (JSON/SARIF) + pack/diff
```

---

### 6.2 フロントエンド／ビルド整合方式

#### 実現方針

- `compile_commands.json` 相当から **実コンパイル条件を再現**しTUを解析
- 解析結果／証拠／再現パックに以下を必ず埋め込む
  - target triple、主要型サイズ・アラインメント等
  - フロントエンド識別（Clangバージョン等）
  - 解析設定（予算・プロファイル等）

#### 方式上の注意

- 目的は「最適化済みコード生成」ではなく、**同一条件でのソース準拠解析**
- 決定性要件のため、TU処理順や集約順を固定（並列実行時も結果同一）

---

### 6.3 Normalized IR（CFG＋イベント）方式

#### 6.3.1 IR設計方針

- **CFGベース**（コア解析・証拠・検証はIRのみを対象）
- 表層依存を排除しつつ、C++破綻点を **イベント命令**として表現
- 正規化規約（カノニカル化）を持ち、差分追跡・決定性を確保

#### 6.3.2 IRに含めるべきイベント（最小核）

- 寿命: `lifetime.begin/end`, `ctor.call`, `dtor.call`, `move`
- 例外: `throw`, `invoke`, `landingpad`, `resume`（例外エッジ保持）
- 仮想: `vcall(receiver, candidateset_id, signature)`（候補集合の上界）
- 並行性: `thread.spawn/join`, `atomic.r/w(order)`, `fence(order)`, `sync.event`
- メモリ: `load/store`, `memcpy/memmove/memset`, `alloc/free` 等
- UB sink: UB条件をPOに落とすための表現（PO違反として扱う）

#### 6.3.3 正規化の要点（PO安定IDと決定性のため）

- テンポラリ名や生成順依存情報を排除
- 参照すべき識別子（関数同定、型、命令種別、CFG位置）をカノニカル化
- `operator[]` 等を「関数呼び出し」として正規化し、契約適用を可能にする

---

### 6.4 PO生成方式（sink網羅＋安定ID）

#### 6.4.1 PO生成

- “危険操作（sink）” を定義し、IR上の **全登場箇所**でPO生成（漏れ禁止）
- 初期で対象とするPO例（段階導入可）
  - OOB、nullptr、未初期化読み、寿命切れ参照（UAF）、二重解放、UB条件（shift/overflow等）
  - 並行性は“器”優先で、初期はUNKNOWN分類を厚くして良い

#### 6.4.2 PO安定ID（第一版方針）

SRSが指定する構成要素を満たす形で、まずは **決定性＞微小編集耐性** で確定します（微小編集耐性は後でdiff側や近傍マッチで補強）。

**推奨構成（例）**

- repo_identity（安定パス or コンテンツハッシュ）
- function_id（マングル名＋テンプレ実引数等の一意識別）
- ir_anchor（CFGノードID＋命令ID：正規化済み）
- po_kind（sink/PO種別）
- semantics_version / profile_version / proof_system_version

---

### 6.5 Analyzer方式（停止性＋Anytime/Refinement）

#### 6.5.1 解析コア（推奨）

- **抽象解釈（Abstract Interpretation）** を基盤とする
  - 数値: interval（初期）→必要に応じoctagon等拡張
  - nullness / 初期化状態 / 寿命状態（alive）/ provenance状態（粗い）
  - エイリアス: 上界（保守的）で開始し、精密化で縮める
- 精密化メニュー（Anytime）
  - 抽象ドメイン強化
  - パス分割・文脈感度
  - 局所SMT（slice距離・変数数・時間等を制限）

#### 6.5.2 停止性の担保（必須）

- 反復上限、widen/narrow、分割上限、SMT time/memory上限を明示し必ず停止
- 予算超過はSAFE/BUGに倒さず必ずUNKNOWN（分類コードに反映）

#### 6.5.3 証拠化との整合（重要）

Analyzerがどれだけ複雑でも、最終出力は **Validatorがチェックできる規則**に還元する必要があります。
したがって Analyzer の内部は自由でも、証拠出力は以下のどちらかに落とし込む方針とします。

- SAFE証拠: 「CFG上で帰納的な不変条件」＋「PO含意」
- BUG証拠: 「IR意味論に従う反例トレース」＋「PO違反到達」

---

### 6.6 UNKNOWN方式（開拓可能性を製品機能として実装）

UNKNOWNは“失敗”ではなく成果物です。1件ごとに以下を必須とします。

- **unknown_code**（不足理由の分類）
- **missing_lemma**（SAFE/BUGに必要だが示せなかった論理条件：機械可読）
- **refinement_plan**（推奨精密化：パラメータ付き）
- **unknown_stable_id**（PO ID＋位置＋前提バージョン等）

#### missing_lemma 表現（推奨）

- PO条件のうち未証明部分を式として保持
  - 例: OOBで `i < len` が不足 → `missing_lemma = (i < len)`

#### refinement_plan の例（推奨）

- `DomainTooWeak.Numeric -> enable octagon`
- `split_on (i < len)`
- `local_smt { slice_distance=20, max_vars=50, timeout_ms=500 }`
- `MissingContract.Pre -> add_contract for foo(ptr,len)`

---

### 6.7 Spec DB（契約）方式

#### 6.7.1 入力と正規化

- ソース内注釈＋外部サイドカーの双方を受理し、内部では **単一Contract IRへ正規化**

#### 6.7.2 契約対象の一意解決（最重要）

- オーバーロード、名前空間/クラススコープ、テンプレ、cv/ref/noexcept等を含めて
  **どの宣言/定義に適用されるかを一意に解決**する
- 同一シンボルに複数契約が当たり得る場合は、version_scopeの評価順と優先順位を固定
- 解決不能（矛盾/競合）は呼び出し点をUNKNOWNへ倒す

#### 6.7.3 Tier運用（AI契約の位置づけ固定）

- Tier0/1/2/Disabled を実装
- **Tier2（AI推定含む）はSAFE確定に使用禁止**
- 解析結果・証拠に「依存した契約ID/Tier/version_scope」を必ず記録し、Validatorが混入を検査可能にする

#### 6.7.4 差分・影響分析

- 契約ID、履歴、差分、ロールバック
- 契約変更が影響するSAFE/BUG/UNKNOWNを一覧可能（再検証範囲の縮小に直結）

---

### 6.8 Certificate（証拠）方式

#### 6.8.1 粒度・共有

- **PO単位**で検証可能
- 複数POが共有する不変条件・補題・契約参照は参照共有（DAG）し重複排除
  - 物理的には CAS（content-addressed storage）を推奨

#### 6.8.2 カノニカル化と改ざん検出

- 「同一意味の証拠が同一表現になる」カノニカル化規約を定義
- ハッシュ対象範囲を固定し、Validatorが同一規約で検証できること
  - UI向けの人間可読説明は、必要に応じてハッシュ対象外とする等、範囲を明文化

#### 6.8.3 推奨フォーマット方針（第一版）

- **カノニカルJSON**（またはCanonical CBOR）
  - 第一版はデバッグ容易性と導入容易性の観点でカノニカルJSONを推奨
  - ただし将来的なサイズ最適化が必要ならCBORへ移行可能な抽象層を設ける

#### 6.8.4 証拠必須要素（最低限）

- PO定義、IR参照、根拠（不変条件／反例モデル）、依存契約（Tier/version_scope）、前提（target/semantics/proof/profile/設定）、依存グラフ

---

### 6.9 Validator方式（TCB最小化）

#### 方針

- 解析を再実行せず、**証拠のみ**で検証する
- 検証失敗は必ずUNKNOWNへ降格し、理由を分類コードで出す
- **小さく単純**（仕様固定・テスト容易）

#### 第一版の現実的な線引き（推奨）

- proof_system_version v1 では、Validatorが **SMTなしでも決定的にチェックできる**証拠規則に限定する
  （例: 単純ドメインの帰納性チェック、トレース再生による反例検証）
- 将来、SMT proof object 等を扱う拡張余地は、証拠形式に保持しておく（段階導入）

---

### 6.10 再現性・差分（pack/diff/決定性）方式

- 再現パック: `tar.gz + manifest.json`
  - 入力ソース、ビルド条件、フロントエンド識別、target、Spec DBスナップショット、各種version、設定、結果digest、証拠hash
- 決定性: 同一入力・同一バージョン・同一設定でカテゴリとIDが一致（並列でも同一）
- diff:
  - PO安定IDをキーに、SAFE/BUG/UNKNOWNの推移を出す
  - SAFE→UNKNOWN等の後退は原因（契約失効/意味論更新/設定変更等）を機械可読に説明

---

### 6.11 大規模運用方式（インクリメンタル／キャッシュ／CI）

- インクリメンタル解析（推奨）
  - TU単位のIRキャッシュ
  - Spec DB変更時の影響分析により再検証範囲を限定
- キャッシュ（推奨）
  - 同一PO・同一前提が成立する場合、証拠または検証結果を再利用
- CIポリシー（必須）
  - SAFE/BUG/UNKNOWNに加え、UNKNOWN分類コードを条件にゲート可能
  - 例: `BudgetExceeded` は許容、`MissingContract.*` は不許容、等

---

### 6.12 セキュリティ・堅牢性方式

- 外部プロセス（SMT等）も含め時間・メモリ制限を適用
- 不正入力でもクラッシュ・情報漏洩・任意コード実行を招きにくい設計
- サンドボックスは推奨（実装方式は任意）

---

## 7. 採用しない（または後回しにする）方式と理由

### 7.1 LLVM IR中心アーキ（後回し）

- PO安定ID・決定性・差分追跡の観点で難度が上がりやすい
- 契約適用やC++イベントの“説明可能な器”をIRに残しにくい

### 7.2 Validator内でSMTを回す設計（第一版では避ける）

- TCBが肥大化しやすい
- 証拠検証が“計算”に寄り、停止性・再現性・可搬性のリスクが増える
- 第一版は、Validatorが決定的にチェックできる規則に絞る方が要求適合度が高い

---

## 8. リスク・未決事項と対策（方式観点）

| リスク/論点 | 影響 | 対策（方式） |
|---|---|---|
| IR正規化が不十分でPO安定IDが揺れる | diff/回帰が破綻 | IRカノニカル化規約を先に固定、生成順依存の排除、順序固定 |
| C++寿命/例外/仮想の意味論が曖昧 | 嘘SAFE/BUGの温床 | semantics_versionで逸脱点を明文化し、正当化不能はUNKNOWNへ倒す |
| Spec DBの対象解決が曖昧 | 契約誤適用 → 嘘SAFE | Clangの名前解決結果を前提に一意解決、競合はUNKNOWN |
| 証拠フォーマットの互換性破壊 | 過去証拠が無効化 | proof_system_versionの後方互換原則を守り、破壊は必ずバージョンアップ＋移行文書 |
| 並行性の早期実装が困難 | 機能不足 | “器”を先に入れ、当面はUNKNOWN分類を厚く（段階導入） |

---

## 9. 次フェーズ（ソフトウェアアーキテクチャ設計）への引継ぎ事項

本方式検討の結論を、アーキ設計で「最初に設計すべき順」に落とします。

### 9.1 アーキ設計の最優先（Milestone Aを成立させる）

1. **データスキーマの確定**
   - Normalized IR（最低限の命令セット＋イベント）
   - PO定義とPO安定ID生成規則
   - Certificateスキーマ（PO単位＋共有＋依存グラフ）
   - UNKNOWN台帳スキーマ（unknown_code/missing_lemma/refinement_plan/安定ID）
   - 再現パック manifest スキーマ
2. **Validatorの責務と検証規則（proof_system v1）の確定**
   - SAFE証拠の検証規則（帰納性＋PO含意）
   - BUG証拠の検証規則（トレース再生＋違反確認）
   - 失敗理由コード体系
3. **決定性設計**
   - ID生成、集約順、シリアライズ、ハッシュ範囲
4. **CLI骨格**
   - analyze/validate/explain/diff/pack の入出力インタフェース（JSON）

### 9.2 ADR（Architecture Decision Record）候補（本書の決定事項）

- ADR-01: Clang Tooling採用とビルド条件再現方針
- ADR-02: Normalized IR（CFG＋C++イベント）採用
- ADR-03: 証拠はPO単位＋共有参照、カノニカル化＋ハッシュ範囲固定
- ADR-04: Validatorは証拠のみ検証、失敗はUNKNOWN、proof_system v1の規則範囲
- ADR-05: PO安定ID生成規則（要素と正規化方法）
- ADR-06: Spec DBの入力・対象解決・Tier運用・影響分析の方式
- ADR-07: pack/diff/決定性の実装方針

### 9.3 アーキ成果物（最低限の“設計完了”定義）

- コンポーネント分割（Frontend/IR/PO/Analyzer/CertStore/Validator/CLI）
- 各コンポーネントのI/F（データスキーマ・API）
- 版管理方針（semantics/proof/profile）
- 主要ユースケースのシーケンス（analyze→validate→diff→pack）
- テスト戦略（Validator中心、決定性・改ざん検出・互換性・回帰説明）

---

## 付録: 本報告書の要点（1ページ要約）

- **方式の中心**は「Analyzerの賢さ」ではなく、**Validatorで検証可能な証拠体系**と、UNKNOWNの開拓可能性。
- **自前Normalized IR（CFG＋イベント）**を採用し、C++破綻点（寿命/例外/仮想/並行性/UB）を初期から表現可能にする。
- **PO単位の証拠＋安定ID＋決定性＋pack/diff**を初期から成立させ、SDD運用（回帰・差分・CI）を可能にする。
- 第一版は **Validatorを小さく保つ**ため、proof_systemを「決定的にチェック可能な規則」に限定し、SMT検証は拡張余地として保持する。
