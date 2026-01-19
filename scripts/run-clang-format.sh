#!/bin/bash
# run-clang-format.sh - clang-format 実行の単一エントリポイント
#
# このスクリプトは clang-format の対象範囲を一元管理します。
# Makefile、pre-commit-check.sh、quick-check.sh、CI はすべてこのスクリプトを呼び出します。
#
# 使い方:
#   ./scripts/run-clang-format.sh                    # 全対象ファイルをチェック
#   ./scripts/run-clang-format.sh --fix              # 全対象ファイルを修正
#   ./scripts/run-clang-format.sh --changed          # 変更ファイルのみチェック
#   ./scripts/run-clang-format.sh --changed --fix    # 変更ファイルのみ修正
#   ./scripts/run-clang-format.sh --list             # 対象ファイル一覧を表示
#   ./scripts/run-clang-format.sh file1.cpp file2.cpp  # 指定ファイルのみ

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
# 設定（対象範囲の単一ソース）
# ===========================================================================

# clang-format の対象ディレクトリ
# clang-tidy と異なり、tests も対象に含める
TARGET_DIRS="$SAPPP_SOURCE_DIRS $SAPPP_TEST_DIR"

# 除外パターン（現時点では除外なし）
EXCLUDE_PATTERN=""

# ===========================================================================
# オプション
# ===========================================================================

MODE="check"  # check|fix|list
SCOPE="all"   # all|changed
SPECIFIC_FILES=()

print_help() {
    cat << EOF
Usage: $0 [OPTIONS] [FILES...]

clang-format 実行の単一エントリポイント。対象範囲はこのスクリプトで一元管理されます。

Options:
  --check         フォーマットをチェック（デフォルト）
  --fix           フォーマットを修正
  --all           全対象ファイルを処理（デフォルト）
  --changed       変更ファイルのみ処理
  --list          対象ファイル一覧を表示（実行しない）
  --help, -h      このヘルプを表示

対象範囲:
  ディレクトリ: $TARGET_DIRS
  拡張子:       $SAPPP_ALL_CPP_EXTENSIONS

Examples:
  $0                      # 全ファイルをチェック
  $0 --fix                # 全ファイルを修正
  $0 --changed            # 変更ファイルのみチェック
  $0 --changed --fix      # 変更ファイルのみ修正
  $0 libs/common/hash.cpp # 特定ファイルのみチェック
  $0 --list | wc -l       # 対象ファイル数を確認
EOF
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --check)
            MODE="check"
            shift
            ;;
        --fix)
            MODE="fix"
            shift
            ;;
        --all)
            SCOPE="all"
            shift
            ;;
        --changed)
            SCOPE="changed"
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
            SPECIFIC_FILES+=("$1")
            shift
            ;;
    esac
done

# ===========================================================================
# clang-format 検出
# ===========================================================================

CLANG_FORMAT="$(detect_clang_format)"

if [ -z "$CLANG_FORMAT" ] && [ "$MODE" != "list" ]; then
    echo -e "${RED}Error: clang-format not found${NC}" >&2
    echo "Install with: sudo apt install clang-format-19" >&2
    exit 1
fi

# ===========================================================================
# 対象ファイル収集
# ===========================================================================

collect_all_files() {
    collect_all_cpp_files "$TARGET_DIRS" "$EXCLUDE_PATTERN"
}

collect_changed_files_impl() {
    local files
    files="$(collect_changed_cpp_files "$TARGET_DIRS")"
    
    if [ -n "$EXCLUDE_PATTERN" ] && [ -n "$files" ]; then
        printf '%s\n' "$files" | grep -vE "$EXCLUDE_PATTERN" || true
    else
        printf '%s\n' "$files"
    fi
}

collect_target_files() {
    case "$SCOPE" in
        all)
            collect_all_files
            ;;
        changed)
            collect_changed_files_impl
            ;;
    esac
}

# ===========================================================================
# メイン処理
# ===========================================================================

main() {
    local target_files

    # 特定ファイル指定時
    if [ ${#SPECIFIC_FILES[@]} -gt 0 ]; then
        target_files=$(printf '%s\n' "${SPECIFIC_FILES[@]}")
    else
        target_files="$(collect_target_files)"
    fi

    if [ -z "$target_files" ]; then
        echo -e "${YELLOW}No files to check${NC}"
        exit 0
    fi

    local file_count
    file_count=$(printf '%s\n' "$target_files" | wc -l | tr -d ' ')

    case "$MODE" in
        list)
            printf '%s\n' "$target_files"
            exit 0
            ;;
        check)
            echo -e "${BLUE}▶ clang-format check: $file_count files${NC}"
            
            local failed=0
            while IFS= read -r file; do
                if [ -f "$file" ]; then
                    if ! $CLANG_FORMAT --dry-run --Werror "$file" 2>/dev/null; then
                        echo -e "${RED}  ✗ $file${NC}"
                        failed=$((failed + 1))
                    fi
                fi
            done <<< "$target_files"
            
            if [ $failed -gt 0 ]; then
                echo -e "${RED}Format errors found in $failed files${NC}"
                echo -e "Run: $0 --fix"
                exit 1
            fi
            
            echo -e "${GREEN}✓ clang-format check passed${NC}"
            ;;
        fix)
            echo -e "${BLUE}▶ clang-format fix: $file_count files${NC}"
            
            printf '%s\n' "$target_files" | while IFS= read -r file; do
                if [ -f "$file" ]; then
                    $CLANG_FORMAT -i "$file"
                fi
            done
            
            echo -e "${GREEN}✓ clang-format fix completed${NC}"
            ;;
    esac
}

main
