#!/bin/bash
# run-clang-tidy.sh - clang-tidy 実行の単一エントリポイント
#
# このスクリプトは clang-tidy の対象範囲を一元管理します。
# Makefile、pre-commit-check.sh、CI はすべてこのスクリプトを呼び出します。
#
# 使い方:
#   ./scripts/run-clang-tidy.sh                    # 全対象ファイルをチェック
#   ./scripts/run-clang-tidy.sh --changed          # 変更ファイルのみ
#   ./scripts/run-clang-tidy.sh --list             # 対象ファイル一覧を表示
#   ./scripts/run-clang-tidy.sh --dry-run          # 実行せずコマンドを表示
#   ./scripts/run-clang-tidy.sh file1.cpp file2.cpp  # 指定ファイルのみ

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
# 設定（対象範囲の単一ソース - common.sh から取得）
# ===========================================================================

# clang-tidy の対象ディレクトリ（tests は除外）
TARGET_DIRS="$SAPPP_SOURCE_DIRS"

# 除外パターン（common.sh から取得）
EXCLUDE_PATTERN="$SAPPP_TIDY_EXCLUDE_PATTERNS"

# オプション
MODE="all"  # all|changed|list|dry-run
SPECIFIC_FILES=()
BUILD_DIR="${SAPPP_BUILD_DIR:-build}"
CLANG_BUILD_DIR="${SAPPP_BUILD_CLANG_DIR:-build-clang}"
BUILD_JOBS="$(get_build_jobs)"

# ===========================================================================
# オプション解析
# ===========================================================================

print_help() {
    cat << EOF
Usage: $0 [OPTIONS] [FILES...]

clang-tidy 実行の単一エントリポイント。対象範囲はこのスクリプトで一元管理されます。

Options:
  --all           全対象ファイルをチェック（デフォルト）
  --changed       変更ファイルのみチェック（git diff）
  --list          対象ファイル一覧を表示（実行しない）
  --dry-run       実行せずコマンドを表示
  --build-dir DIR compile_commands.json のあるディレクトリ（デフォルト: build）
  --jobs N        並列度（デフォルト: nproc）
  --help, -h      このヘルプを表示

対象範囲:
  ディレクトリ: $TARGET_DIRS
  除外:         ${EXCLUDE_PATTERNS[*]}
  拡張子:       $EXTENSIONS

Examples:
  $0                      # 全ファイルチェック
  $0 --changed            # 変更ファイルのみ
  $0 libs/common/hash.cpp # 特定ファイルのみ
  $0 --list | wc -l       # 対象ファイル数を確認
EOF
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --all)
            MODE="all"
            shift
            ;;
        --changed)
            MODE="changed"
            shift
            ;;
        --list)
            MODE="list"
            shift
            ;;
        --dry-run)
            MODE="dry-run"
            shift
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --jobs)
            BUILD_JOBS="$2"
            shift 2
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
# clang-tidy 検出（共通ライブラリ使用）
# ===========================================================================

CLANG_TIDY="$(detect_clang_tidy)"

if [ -z "$CLANG_TIDY" ] && [ "$MODE" != "list" ]; then
    echo -e "${RED}Error: clang-tidy not found${NC}" >&2
    echo "Install with: sudo apt install clang-tidy-19" >&2
    exit 1
fi

# ===========================================================================
# compile_commands.json 検出
# ===========================================================================

detect_compile_commands() {
    # Clang ビルドディレクトリを優先（GCC固有フラグの警告を回避）
    if [ -f "$CLANG_BUILD_DIR/compile_commands.json" ]; then
        echo "$CLANG_BUILD_DIR"
    elif [ -f "$BUILD_DIR/compile_commands.json" ]; then
        echo "$BUILD_DIR"
    else
        echo ""
    fi
}

TIDY_BUILD_DIR="$(detect_compile_commands)"

if [ -z "$TIDY_BUILD_DIR" ] && [ "$MODE" != "list" ]; then
    echo -e "${RED}Error: compile_commands.json not found${NC}" >&2
    echo "Run: cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
    exit 1
fi

# GCC compile_commands 使用時の追加引数
TIDY_EXTRA_ARGS=""
if [ "$TIDY_BUILD_DIR" = "$BUILD_DIR" ] && [ -f "$BUILD_DIR/compile_commands.json" ]; then
    # GCC compile_commands を使う場合、GCC固有警告オプションを抑制
    if grep -q "gcc" "$BUILD_DIR/compile_commands.json" 2>/dev/null; then
        TIDY_EXTRA_ARGS="--extra-arg=-Wno-unknown-warning-option --extra-arg=-Wno-error=unknown-warning-option"
    fi
fi

# ===========================================================================
# 対象ファイル収集（共通ライブラリ使用）
# ===========================================================================

collect_all_files() {
    collect_cpp_source_files "$TARGET_DIRS" "$EXCLUDE_PATTERN"
}

collect_changed_files() {
    if ! git rev-parse --git-dir > /dev/null 2>&1; then
        echo -e "${YELLOW}Warning: Not a git repository, falling back to all files${NC}" >&2
        collect_all_files
        return
    fi

    local changed_files
    changed_files="$(collect_changed_cpp_files "$TARGET_DIRS")"

    if [ -z "$changed_files" ]; then
        echo -e "${YELLOW}No changed files detected${NC}" >&2
        return
    fi

    # C++ ソースファイルのみフィルタ（ヘッダ除外）
    local cpp_pattern="\\.($SAPPP_CPP_EXTENSIONS)$"
    local filtered
    if [ -n "$(detect_ripgrep)" ]; then
        filtered=$(printf '%s\n' "$changed_files" | rg -e "$cpp_pattern" || true)
    else
        filtered=$(printf '%s\n' "$changed_files" | grep -E "$cpp_pattern" || true)
    fi
    
    # 除外パターン適用
    if [ -n "$EXCLUDE_PATTERN" ] && [ -n "$filtered" ]; then
        printf '%s\n' "$filtered" | grep -vE "$EXCLUDE_PATTERN" | sort -u || true
    else
        printf '%s\n' "$filtered" | sort -u
    fi
}

collect_target_files() {
    case "$MODE" in
        all|dry-run)
            collect_all_files
            ;;
        changed)
            collect_changed_files
            ;;
        list)
            collect_all_files
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
        dry-run)
            echo -e "${BLUE}Would run clang-tidy on $file_count files:${NC}"
            printf '%s\n' "$target_files"
            echo ""
            echo -e "${BLUE}Command:${NC}"
            echo "$CLANG_TIDY -p $TIDY_BUILD_DIR --warnings-as-errors='*' $TIDY_EXTRA_ARGS <file>"
            exit 0
            ;;
    esac

    echo -e "${BLUE}▶ clang-tidy: $file_count files (jobs=$BUILD_JOBS)${NC}"
    
    # 並列実行
    printf '%s\n' "$target_files" | tr '\n' '\0' | \
        xargs -0 -P"$BUILD_JOBS" -I{} \
        $CLANG_TIDY -p "$TIDY_BUILD_DIR" --warnings-as-errors='*' $TIDY_EXTRA_ARGS {}

    echo -e "${GREEN}✓ clang-tidy completed${NC}"
}

main
