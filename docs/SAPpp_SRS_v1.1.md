# SAP++ 要求仕様書（SRS）v1.1
**Sound, Static Absence-Proving Analyzer for C++**  
**状態**: 改訂版（Revised）  
**作成日**: 2026-01-17  
**対象**: 仕様駆動開発（Specification-Driven Development; SDD）

---

## 変更履歴

| 版 | 日付 | 概要 |
|---|---|---|
| v1.0 | 2026-01-17 | 完成版。ドラフト統合・要求の最終確定（UNKNOWN=0は不要／開拓可能性必須、UBはBUG扱い、並行性は高品質アーキ優先、証拠はPO単位、契約は高UX＋高拡張） |
| v1.1 | 2026-01-17 | 改善反映（採用: 提案1/2/4/8/9/10/11/13）。要求ID付与漏れの解消、semantics_versionの基準規格・逸脱点の明文化、証拠のカノニカル化とハッシュ範囲の明確化、契約の対象解決規則の追加、Tier0/1定義の明確化、PO安定ID生成規則の追加、インクリメンタル/キャッシュ/CI運用要件の追加、Open Issues付録の追加。 |

---

## 規範キーワード

本書では要求の強度を以下で表す。

- **MUST（必須）**: 実装は必ず満たさなければならない。
- **SHOULD（推奨）**: 強く推奨。満たさない場合は理由と代替策が必要。
- **MAY（任意）**: 実装してもよい。

---

## 0. 背景と狙い

生成AIが出力するソースコードとテストは「自作自演」になりがちで、動的テストによる品質保証の相対的価値が低下する。そのため本プロジェクトは、**コードから思弁的に推認し、不在証明で品質を断言する**静的解析機（SAP++）を構築する。

本ツールは「UNKNOWNをゼロにする」ことを合格条件にしない。一方で、UNKNOWNを **不足理由・不足補題・開拓計画**として構造化し、ツールのバージョンアップでUNKNOWNをSAFEやBUGへ再分類できる「開拓可能性」を最上位の価値とする。

---

## 1. スコープ

### 1.1 対象言語

#### REQ-SCP-001（MUST）: 対象言語
- SAP++は C/C++ を入力として受理できること（最終的に C++ を主対象とする）。

#### REQ-SCP-002（MUST）: 早期TU受理アーキテクチャ
- Phaseの早期から C++ 翻訳単位（TU）を受理できるアーキテクチャを持つこと（実装精度は段階的でよい）。

### 1.2 対象ユースケース
- 生成AIコードの静的品質保証（代理実行）
- CIゲート（SAFE/UNKNOWN/BUGのポリシー制御）
- 大規模コードベースへの漸進導入（契約駆動・差分追跡）

### 1.3 非スコープ（保証根拠に含めない）

#### REQ-SCP-010（MUST）: 動的根拠の禁止
- 動的テスト、計測、プロファイル、実行ログを **SAFE/BUG確定の保証根拠**として使ってはならない。

#### REQ-SCP-011（MAY）: 動的手法の補助利用
- 研究・開発の補助（例: ツール品質のデバッグ、ベンチマーク、契約候補の探索）として、動的テスト等を用いてよい。

---

## 2. 用語

- **PO（Proof Obligation）**: 安全性性質を満たすための証明課題（例: OOB回避なら `0 <= i < len`）。
- **SAFE**: 指定された意味論モデルの下で、対象POが全実行で成立することが証明され、かつ証拠がValidatorで検証済み。
- **BUG（Certified）**: 指定された意味論モデルの下で、PO違反（UB含む）が**到達可能**であることが証明され、かつ証拠がValidatorで検証済み。
- **UNKNOWN**: SAFE/BUGのどちらも証明できない。必ず不足理由と開拓手段が付く。
- **意味論モデル（Tool Semantics）**: 本ツールが採用するC/C++意味づけ（UB、寿命、例外、仮想、並行性など）。
- **プロファイル（Profile）**: どの性質を保証対象とするかの集合（Safety/UB/Concurrency/Library等）。バージョン付き。
- **契約（Contract）**: 外部関数等の仕様（Pre/Post/Frame/Ownership/Failure/Concurrency等）。
- **Spec DB**: 契約の格納・差分・影響分析を行うDB。
- **Certificate（証拠）**: SAFE/BUGを検証可能にする機械可読データ。
- **Validator**: Certificateを入力しSAFE/BUGを確定する検証器。
- **TCB（Trusted Computing Base）**: SAFE/BUGの真を信頼するために正しいと仮定せざるを得ない要素。

