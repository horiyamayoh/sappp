#!/bin/bash
# pre-commit-check.sh - CI相当のローカルチェックを実行
#
# 使い方:
#   ./scripts/pre-commit-check.sh         # 全チェック
#   ./scripts/pre-commit-check.sh --quick # 最小限のチェック（ビルド+テスト+フォーマット）
#   ./scripts/pre-commit-check.sh --help  # ヘルプ表示

set -euo pipefail

# 色付き出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# スクリプトのディレクトリからプロジェクトルートを取得
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# オプション解析
QUICK_MODE=false
SKIP_CLANG_BUILD=false
SKIP_TIDY=false
SKIP_SCHEMA=false
TIDY_ALL=false
CHECK_MODE="full"
STAMP_FILE="${SAPPP_CI_STAMP_FILE:-}"
NO_STAMP=false

for arg in "$@"; do
    case $arg in
        --quick)
            QUICK_MODE=true
            SKIP_CLANG_BUILD=true
            SKIP_TIDY=true
            SKIP_SCHEMA=true
            CHECK_MODE="quick"
            ;;
        --skip-clang)
            SKIP_CLANG_BUILD=true
            ;;
        --skip-tidy)
            SKIP_TIDY=true
            ;;
        --skip-schema)
            SKIP_SCHEMA=true
            ;;
        --tidy-all)
            TIDY_ALL=true
            ;;
        --stamp-file=*)
            STAMP_FILE="${arg#*=}"
            ;;
        --no-stamp)
            NO_STAMP=true
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --quick        最小限のチェック（GCCビルド+テスト+フォーマット）"
            echo "  --skip-clang   Clang 19 ビルドをスキップ"
            echo "  --skip-tidy    clang-tidy をスキップ"
            echo "  --skip-schema  スキーマ検証をスキップ"
            echo "  --tidy-all     clang-tidy を全ファイルに適用"
            echo "  --stamp-file=  成功時のスタンプ保存先を指定"
            echo "  --no-stamp     スタンプを出力しない"
            echo "  --help, -h     このヘルプを表示"
            exit 0
            ;;
    esac
done

# 並列度
if command -v nproc &> /dev/null; then
    DEFAULT_JOBS="$(nproc)"
else
    DEFAULT_JOBS=1
fi
BUILD_JOBS="${SAPPP_BUILD_JOBS:-$DEFAULT_JOBS}"

# CMakeジェネレータ
if command -v ninja &> /dev/null; then
    GENERATOR="-G Ninja"
else
    GENERATOR=""
fi

# ccache（任意）
CMAKE_LAUNCHER_OPTS=""
if [ "${SAPPP_USE_CCACHE:-0}" = "1" ] && command -v ccache &> /dev/null; then
    CMAKE_LAUNCHER_OPTS="-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
fi

has_compiler_warnings() {
    local log_file="$1"
    if command -v rg &> /dev/null; then
        rg -n ":[0-9]+:[0-9]+: warning:" "$log_file" > /dev/null
    else
        grep -nE ":[0-9]+:[0-9]+: warning:" "$log_file" > /dev/null
    fi
}

print_compiler_warnings() {
    local log_file="$1"
    if command -v rg &> /dev/null; then
        rg -n ":[0-9]+:[0-9]+: warning:" "$log_file" | head -20
    else
        grep -nE ":[0-9]+:[0-9]+: warning:" "$log_file" | head -20
    fi
}

write_stamp() {
    if [ "$NO_STAMP" = true ] || [ -z "$STAMP_FILE" ]; then
        return 0
    fi

    local stamp_dir
    stamp_dir="$(dirname "$STAMP_FILE")"
    mkdir -p "$stamp_dir"

    local tree_hash=""
    local head_hash=""
    if git rev-parse --git-dir > /dev/null 2>&1; then
        tree_hash="$(git write-tree 2>/dev/null || true)"
        if git rev-parse --verify HEAD > /dev/null 2>&1; then
            head_hash="$(git rev-parse HEAD)"
        fi
    fi

    local checked_at
    checked_at="$(date +%s)"

    cat > "$STAMP_FILE" << EOF
{"check_mode":"$CHECK_MODE","tree_hash":"$tree_hash","head_before":"$head_hash","checked_at":$checked_at}
EOF
    echo -e "${GREEN}✓ スタンプを保存しました: $STAMP_FILE${NC}"
}

