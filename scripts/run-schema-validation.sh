#!/bin/bash
# run-schema-validation.sh - JSON Schema 検証の単一エントリポイント
#
# このスクリプトは JSON Schema 検証を一元管理します。
# pre-commit-check.sh、CI はすべてこのスクリプトを呼び出します。
#
# 使い方:
#   ./scripts/run-schema-validation.sh           # 全スキーマを検証
#   ./scripts/run-schema-validation.sh --list    # スキーマ一覧を表示
#   ./scripts/run-schema-validation.sh schema.json  # 特定スキーマのみ

set -euo pipefail

# ===========================================================================
# 初期化
# ===========================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# 共通ライブラリ読み込み
source "$SCRIPT_DIR/lib/common.sh"

# ===========================================================================
# 設定（単一ソース）
# ===========================================================================

# スキーマディレクトリ
SCHEMA_DIR="schemas"

# スキーマファイルパターン
SCHEMA_PATTERN="*.schema.json"

# ajv オプション
AJV_SPEC="draft2020"
AJV_FORMATS="ajv-formats"

# ===========================================================================
# オプション
# ===========================================================================

MODE="validate"  # validate|list
SPECIFIC_SCHEMAS=()

print_help() {
    cat << EOF
Usage: $0 [OPTIONS] [SCHEMAS...]

JSON Schema 検証の単一エントリポイント。

Options:
  --validate      スキーマを検証（デフォルト）
  --list          スキーマ一覧を表示
  --help, -h      このヘルプを表示

対象:
  ディレクトリ: $SCHEMA_DIR
  パターン:     $SCHEMA_PATTERN

Examples:
  $0                              # 全スキーマを検証
  $0 --list                       # スキーマ一覧を確認
  $0 schemas/po.v1.schema.json    # 特定スキーマのみ
EOF
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --validate)
            MODE="validate"
            shift
            ;;
        --list)
            MODE="list"
            shift
            ;;
        --help|-h)
            print_help
            exit 0
            ;;
        -*)
            echo -e "${RED}Error: Unknown option: $1${NC}" >&2
            print_help >&2
            exit 1
            ;;
        *)
            SPECIFIC_SCHEMAS+=("$1")
            shift
            ;;
    esac
done

# ===========================================================================
# ajv 検出
# ===========================================================================

AJV="$(detect_ajv)"

if [ -z "$AJV" ] && [ "$MODE" != "list" ]; then
    echo -e "${RED}Error: ajv-cli not found${NC}" >&2
    echo "Install with: npm install -g ajv-cli ajv-formats" >&2
    exit 1
fi

# ===========================================================================
# スキーマ収集
# ===========================================================================

collect_schemas() {
    find "$SCHEMA_DIR" -name "$SCHEMA_PATTERN" -print 2>/dev/null | sort
}

# ===========================================================================
# メイン処理
# ===========================================================================

main() {
    local schemas

    # 特定スキーマ指定時
    if [ ${#SPECIFIC_SCHEMAS[@]} -gt 0 ]; then
        schemas=$(printf '%s\n' "${SPECIFIC_SCHEMAS[@]}")
    else
        schemas="$(collect_schemas)"
    fi

    if [ -z "$schemas" ]; then
        echo -e "${YELLOW}No schemas found${NC}"
        exit 0
    fi

    local schema_count
    schema_count=$(printf '%s\n' "$schemas" | wc -l | tr -d ' ')

    case "$MODE" in
        list)
            printf '%s\n' "$schemas"
            exit 0
            ;;
        validate)
            echo -e "${BLUE}▶ Schema validation: $schema_count schemas${NC}"
            
            local failed=0
            
            while IFS= read -r schema; do
                if [ ! -f "$schema" ]; then
                    echo -e "${RED}  ✗ $schema (not found)${NC}"
                    failed=$((failed + 1))
                    continue
                fi
                
                # 他のスキーマを参照として追加（クロスリファレンス対応）
                local refs=""
                while IFS= read -r ref_schema; do
                    if [ "$ref_schema" != "$schema" ]; then
                        refs="$refs -r $ref_schema"
                    fi
                done <<< "$(collect_schemas)"
                
                echo "  Validating: $schema"
                # shellcheck disable=SC2086
                if ! $AJV compile -s "$schema" --spec="$AJV_SPEC" -c "$AJV_FORMATS" $refs 2>&1; then
                    echo -e "${RED}  ✗ $schema${NC}"
                    failed=$((failed + 1))
                fi
            done <<< "$schemas"
            
            if [ $failed -gt 0 ]; then
                echo -e "${RED}Schema validation failed: $failed errors${NC}"
                exit 1
            fi
            
            echo -e "${GREEN}✓ Schema validation passed${NC}"
            ;;
    esac
}

main