---

## 3. 製品のコア契約（Guarantee Contract）

### 3.1 結果カテゴリ
- **SAFE**: 不在証明が完了し、検証済み証拠を伴う。
- **BUG**: 到達可能な反例（UBを含む）が確定し、検証済み証拠を伴う。
- **UNKNOWN**: 証明不能。安全性主張はしない。必ず不足理由と開拓計画を伴う。

### 3.2 UB（未定義動作）の扱い

#### REQ-GUA-010（MUST）: UBの第一級扱い
- UBは「バグクラス」として第一級に扱うこと。

#### REQ-GUA-011（MUST）: 到達可能UBはBUG
- UBが到達可能であることを証明できた場合は **BUG** として報告すること。

#### REQ-GUA-012（MUST）: 到達不能証明なしのUB可能性はUNKNOWN
- UB可能性を否定できないが到達可能性の証明ができない場合は **UNKNOWN** として報告すること（嘘BUG禁止）。

### 3.3 UNKNOWNの合格基準

#### REQ-GUA-020（MUST）: UNKNOWN=0は合格条件にしない
- UNKNOWN=0 を合格条件にしてはならない。

#### REQ-GUA-021（MUST）: UNKNOWNは必ず開拓可能
- UNKNOWNは必ず「開拓可能」な情報（不足補題＋改善手段＋差分追跡ID）を持つこと。

---

## 4. 最上位原則（必須要求）

> 本章の要求はすべて必須（最優先、妥協不可）。

### REQ-PRIN-001: 嘘SAFEゼロ（Soundness）
- SAFEと確定した結論は、採用意味論モデルの下で常に真であること。

### REQ-PRIN-002: 嘘BUGゼロ
- BUGと確定した結論は、採用意味論モデルの下で「到達可能なPO違反」が常に真であること。

### REQ-PRIN-003: Proof-Carrying Results
- SAFE/BUG は必ず検証可能な **Certificate** を伴うこと。

### REQ-PRIN-004: Validatorによる確定
- SAFE/BUG は必ず Validator により検証され、検証に失敗した場合は必ず UNKNOWN に降格すること。

### REQ-PRIN-005: Static-only（保証根拠）
- SAFE/BUG確定の根拠に、動的実行・テスト・計測を含めないこと。

### REQ-PRIN-006: UNKNOWN開拓性
- UNKNOWNは必ず「不足理由」「不足補題（何が必要か）」「開拓手段（どうすればよいか）」「安定ID」を含むこと。

### REQ-PRIN-007: C++完全志向の器（後付けで詰まない）
- 寿命・例外・仮想・並行性・UBを**表現できるIRと証拠体系**を初期から要求する（精度は段階的でよい）。

---

## 5. システム境界・アーキテクチャ不変条件（SDD用の設計制約）

### 5.1 二分割構造（Analyzer / Validator）
#### REQ-ARCH-001（MUST）: 不信なAnalyzerと信頼核Validator
- 解析器（Analyzer）は複雑化してよいが、SAFE/BUGの確定はValidatorの検証を通過した場合に限る。

#### REQ-ARCH-002（MUST）: TCB最小化指向
- TCBは最小化されるよう設計されること。特にValidatorは小さく単純であること。

#### REQ-ARCH-003（MUST）: Analyzerの差し替え・増築耐性
- Analyzerを更新・増築しても、Validatorが通らない限りSAFE/BUGは増えない（嘘SAFE/BUGを出さない）構造であること。

### 5.2 バージョン固定・互換性
#### REQ-ARCH-010（MUST）: 3種のバージョンを結果に埋め込む
- 解析結果・証拠・再現パックには必ず以下を含むこと:
  - `semantics_version`（意味論モデル）
  - `proof_system_version`（証拠検証規則）
  - `profile_version`（保証対象性質の集合）

#### REQ-ARCH-011（MUST）: 後方互換の原則
- `proof_system_version` が同一なら、古い証拠は将来Validatorでも検証できること（互換を維持）。
- 破壊的変更が必要な場合、必ずバージョンを上げ、移行方針を文書化すること。