# 結果追跡
PASSED=()
FAILED=()

# ビルドディレクトリ（Docker CI では tmpfs で隔離されるため常に build/ を使用）
BUILD_DIR="${SAPPP_BUILD_DIR:-build}"
BUILD_CLANG_DIR="${SAPPP_BUILD_CLANG_DIR:-build-clang}"
CLANG_BUILD_DIR="$BUILD_CLANG_DIR"
LOG_ROOT="${SAPPP_LOG_DIR:-}"
if [ -n "$LOG_ROOT" ]; then
    GCC_LOG_DIR="$LOG_ROOT/gcc"
    CLANG_LOG_DIR="$LOG_ROOT/clang"
else
    GCC_LOG_DIR="$BUILD_DIR/ci-logs"
    CLANG_LOG_DIR="$BUILD_CLANG_DIR/ci-logs"
fi

collect_tidy_files() {
    local files=""
    # テストファイルと frontend_clang は clang-tidy 対象外
    # - tests: テストは何でもありにする
    # - frontend_clang: Clang AST API の制約で警告対応が困難
    local target_dirs="libs tools include"
    local exclude_pattern="libs/frontend_clang/"
    
    if [ "$TIDY_ALL" = true ]; then
        find $target_dirs -name "*.cpp" -print | grep -v "$exclude_pattern"
        return 0
    fi

    if git rev-parse --git-dir > /dev/null 2>&1; then
        if git rev-parse --verify HEAD > /dev/null 2>&1; then
            files=$( { git diff --name-only --diff-filter=ACMR HEAD -- $target_dirs; \
                git diff --name-only --cached --diff-filter=ACMR HEAD -- $target_dirs; } | sort -u | grep -v "$exclude_pattern" || true )
        else
            files=$(git ls-files -- $target_dirs 2>/dev/null | grep -v "$exclude_pattern" || true)
        fi
    fi

    if [ -n "$files" ]; then
        if command -v rg &> /dev/null; then
            files=$(echo "$files" | rg -e '\.cpp$' || true)
        else
            files=$(echo "$files" | grep -E '\.cpp$' || true)
        fi
    fi

    if [ -z "$files" ]; then
        find $target_dirs -name "*.cpp" -print | grep -v "$exclude_pattern"
        return 0
    fi

    echo "$files"
}

run_check() {
    local name="$1"
    local cmd="$2"
    
    echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}▶ $name${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    if eval "$cmd"; then
        echo -e "${GREEN}✓ $name: PASSED${NC}"
        PASSED+=("$name")
    else
        echo -e "${RED}✗ $name: FAILED${NC}"
        FAILED+=("$name")
    fi
}

echo -e "${YELLOW}"
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║            SAP++ Pre-Commit CI Check                          ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

# ===========================================================================
# 1. clang-format チェック
# ===========================================================================
run_check "Format Check (clang-format)" '
    if command -v clang-format-19 &> /dev/null; then
        CLANG_FORMAT=clang-format-19
    elif command -v clang-format &> /dev/null; then
        CLANG_FORMAT=clang-format
    else
        echo "Warning: clang-format not found, skipping"
        CLANG_FORMAT=""
    fi
    if [ -n "$CLANG_FORMAT" ]; then
        find libs tools tests include \
            \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) \
            -print0 2>/dev/null | xargs -0 $CLANG_FORMAT --dry-run --Werror
    fi
'