---

## 6. 入力・ビルド整合（フロントエンド要求）

#### REQ-IN-001（MUST）: 実コンパイル条件の再現
- `compile_commands.json` 相当またはビルドフックから実際のコンパイル条件（マクロ、include、標準、ターゲット、ABI、最適化等）を取得し同一条件で解析すること。

#### REQ-IN-002（MUST）: ターゲット前提の記録
- target triple、主要型サイズ・アラインメント等の前提を結果・証拠・再現パックに含めること。

#### REQ-IN-003（MUST）: C++ TU受理（早期アーキ要件）
- 初期フェーズから C++ 翻訳単位を受理し、AST/CFG/IR生成可能な構造を持つこと。

#### REQ-IN-004（SHOULD）: ソースマップ
- マクロ展開後の解析でも原ソース位置へ戻せるマッピングを提供すること。

---

## 7. 正規化IR（Normalized IR）要求

### 7.1 IRの必須性質
#### REQ-IR-001（MUST）: コア解析はIRのみを対象
- コア解析・証拠・検証は、言語表層から独立したCFGベースIRを対象とすること。

#### REQ-IR-002（MUST）: C++破綻点を表現できる器
- IRは最低限、以下のイベントを表現できること（精密化は段階的でよい）:
  - 寿命イベント（生成/破棄/ムーブ、RAIIデストラクタ）
  - 例外フロー（throw/伝播/巻き戻しによるデストラクタ）
  - 動的ディスパッチ（仮想呼び出し候補集合）
  - 並行性イベント（スレッド開始/合流、atomic、fence、同期イベント）
  - メモリ操作（load/store、memcpy等）
  - UB sink（UBをPO違反として扱うため）

### 7.2 正規化の必須項目
#### REQ-IR-010（MUST）: 暗黙デストラクタの明示化
- 暗黙デストラクタ（RAII）をCFG上の明示的イベント/呼び出しとして表現すること。

#### REQ-IR-011（MUST）: 演算子の契約適用可能化
- operator[] 等を関数呼び出し扱いに正規化し、契約（Spec DB）を適用可能にすること。

#### REQ-IR-012（MUST）: 例外エッジの保持
- 例外経路をIR上に表現できること。未精密化の場合は保守的にUNKNOWN側へ倒せること。

#### REQ-IR-013（MUST）: 仮想呼び出し候補集合の保持
- 候補集合の上界（保守的）を保持し、精密化で縮められる器を持つこと。

#### REQ-IR-014（SHOULD）: 解析用IRの正規化規約
- 解析用IRは、差分追跡・再現性・PO安定IDのために、少なくとも以下の正規化規約を持つこと:
  - 不要な名前（テンポラリ名等）や順序依存情報の排除
  - 参照すべき識別子（関数シンボル、型、命令種別、CFG位置等）のカノニカル化
  - 同一入力・同一コンパイル条件で、フロントエンド差分を最小化するための表現ルール（可能な範囲で）

---

## 8. PO（Proof Obligation）生成要求

#### REQ-PO-001（MUST）: sink網羅とPO生成
- 危険操作（sink）を定義し、IR上の全登場箇所でPOを生成すること（漏れ禁止）。

#### REQ-PO-002（MUST）: PO IDの安定性
- POには安定IDを付与し、ツール更新後も同一POを追跡できること。

#### REQ-PO-002a（MUST）: PO安定IDの生成規則
- PO安定IDは、少なくとも以下の要素を含む正規化済みの構成要素から生成されること:
  - 解析対象の同一性（例: リポジトリ内の安定パスまたはコンテンツハッシュ）
  - 関数/メソッド同定子（例: マングル名・型引数を含む一意識別子）
  - IR上の位置同定子（CFGノード/命令ID等のカノニカル化されたアンカー）
  - sink/PO種別
  - `semantics_version` / `profile_version` / `proof_system_version`

#### REQ-PO-002b（SHOULD）: 微小編集耐性
- PO安定IDは、行番号のズレ等の微小編集に対して可能な限り頑健であること（AST/IRアンカー優先、行番号は補助情報として扱う等）。

#### REQ-PO-003（SHOULD）: バグクラス拡張性
- sink/POは拡張可能であり、追加しても既存結果の意味が壊れないバージョン管理を備えること。

---

## 9. 解析エンジン要求（不在証明コア）

### 9.1 基本
#### REQ-ANL-001（MUST）: 不在証明は抽象的根拠により行う
- SAFEの確定は「抽象不変条件等の根拠」からPOを含意できた場合に限ること。

#### REQ-ANL-002（MUST）: 反例は到達可能性を伴う
- BUGは「到達可能なPO違反」の静的証明がある場合に限ること。

### 9.2 停止性
#### REQ-ANL-010（MUST）: 解析は必ず停止
- 反復上限K、widen/narrow規約、分割上限、SMT time/memory上限などにより必ず停止すること。

#### REQ-ANL-011（MUST）: 予算超過はUNKNOWN
- 予算超過時にSAFE/BUGへ倒してはならず、必ずUNKNOWNとすること。

### 9.3 Anytime/Refinement（UNKNOWN開拓）
#### REQ-ANL-020（MUST）: 精密化メニューの提供
- 少なくとも以下の精密化を提供する器を持つこと:
  - 抽象ドメイン強化
  - パス分割/文脈感度
  - 局所SMT（UNSATでSAFE昇格、SATでBUG証拠）

#### REQ-ANL-021（MUST）: 局所SMTの局所性
- slice距離N、関数深さD、変数/メモリ数M、時間T等の制限を持ち、超過時はUNKNOWNのままにすること。

#### REQ-ANL-022（SHOULD）: 単調性（運用上の期待）
- 同一入力・同一バージョン・同一設定で、精密化予算を増やすほどUNKNOWNが減る（理想）こと。
- 実装上単調性が破れる場合、結果差分の説明（どの近似が変わったか）を出せること。

---

## 10. C/C++ 意味論モデル要求（Tool Semantics）

#### REQ-SEM-001（MUST）: 意味論モデルの文書化と最小例
- semantics_versionごとに、文章＋最小例（litmus/例）で意味論を固定し、再現パックから参照できること。

#### REQ-SEM-002（MUST）: 基準規格（ベースライン）の明示
- semantics_versionは、少なくとも以下を文書化し、再現パックから参照できること:
  - ベースラインとする言語規格（例: ISO C++23 など）
  - コンパイル条件に依存する振る舞い（例: ABI、ターゲット、標準ライブラリ実装差）の扱い

#### REQ-SEM-003（MUST）: 意図的な抽象化・逸脱点の明示
- semantics_versionは、ベースライン規格に対する **意図的な抽象化/簡略化/未対応** を「逸脱点一覧」として明示すること。
- 逸脱点に起因して SAFE/BUG を正当化できない場合、ツールは必ず UNKNOWN に倒すこと（嘘SAFE/嘘BUG回避の優先）。

#### REQ-SEM-004（SHOULD）: 最小例のカテゴリ網羅
- 最小例（litmus/例）は、少なくとも以下のカテゴリを網羅すること（各カテゴリ最低1例以上）:
  - 寿命（生成/破棄/ムーブ/RAII、例外巻き戻しに伴う破棄）
  - 例外（throw/伝播/巻き戻しと制御フロー）
  - 仮想/動的ディスパッチ（候補集合の扱い）
  - provenance（喪失・保持・変換の扱い）
  - 並行性（atomic order/happens-before/data race）

#### REQ-SEM-010（MUST）: UBはPOとして扱う
- UBの発生条件をPOとして表現し、到達可能UBをBUGとして確定できること。

#### REQ-SEM-020（MUST）: 寿命（alive）を第一級に扱う
- オブジェクト寿命（生成〜破棄）を表現し、寿命切れ参照をPOとして扱えること。

#### REQ-SEM-021（SHOULD）: 初期化状態を扱う
- 未初期化読みをPOとして扱える器を持つこと（実装の段階導入は可）。

#### REQ-SEM-030（MUST）: provenance状態を保持
- ポインタ抽象状態にprovenance状態を保持し、喪失が疑われる操作をモデル化できること。

#### REQ-SEM-031（MUST）: provenance方針のバージョン管理
- provenance規則はsemantics_versionの一部であり、変更時は影響分析・再検証対象の特定ができること。

---

## 11. C++ 並行性／メモリモデル要求（高品質アーキ）