# ===========================================================================
# 2. GCC 14 ビルド
# ===========================================================================
run_check "Build (GCC 14)" '
    if command -v g++-14 &> /dev/null; then
        CXX_COMPILER=g++-14
        C_COMPILER=gcc-14
    else
        CXX_COMPILER=g++
        C_COMPILER=gcc
        echo "Warning: g++-14 not found, using default g++"
    fi

    mkdir -p "$GCC_LOG_DIR"
    cmake -S . -B "$BUILD_DIR" $GENERATOR \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=$C_COMPILER \
        -DCMAKE_CXX_COMPILER=$CXX_COMPILER \
        -DSAPPP_BUILD_TESTS=ON \
        -DSAPPP_BUILD_CLANG_FRONTEND=OFF \
        -DSAPPP_WERROR=ON \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        $CMAKE_LAUNCHER_OPTS \
        > "$GCC_LOG_DIR/config.log" 2>&1 || { \
            echo "CMake configure failed"; \
            tail -100 "$GCC_LOG_DIR/config.log"; \
            false; } \
    && cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS" > "$GCC_LOG_DIR/build.log" 2>&1 || { \
        echo "Build failed"; \
        tail -100 "$GCC_LOG_DIR/build.log"; \
        false; } \
    && if has_compiler_warnings "$GCC_LOG_DIR/build.log"; then \
        echo "Compiler warnings found"; \
        print_compiler_warnings "$GCC_LOG_DIR/build.log"; \
        false; \
    fi
'

# ===========================================================================
# 3. テスト実行
# ===========================================================================
run_check "All Tests" '
    ctest --test-dir "$BUILD_DIR" --output-on-failure -j "$BUILD_JOBS"
'

# ===========================================================================
# 4. 決定性テスト
# ===========================================================================
run_check "Determinism Tests" '
    ctest --test-dir "$BUILD_DIR" -R determinism --output-on-failure -j "$BUILD_JOBS"
'

# ===========================================================================
# 5. Clang ビルド（オプション）
#    NOTE: libc++ は std::views::enumerate 未実装のため libstdc++ を使用
# ===========================================================================
CLANG_CXX_COMPILER=""
CLANG_C_COMPILER=""
if command -v clang++-19 &> /dev/null; then
    CLANG_CXX_COMPILER=clang++-19
    CLANG_C_COMPILER=clang-19
fi

if [ "$SKIP_CLANG_BUILD" = false ]; then
    if [ -z "$CLANG_CXX_COMPILER" ]; then
        echo -e "${YELLOW}Warning: clang++-19 not found, skipping Clang build${NC}"
        SKIP_CLANG_BUILD=true
    fi
fi

if [ "$SKIP_CLANG_BUILD" = false ]; then
    run_check "Build (Clang)" '
        rm -rf "$CLANG_BUILD_DIR"
        mkdir -p "$CLANG_LOG_DIR"
        cmake -S . -B "$CLANG_BUILD_DIR" $GENERATOR \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER='"$CLANG_C_COMPILER"' \
            -DCMAKE_CXX_COMPILER='"$CLANG_CXX_COMPILER"' \
            -DSAPPP_BUILD_TESTS=ON \
            -DSAPPP_BUILD_CLANG_FRONTEND=OFF \
            -DSAPPP_WERROR=ON \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            $CMAKE_LAUNCHER_OPTS \
            > "$CLANG_LOG_DIR/config.log" 2>&1 || { \
                echo "CMake configure failed"; \
                tail -100 "$CLANG_LOG_DIR/config.log"; \
                false; } \
        && cmake --build "$CLANG_BUILD_DIR" --parallel "$BUILD_JOBS" > "$CLANG_LOG_DIR/build.log" 2>&1 || { \
            echo "Build failed"; \
            tail -100 "$CLANG_LOG_DIR/build.log"; \
            false; } \
        && if has_compiler_warnings "$CLANG_LOG_DIR/build.log"; then \
            echo "Compiler warnings found"; \
            print_compiler_warnings "$CLANG_LOG_DIR/build.log"; \
            false; \
        fi \
        && ctest --test-dir "$CLANG_BUILD_DIR" --output-on-failure -j "$BUILD_JOBS"
    '
fi