> 早期実装は必須ではないが、後から詰む設計は禁止（器を最初から要求）。

#### REQ-CONC-001（MUST）: 並行性イベントのIR表現
- スレッド開始/合流、atomic操作（オーダ含む）、fence、同期イベントをIRで表現できること。

#### REQ-CONC-002（MUST）: 共有メモリアクセス分類
- 共有メモリアクセスを atomic/non-atomic、read/write 等で分類できること。

#### REQ-CONC-010（MUST）: data race をPOとして表現可能
- data race（＝UB）不在を将来POとして扱える器を持つこと。

#### REQ-CONC-011（MUST）: 並行性UNKNOWN分類
- 並行性が原因で証明不能な場合、UNKNOWNに以下相当の分類を付与できること:
  - RaceUnproven / AtomicOrderUnknown / SyncContractMissing / ConcurrencyUnsupported 等

#### REQ-CONC-020（SHOULD）: 段階精密化の導線
- ロック契約・happens-before・atomic整合の段階導入が可能なデータモデル（イベント/依存/関係）を備えること。

---

## 12. Spec DB（契約）要求（入力方式・拡張性・保守性）

### 12.1 入力方式（UX優先）
#### REQ-SPC-001（MUST）: ハイブリッド入力＋単一正規化
- 契約は (a) ソース内注釈 と (b) 外部サイドカー の双方をサポートし、内部では単一のContract IRへ正規化すること。

#### REQ-SPC-002（MUST）: 改変不可対象への対応
- 第三者ライブラリ等、改変不可のヘッダ/バイナリに対してもサイドカー契約で対応できること。

### 12.2 契約表現能力
#### REQ-SPC-010（MUST）: 契約要素
- 契約は最低限、Pre/Post/Frame/Ownership/Lifetime/Failure semantics/Concurrency semantics を表現できること。

#### REQ-SPC-011（MUST）: version_scope
- 契約は適用範囲（バージョン、ABI、条件）を持つこと。

### 12.2.1 契約の対象解決（名前解決・オーバーロード・テンプレート）

#### REQ-SPC-012（MUST）: 契約対象の一意解決規則
- 契約が「どの宣言/定義に適用されるか」を一意に解決する規則を持つこと。
- 少なくとも以下を扱えること:
  - 関数オーバーロード
  - 名前空間/クラススコープ
  - テンプレート（型引数/非型引数を含む識別）
  - メンバ関数（cv/ref修飾、noexcept含む）

#### REQ-SPC-013（MUST）: version_scopeの評価順と優先順位
- 同一シンボルに複数契約が適用され得る場合、version_scope（バージョン/ABI/条件）の評価順と優先順位を仕様として固定すること。
- 解決不能（矛盾/競合）な場合は、当該呼び出し点の扱いを保守的にUNKNOWN側へ倒せること。

#### REQ-SPC-014（SHOULD）: 同梱ベース契約
- 標準ライブラリや主要Cライブラリについて、導入初期に利用可能なベース契約セットを同梱（または配布）できること。

### 12.3 信頼ティア（AIの位置づけ固定）
#### REQ-SPC-020（MUST）: Tier制度
- Tier0（静的に実装適合を証明済み）、Tier1（根拠メタデータ付き採用）、Tier2（推定/AI含む：SAFE禁止）、Disabled（失効）を持つこと。

#### REQ-SPC-022（MUST）: Tier0の定義（実装適合の証明）
- Tier0契約は、少なくとも以下を満たす「実装適合が検証済み」であることを意味するものとして定義されること:
  - 対象（関数/型/バージョン範囲）が明示されている
  - Implementation ⊨ Contract を裏付ける検証成果物（例: 専用の検証ログ、機械検証可能な証拠、監査可能な手順）が存在し、参照可能である
  - 検証が成立する前提（target/ABI/semantics_version等）が明示されている

#### REQ-SPC-023（MUST）: Tier1根拠メタデータの最小要件
- Tier1契約は「根拠メタデータ付き採用」として、少なくとも以下のメタデータを保持すること:
  - 根拠種別（例: 公式仕様/ベンダ文書/人手レビュー/外部監査/テスト由来等）
  - 参照（ドキュメント識別子、チケット、レビュー記録等）
  - 適用範囲（version_scope）

#### REQ-SPC-021（MUST）: Tier2はSAFEに使用禁止
- AI生成を含む推定契約（Tier2）はSAFE確定に用いてはならない。

### 12.4 契約の反証と波及
#### REQ-SPC-030（MUST）: 実装不適合の検出
- Implementation ⊭ Contract を検出した場合、契約をDisabled（またはTier2へ降格）にし、依存SAFEを再評価してUNKNOWNへ降格すること。

#### REQ-SPC-031（MUST）: 反証証拠の保存
- 反証の証拠（破れたPO、経路、モデル等）を保存し、再現可能にすること。

### 12.5 差分・影響分析
#### REQ-SPC-040（MUST）: 契約差分管理
- 契約ID、履歴、差分、ロールバックをサポートすること。

#### REQ-SPC-041（MUST）: 影響分析
- 契約変更が影響するSAFE/BUG/UNKNOWNを一覧できること。

---

## 13. UNKNOWN仕様（開拓可能性の中核要求）

#### REQ-UNK-001（MUST）: UNKNOWNは不足理由を分類
- UNKNOWN 1件ごとに不足分類コードを付与すること。

#### REQ-UNK-002（MUST）: UNKNOWNは不足補題（Missing Lemma）を含む
- SAFE/BUGに必要だが証明できなかった論理条件を、機械可読に含めること。

#### REQ-UNK-003（MUST）: UNKNOWNは開拓計画（Refinement Plan）を含む
- 推奨精密化（ドメイン強化/分割/局所SMT/契約追加）をパラメータ付きで提示すること。

#### REQ-UNK-004（MUST）: UNKNOWN安定ID
- UNKNOWNにはPO ID、位置、前提（profile/semanticsなど）を含む安定IDが付与されること。

#### REQ-UNK-005（MUST）: UNKNOWN台帳と差分追跡
- バージョンアップにより UNKNOWN→SAFE/BUG へ再分類された場合、解消された不足補題・変更された依存要素を差分として出力できること。

---

## 14. 証拠（Certificate）要求（最高品質：PO単位＋共有＋依存グラフ）

### 14.1 粒度
#### REQ-CRT-001（MUST）: PO単位の証拠
- SAFE/BUGはPO単位で確定し、PO単位で検証可能な証拠を生成すること。

#### REQ-CRT-002（MUST）: 共有と重複排除
- 複数POが共有する不変条件・契約・補題は参照共有できること（証拠の巨大化を許容しつつ運用可能にする）。

### 14.2 証拠内容（最低限）
#### REQ-CRT-010（MUST）: 証拠必須要素
- 証拠は最低限以下を含むこと:
  - PO定義（プロファイル含む）
  - IR参照（CFGノード/命令ID）
  - 根拠（不変条件／反例モデル）
  - 依存契約（Spec DB項目、Tier、version_scope）
  - 前提（target、semantics_version、proof_system_version、解析設定）
  - 依存グラフ（どの証明が何に依存するか）

#### REQ-CRT-011（MUST）: 改ざん検出
- 証拠はhash等により改ざん検出可能であること。

#### REQ-CRT-012（MUST）: カノニカル（正規化）表現
- 証拠は「同一意味の証拠が同一表現になる」カノニカル化規約を持つこと（例: キー順序、数値表現、参照の正規化、パス正規化等）。

#### REQ-CRT-013（MUST）: ハッシュ対象範囲の明示
- 改ざん検出に用いるハッシュ対象（何を含め、何を除外するか）を証拠仕様として固定し、Validatorが同一規約で検証できること。

#### REQ-CRT-014（MAY）: 証拠への署名
- 証拠に署名（例: 公開鍵署名）を付与してよい。

---

## 15. Validator要求（信頼核）

#### REQ-VAL-001（MUST）: 証拠のみで検証
- Validatorは解析を再実行せず、証拠を入力としてSAFE/BUGの正当性を検証すること。

#### REQ-VAL-002（MUST）: 失敗はUNKNOWNへ降格
- 検証失敗時は必ずUNKNOWNへ降格し、失敗理由を出力すること。

#### REQ-VAL-003（MUST）: 小さく単純
- Validatorは小さく、仕様が固定され、テスト容易であること（TCB最小化）。