# ===========================================================================
# 6. clang-tidy（オプション）
# ===========================================================================
if [ "$SKIP_TIDY" = false ]; then
    run_check "Static Analysis (clang-tidy)" '
        if command -v clang-tidy-19 &> /dev/null; then
            CLANG_TIDY=clang-tidy-19
        elif command -v clang-tidy &> /dev/null; then
            CLANG_TIDY=clang-tidy
        else
            echo "Warning: clang-tidy not found, skipping"
            CLANG_TIDY=""
        fi

        # 変更されたファイルのみチェック（高速化）
        if [ -n "$CLANG_TIDY" ]; then
            CHANGED_FILES=$(collect_tidy_files)
            if [ -z "$CHANGED_FILES" ]; then
                echo "No C++ files to check"
            else
                echo "Checking files:"
                echo "$CHANGED_FILES"
                # Use Clang build directory if available, otherwise use GCC build with extra args to suppress GCC-specific flags
                TIDY_BUILD_DIR="$BUILD_DIR"
                TIDY_EXTRA_ARGS=""
                if [ -d "$BUILD_CLANG_DIR" ] && [ -f "$BUILD_CLANG_DIR/compile_commands.json" ]; then
                    TIDY_BUILD_DIR="$BUILD_CLANG_DIR"
                else
                    # Suppress GCC-specific unknown warning options when using GCC compile_commands
                    TIDY_EXTRA_ARGS="--extra-arg=-Wno-unknown-warning-option"
                fi
                echo "$CHANGED_FILES" | xargs -P"$BUILD_JOBS" -I{} $CLANG_TIDY -p "$TIDY_BUILD_DIR" --warnings-as-errors="*" $TIDY_EXTRA_ARGS {}
            fi
        fi
    '
fi

# ===========================================================================
# 7. スキーマ検証（オプション）
# ===========================================================================
if [ "$SKIP_SCHEMA" = false ]; then
    run_check "Schema Validation" '
        if ! command -v ajv &> /dev/null; then
            echo "Warning: ajv-cli not found, trying with Python"
            if command -v python3 &> /dev/null; then
                SCHEMA_OK=true
                for schema in schemas/*.schema.json; do
                    echo "Validating: $schema"
                    if ! python3 -c "import json; json.load(open(\"$schema\"))"; then
                        SCHEMA_OK=false
                        break
                    fi
                done
                if [ "$SCHEMA_OK" = false ]; then
                    false
                fi
            else
                echo "Warning: No JSON validator found, skipping"
            fi
        else
            # NOTE: ajv-formats is required for date-time format validation
            # Build reference list for cross-schema references (excluding the schema being validated)
            SCHEMA_OK=true
            for schema in schemas/*.schema.json; do
                refs=""
                for ref_schema in schemas/*.schema.json; do
                    if [ "$ref_schema" != "$schema" ]; then
                        refs="$refs -r $ref_schema"
                    fi
                done
                echo "Validating: $schema"
                if ! ajv compile -s "$schema" --spec=draft2020 -c ajv-formats $refs; then
                    SCHEMA_OK=false
                    break
                fi
            done
            if [ "$SCHEMA_OK" = false ]; then
                false
            fi
        fi
    '
fi

# ===========================================================================
# サマリー
# ===========================================================================
echo -e "\n${YELLOW}"
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║                        Summary                                 ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

echo -e "${GREEN}Passed: ${#PASSED[@]}${NC}"
for name in "${PASSED[@]}"; do
    echo -e "  ${GREEN}✓ $name${NC}"
done

if [ ${#FAILED[@]} -gt 0 ]; then
    echo -e "\n${RED}Failed: ${#FAILED[@]}${NC}"
    for name in "${FAILED[@]}"; do
        echo -e "  ${RED}✗ $name${NC}"
    done
    echo -e "\n${RED}━━━ CI チェック失敗 ━━━${NC}"
    exit 1
else
    write_stamp
    echo -e "\n${GREEN}━━━ 全チェック通過！コミットしてOK ━━━${NC}"
    exit 0
fi