#### REQ-VAL-004（MUST）: 検証失敗理由の標準分類
- Validatorは、検証失敗理由を標準分類コードとして出力できること（例: SchemaInvalid / VersionMismatch / MissingDependency / RuleViolation / ProofCheckFailed 等）。

#### REQ-VAL-010（SHOULD）: SMT結果の検証可能性
- 将来的にSMTの検証可能証拠（proof object等）を取り扱える拡張性を証拠形式に保持すること（現時点での実装は段階導入可）。

---

## 16. 再現性・回帰・差分（SDDの運用要件）

#### REQ-REP-001（MUST）: 再現パック
- `tar.gz + manifest.json` の再現パックを生成できること。
- 必須要素: 入力ソース、ビルド条件、フロントエンド識別、target、Spec DBスナップショット、semantics/proof/profileバージョン、解析設定、結果digest、証拠hash。

#### REQ-REP-002（MUST）: 決定性（同一条件で同一結果）
- 同一入力・同一バージョン・同一設定で、結果（少なくともカテゴリとID）が決定的であること（並列化時も同様）。

#### REQ-REP-003（MUST）: 回帰理由の説明
- SAFE→UNKNOWN 等の後退が生じた場合、原因（契約失効/意味論更新/設定変更等）を機械可読に出力すること。

#### REQ-REP-004（MUST）: UNKNOWN台帳
- UNKNOWNは台帳化され、バージョン間推移（SAFE化/BUG化/残存/新規）を追跡できること。

---

## 17. 出力・統合（CLI/JSON/SARIF）

#### REQ-OUT-001（MUST）: 機械可読JSON
- 結果を機械可読JSONで出力すること（スキーマはバージョン管理されること）。

#### REQ-OUT-002（SHOULD）: SARIF出力
- 可能ならSARIF等の既存フォーマットにも出力できること。

#### REQ-OUT-003（MUST）: 人間可読レポート
- 人間がUNKNOWNの開拓を実行できるよう、分類・不足補題・開拓計画・依存情報を読める形式で出力すること。

#### REQ-OUT-010（MUST）: CLIコマンド（最低限）
- 少なくとも以下を提供すること:
  - `analyze`（解析＋証拠生成）
  - `validate`（証拠検証）
  - `explain`（UNKNOWN開拓ガイド表示）
  - `diff`（バージョン差分）
  - `pack`（再現パック生成）

---

## 18. 拡張性（更新でUNKNOWNを減らすための要件）

#### REQ-EXT-001（MUST）: 解析プラグイン追加が可能
- 新しい解析（ドメイン、分割、SMT、並行性精密化等）を追加してUNKNOWNを減らせる構造であること。

#### REQ-EXT-002（MUST）: 証拠形式の拡張はバージョン管理
- 証拠形式は拡張可能だが、`proof_system_version` により互換と移行が管理されること。

#### REQ-EXT-003（SHOULD）: Spec DBのスキーマ拡張
- 契約要素の拡張（新しいOwnership概念等）に耐えるスキーマであること。

### 18.1 大規模運用（CI/インクリメンタル/キャッシュ）

#### REQ-OPS-001（SHOULD）: インクリメンタル解析
- 変更範囲を最小化する形で再解析できること（例: 変更TUのみ再解析、Spec DB変更の影響範囲のみ再検証）。

#### REQ-OPS-002（SHOULD）: 証拠・検証のキャッシュ
- 同一PO・同一前提（入力/バージョン/設定/依存契約）が成立する場合、証拠または検証結果を再利用できること。

#### REQ-OPS-003（MUST）: CIゲート用ポリシー設定
- CIゲートで、カテゴリ（SAFE/BUG/UNKNOWN）およびUNKNOWN分類コード等に基づく判定ポリシーを設定できること（例: BudgetExceededは許容、MissingContractは不許容、等）。

---

## 19. セキュリティ・堅牢性（ツールとしての品質）

#### REQ-SEC-001（MUST）: リソース制限
- ソルバ等外部プロセスを含め、時間・メモリ制限を適用できること。

#### REQ-SEC-002（MUST）: 不正入力耐性
- 不正なソース/ビルド条件でも、クラッシュ・情報漏洩・任意コード実行を招きにくい設計であること。

#### REQ-SEC-003（SHOULD）: サンドボックス
- 解析をサンドボックス化できること（実装方式は任意）。

---

## 20. AI利用方針（保証根拠に混ぜない）

#### REQ-AI-001（MUST）: ランタイム保証にAIを使わない
- SAFE/BUG確定（証拠生成・検証）の根拠にAI推論を含めないこと。

#### REQ-AI-002（MUST）: AI生成契約はTier2固定
- AI生成契約はTier2として扱い、SAFE確定に使用しないこと。

#### REQ-AI-003（MAY）: AIの許容用途
- AIは実装支援、契約候補生成（Tier2）、UNKNOWN説明文草案生成等に利用してよい（ただし保証根拠ではない）。

---

## 21. 検証（Verification）計画（SDD向け）

本仕様の要求は、以下の方法で検証される（複数可）:

- **INSPECT**: 設計/実装/成果物レビューで確認
- **ANALYZE**: 静的な整合性チェックや形式的検査で確認
- **TEST**: テスト実行で確認（ただしツール品質の確認であり、解析結果の保証根拠ではない）
- **DEMO**: 再現パック等でデモンストレーション

#### REQ-VER-001（MUST）: 検証トレーサビリティ
- 各REQに対し、プロジェクト内で「検証方法」と「受入基準」をトレーサブルに管理できること。  
  （例: 要求ID→設計要素→テスト/検証項目→成果物）

---

## 22. マイルストーン（器→精密化の順）

> 実装順序は「器を先に固める」。早期に全部を証明する必要はないが、後から詰む設計は禁止。

### Milestone A: 器の完成（最優先）
- IR（寿命/例外/仮想/並行性/UB sink）表現
- Certificateスキーマ＋Validator確定
- UNKNOWN仕様（不足補題＋開拓計画＋台帳）
- 再現パック＋diff

**受入基準（例）**
- REQ-PRIN系、REQ-ARCH系、REQ-IR系、REQ-UNK系、REQ-VAL系、REQ-REP系が満たされる。

### Milestone B: 逐次C++の精密化（寿命・例外・仮想）
- UNKNOWNの主要原因を削減（契約・不変条件強化）

### Milestone C: 並行性精密化
- data race（UB）を中心に、段階的にPOと証明を拡張

### Milestone D: 大規模運用
- Spec DB拡充、影響分析強化、インクリメンタル解析、CI統合の成熟

---

## 付録A: 典型的UNKNOWN分類コード（例）

- MissingInvariant
- MissingContract.Pre / Post / Frame / Ownership / Concurrency
- AliasTooWide / PointsToUnknown
- DomainTooWeak.Numeric / DomainTooWeak.Memory / DomainTooWeak.Concurrency
- VirtualDispatchUnknown
- ExceptionFlowConservative
- ProvenanceLost
- LifetimeUnmodeled
- RaceUnproven / AtomicOrderUnknown / SyncContractMissing / ConcurrencyUnsupported
- BudgetExceeded
- UnsupportedFeature

> 注: コード体系はバージョン管理し、安定IDとして扱う（REQ-UNK-004/005）。

---

## 付録B: TCB（最小化の指針）

TCBに含まれ得る要素（最小化対象）:
- フロントエンド（Clang等）とビルド条件再現
- 意味論モデルの定義（semantics_version）
- Validator（proof_system）
- Tier0/1契約（Spec DB）
- （必要なら）SMTソルバの正しさ（可能なら証明検査で削減）

#### REQ-TCB-001（MUST）: TCB明示
- 解析結果はTCBの構成とバージョンを明示すること。

---

## 付録C: Open Issues（未決事項の棚）

本付録は、設計・仕様として今後の明文化が必要な論点を列挙する（要求ではない）。

- **SMTソルバ戦略**: 複数ソルバ対応の要否、推奨ソルバ、タイムアウト規約、証拠検証（proof object）方針。
- **並行性の段階導入の粒度**: lock契約の標準表現、happens-before関係の表現粒度、data race検出の初期段階の近似方針。
- **CとC++の差分の扱い**: strict aliasing、provenance、ライブラリ境界（C ABI）などの統一/分離方針。
- **標準ライブラリ契約の配布範囲**: どこまで同梱し、どこから外部配布とするか（libstdc++/libc++/MSVC STL差分を含む）。
- **反例の提示形式**: BUG証拠における「人間可読」反例の最小要件（証拠のコアと表示の分離をどう行うか）。

