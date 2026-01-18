#!/bin/bash
# pre-commit-check.sh - CI相当のローカルチェックを実行
#
# 使い方:
#   ./scripts/pre-commit-check.sh          # フルチェック（CI相当）
#   ./scripts/pre-commit-check.sh --smart  # 変更内容に応じて最小化
#   ./scripts/pre-commit-check.sh --quick  # 最小限の高速チェック
#   ./scripts/pre-commit-check.sh --ci     # CIと完全一致の厳格モード
#   ./scripts/pre-commit-check.sh --help   # ヘルプ表示

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
MODE="${SAPPP_GATE_MODE:-full}" # full|smart|quick|ci
QUICK_MODE=false
SMART_MODE=false
CI_MODE=false
STRICT_TOOLS=false
SKIP_CLANG_BUILD=false
SKIP_TIDY=false
SKIP_SCHEMA=false
SKIP_DETERMINISM=false
CLEAN_CLANG_BUILD=false
TIDY_SCOPE="changed" # changed|all
CHECK_MODE="full"
STAMP_FILE="${SAPPP_CI_STAMP_FILE:-}"
NO_STAMP=false

set_mode_full() {
    MODE="full"
    QUICK_MODE=false
    SMART_MODE=false
    CI_MODE=false
    STRICT_TOOLS=true
    SKIP_CLANG_BUILD=false
    SKIP_TIDY=false
    SKIP_SCHEMA=false
    SKIP_DETERMINISM=false
    TIDY_SCOPE="all"
    CHECK_MODE="full"
}

set_mode_smart() {
    MODE="smart"
    QUICK_MODE=false
    SMART_MODE=true
    CI_MODE=false
    STRICT_TOOLS=false
    SKIP_CLANG_BUILD=true
    SKIP_TIDY=false
    SKIP_SCHEMA=false
    SKIP_DETERMINISM=false
    TIDY_SCOPE="changed"
    CHECK_MODE="smart"
}

set_mode_quick() {
    MODE="quick"
    QUICK_MODE=true
    SMART_MODE=false
    CI_MODE=false
    STRICT_TOOLS=false
    SKIP_CLANG_BUILD=true
    SKIP_TIDY=true
    SKIP_SCHEMA=true
    SKIP_DETERMINISM=true
    TIDY_SCOPE="changed"
    CHECK_MODE="quick"
}

set_mode_ci() {
    MODE="ci"
    QUICK_MODE=false
    SMART_MODE=false
    CI_MODE=true
    STRICT_TOOLS=true
    SKIP_CLANG_BUILD=false
    SKIP_TIDY=false
    SKIP_SCHEMA=false
    SKIP_DETERMINISM=false
    TIDY_SCOPE="all"
    CHECK_MODE="ci"
}

case "$MODE" in
    smart)
        set_mode_smart
        ;;
    quick)
        set_mode_quick
        ;;
    ci)
        set_mode_ci
        ;;
    *)
        set_mode_full
        ;;
esac

for arg in "$@"; do
    case $arg in
        --full)
            set_mode_full
            ;;
        --smart)
            set_mode_smart
            ;;
        --quick)
            set_mode_quick
            ;;
        --ci)
            set_mode_ci
            ;;
        --skip-clang)
            SKIP_CLANG_BUILD=true
            ;;
        --with-clang)
            SKIP_CLANG_BUILD=false
            ;;
        --clean-clang)
            CLEAN_CLANG_BUILD=true
            ;;
        --skip-tidy)
            SKIP_TIDY=true
            ;;
        --skip-schema)
            SKIP_SCHEMA=true
            ;;
        --skip-determinism)
            SKIP_DETERMINISM=true
            ;;
        --tidy-all)
            TIDY_SCOPE="all"
            ;;
        --tidy-changed)
            TIDY_SCOPE="changed"
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
            echo "  --full             フルチェック（CI相当）"
            echo "  --smart            変更内容に応じて最小化"
            echo "  --quick            最小限の高速チェック"
            echo "  --ci               CIと完全一致の厳格モード"
            echo "  --skip-clang        Clang 19 ビルドをスキップ"
            echo "  --with-clang        smart モードでも Clang ビルドを実行"
            echo "  --clean-clang       Clang ビルドをクリーン実行"
            echo "  --skip-tidy         clang-tidy をスキップ"
            echo "  --skip-schema       スキーマ検証をスキップ"
            echo "  --skip-determinism  決定性テストをスキップ"
            echo "  --tidy-all          clang-tidy を全ファイルに適用"
            echo "  --tidy-changed      clang-tidy を変更ファイルに限定"
            echo "  --stamp-file=       成功時のスタンプ保存先を指定"
            echo "  --no-stamp          スタンプを出力しない"
            echo "  --help, -h          このヘルプを表示"
            exit 0
            ;;
    esac
done

# CIモードの整合性チェック
if [ "$CI_MODE" = true ]; then
    if [ "$SKIP_CLANG_BUILD" = true ] || [ "$SKIP_TIDY" = true ] || [ "$SKIP_SCHEMA" = true ] \
        || [ "$SKIP_DETERMINISM" = true ]; then
        echo -e "${RED}Error: --ci では skip オプションを使用できません${NC}"
        exit 1
    fi
    if [ "$TIDY_SCOPE" != "all" ]; then
        echo -e "${YELLOW}⚠ --ci では clang-tidy を全ファイルに固定します${NC}"
        TIDY_SCOPE="all"
    fi
fi

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

filter_paths() {
    local pattern="$1"
    if command -v rg &> /dev/null; then
        rg -e "$pattern"
    else
        grep -E "$pattern"
    fi
}

collect_changed_files() {
    if ! git rev-parse --git-dir > /dev/null 2>&1; then
        return 0
    fi

    local files=""
    if git rev-parse --verify HEAD > /dev/null 2>&1; then
        files=$(
            {
                git diff --name-only --diff-filter=ACMR HEAD
                git diff --name-only --cached --diff-filter=ACMR HEAD
                git ls-files --others --exclude-standard
            } | sort -u
        )
    else
        files=$(
            {
                git ls-files
                git ls-files --others --exclude-standard
            } | sort -u
        )
    fi

    echo "$files"
}

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

    local skipped_steps=""
    if [ ${#SKIPPED[@]} -gt 0 ]; then
        local IFS=,
        skipped_steps="${SKIPPED[*]}"
    fi

    cat > "$STAMP_FILE" << EOF
{"stamp_version":"v2","check_mode":"$CHECK_MODE","tidy_scope":"$TIDY_SCOPE","skipped_steps":"$skipped_steps","tree_hash":"$tree_hash","head_before":"$head_hash","checked_at":$checked_at}
EOF
    echo -e "${GREEN}✓ スタンプを保存しました: $STAMP_FILE${NC}"
}

# 結果追跡
PASSED=()
FAILED=()
SKIPPED=()

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

CHANGED_FILES="$(collect_changed_files || true)"
DETECTED_CHANGES=false
if [ -n "$CHANGED_FILES" ]; then
    DETECTED_CHANGES=true
fi

CHANGED_CPP=""
CHANGED_HEADERS=""
CHANGED_SCHEMA=""
CHANGED_CMAKE=""
CHANGED_TESTS=""
CHANGED_DOCS=""
CHANGED_SCRIPTS=""

if [ "$DETECTED_CHANGES" = true ]; then
    CHANGED_CPP=$(printf '%s\n' "$CHANGED_FILES" | filter_paths '^(libs|tools|include|tests)/.*\\.(cpp|cc|cxx)$' || true)
    CHANGED_HEADERS=$(printf '%s\n' "$CHANGED_FILES" | filter_paths '^(libs|tools|include|tests)/.*\\.(hpp|h)$' || true)
    CHANGED_SCHEMA=$(printf '%s\n' "$CHANGED_FILES" | filter_paths '^schemas/.*\\.schema\\.json$' || true)
    CHANGED_CMAKE=$(printf '%s\n' "$CHANGED_FILES" | filter_paths '(^CMakeLists\\.txt$|^cmake/|^CMakePresets\\.json$)' || true)
    CHANGED_TESTS=$(printf '%s\n' "$CHANGED_FILES" | filter_paths '^tests/' || true)
    CHANGED_DOCS=$(printf '%s\n' "$CHANGED_FILES" | filter_paths '^(docs/|README\\.md$|CONTRIBUTING\\.md$|AGENTS\\.md$)' || true)
    CHANGED_SCRIPTS=$(printf '%s\n' "$CHANGED_FILES" | filter_paths '^(scripts/|docker/|\\.github/|Makefile$)' || true)
fi

HAS_CPP_CHANGES=false
HAS_HEADER_CHANGES=false
HAS_SCHEMA_CHANGES=false
HAS_BUILD_CHANGES=false

if [ -n "$CHANGED_CPP" ]; then
    HAS_CPP_CHANGES=true
fi
if [ -n "$CHANGED_HEADERS" ]; then
    HAS_HEADER_CHANGES=true
fi
if [ -n "$CHANGED_SCHEMA" ]; then
    HAS_SCHEMA_CHANGES=true
fi
if [ -n "$CHANGED_CPP" ] || [ -n "$CHANGED_HEADERS" ] || [ -n "$CHANGED_CMAKE" ] || [ -n "$CHANGED_TESTS" ]; then
    HAS_BUILD_CHANGES=true
fi

if [ "$SMART_MODE" = true ] && [ "$DETECTED_CHANGES" = false ]; then
    echo -e "${YELLOW}⚠ 変更検出に失敗したため smart モードをフル実行に切り替えます${NC}"
    SMART_MODE=false
    CHECK_MODE="full"
    STRICT_TOOLS=true
    if [ "$TIDY_SCOPE" = "changed" ]; then
        TIDY_SCOPE="all"
    fi
fi

collect_tidy_files() {
    local files=""
    # テストファイルと frontend_clang は clang-tidy 対象外
    # - tests: テストは何でもありにする
    # - frontend_clang: Clang AST API の制約で警告対応が困難
    local target_dirs="libs tools include"
    local exclude_pattern="libs/frontend_clang/"
    
    if [ "$TIDY_SCOPE" = "all" ]; then
        find $target_dirs \( -name "*.cpp" -o -name "*.cc" -o -name "*.cxx" \) -print \
            | grep -v "$exclude_pattern" | sort -u
        return 0
    fi

    if [ -n "$CHANGED_CPP" ]; then
        files=$(printf '%s\n' "$CHANGED_CPP" | filter_paths '^(libs|tools|include)/' | sort -u || true)
    elif [ -n "$CHANGED_HEADERS" ]; then
        files=$(find $target_dirs \( -name "*.cpp" -o -name "*.cc" -o -name "*.cxx" \) -print | sort -u)
    else
        files=""
    fi

    if [ -n "$files" ]; then
        files=$(printf '%s\n' "$files" | grep -v "$exclude_pattern" || true)
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
        local status=$?
        if [ "$status" -eq 2 ]; then
            echo -e "${YELLOW}↷ $name: SKIPPED${NC}"
            SKIPPED+=("$name")
        else
            echo -e "${RED}✗ $name: FAILED${NC}"
            FAILED+=("$name")
        fi
    fi
}

echo -e "${YELLOW}"
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║            SAP++ Pre-Commit CI Check                          ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

echo -e "${BLUE}Mode: $CHECK_MODE (tidy_scope=$TIDY_SCOPE)${NC}"
if [ "$DETECTED_CHANGES" = true ]; then
    CHANGE_COUNT=$(printf '%s\n' "$CHANGED_FILES" | wc -l | tr -d ' ')
    echo -e "${BLUE}Changed files detected: $CHANGE_COUNT${NC}"
fi

# ===========================================================================
# 1. clang-format チェック
# ===========================================================================
run_check "Format Check (clang-format)" '
    if [ "$SMART_MODE" = true ] && [ "$HAS_CPP_CHANGES" = false ] && [ "$HAS_HEADER_CHANGES" = false ]; then
        echo "No C++ changes; skipping format check"
        exit 2
    fi

    if command -v clang-format-19 &> /dev/null; then
        CLANG_FORMAT=clang-format-19
    elif command -v clang-format &> /dev/null; then
        CLANG_FORMAT=clang-format
    else
        if [ "$STRICT_TOOLS" = true ]; then
            echo "Error: clang-format not found"
            exit 1
        fi
        echo "Warning: clang-format not found, skipping"
        exit 2
    fi

    if [ "$SMART_MODE" = true ] && [ "$DETECTED_CHANGES" = true ]; then
        TARGET_FILES=$(printf "%s\n" "$CHANGED_CPP" "$CHANGED_HEADERS" | filter_paths "\\.(cpp|hpp|h)$" | sort -u || true)
        if [ -z "$TARGET_FILES" ]; then
            echo "No C++ files to check"
            exit 2
        fi
        printf "%s\n" "$TARGET_FILES" | tr "\\n" "\\0" | xargs -0 $CLANG_FORMAT --dry-run --Werror
    else
        find libs tools tests include \
            \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) \
            -print0 2>/dev/null | xargs -0 $CLANG_FORMAT --dry-run --Werror
    fi
'

# ===========================================================================
# 2. GCC 14 ビルド
# ===========================================================================
run_check "Build (GCC 14)" '
    if [ "$SMART_MODE" = true ] && [ "$HAS_BUILD_CHANGES" = false ]; then
        echo "No build-relevant changes; skipping GCC build"
        exit 2
    fi

    if command -v g++-14 &> /dev/null; then
        CXX_COMPILER=g++-14
        C_COMPILER=gcc-14
    else
        if [ "$STRICT_TOOLS" = true ]; then
            echo "Error: g++-14 not found"
            exit 1
        fi
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
    if [ "$SMART_MODE" = true ] && [ "$HAS_BUILD_CHANGES" = false ]; then
        echo "No build-relevant changes; skipping tests"
        exit 2
    fi
    ctest --test-dir "$BUILD_DIR" --output-on-failure -j "$BUILD_JOBS"
'

# ===========================================================================
# 4. 決定性テスト
# ===========================================================================
run_check "Determinism Tests" '
    if [ "$SKIP_DETERMINISM" = true ]; then
        echo "Determinism tests skipped by option"
        exit 2
    fi
    if [ "$SMART_MODE" = true ] && [ "$HAS_BUILD_CHANGES" = false ]; then
        echo "No build-relevant changes; skipping determinism tests"
        exit 2
    fi
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
        if [ "$STRICT_TOOLS" = true ]; then
            echo -e "${RED}Error: clang++-19 not found${NC}"
            exit 1
        fi
        echo -e "${YELLOW}Warning: clang++-19 not found, skipping Clang build${NC}"
        SKIP_CLANG_BUILD=true
    fi
fi

if [ "$SKIP_CLANG_BUILD" = false ]; then
    run_check "Build (Clang)" '
        if [ "$SMART_MODE" = true ] && [ "$HAS_BUILD_CHANGES" = false ]; then
            echo "No build-relevant changes; skipping Clang build"
            exit 2
        fi
        if [ "$CLEAN_CLANG_BUILD" = true ]; then
            rm -rf "$CLANG_BUILD_DIR"
        fi
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
else
    run_check "Build (Clang)" '
        echo "Clang build skipped by option or environment"
        exit 2
    '
fi

# ===========================================================================
# 6. clang-tidy（オプション）
# ===========================================================================
if [ "$SKIP_TIDY" = true ]; then
    run_check "Static Analysis (clang-tidy)" '
        echo "clang-tidy skipped by option"
        exit 2
    '
else
    run_check "Static Analysis (clang-tidy)" '
        if [ "$SMART_MODE" = true ] && [ "$HAS_CPP_CHANGES" = false ] && [ "$HAS_HEADER_CHANGES" = false ]; then
            echo "No C++ changes; skipping clang-tidy"
            exit 2
        fi

        if command -v clang-tidy-19 &> /dev/null; then
            CLANG_TIDY=clang-tidy-19
        elif command -v clang-tidy &> /dev/null; then
            CLANG_TIDY=clang-tidy
        else
            if [ "$STRICT_TOOLS" = true ]; then
                echo "Error: clang-tidy not found"
                exit 1
            fi
            echo "Warning: clang-tidy not found, skipping"
            exit 2
        fi

        CHANGED_FILES=$(collect_tidy_files)
        if [ -z "$CHANGED_FILES" ]; then
            echo "No C++ files to check"
            exit 2
        fi

        echo "Checking files:"
        echo "$CHANGED_FILES"

        # Use Clang build directory if available, otherwise use GCC build with extra args to suppress GCC-specific flags
        TIDY_BUILD_DIR="$BUILD_DIR"
        TIDY_EXTRA_ARGS=""
        if [ -d "$BUILD_CLANG_DIR" ] && [ -f "$BUILD_CLANG_DIR/compile_commands.json" ]; then
            TIDY_BUILD_DIR="$BUILD_CLANG_DIR"
        else
            # Suppress GCC-specific unknown warning options when using GCC compile_commands
            TIDY_EXTRA_ARGS="--extra-arg=-Wno-unknown-warning-option --extra-arg=-Wno-error=unknown-warning-option"
        fi

        printf "%s\n" "$CHANGED_FILES" | tr "\\n" "\\0" | \
            xargs -0 -P"$BUILD_JOBS" -I{} $CLANG_TIDY -p "$TIDY_BUILD_DIR" \
                --warnings-as-errors="*" $TIDY_EXTRA_ARGS {}
    '
fi

# ===========================================================================
# 7. スキーマ検証（オプション）
# ===========================================================================
if [ "$SKIP_SCHEMA" = true ]; then
    run_check "Schema Validation" '
        echo "Schema validation skipped by option"
        exit 2
    '
else
    run_check "Schema Validation" '
        if [ "$SMART_MODE" = true ] && [ "$HAS_SCHEMA_CHANGES" = false ]; then
            echo "No schema changes; skipping schema validation"
            exit 2
        fi

        if ! command -v ajv &> /dev/null; then
            if [ "$STRICT_TOOLS" = true ]; then
                echo "Error: ajv-cli not found"
                exit 1
            fi
            echo "Warning: ajv-cli not found, skipping"
            exit 2
        fi

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

if [ ${#SKIPPED[@]} -gt 0 ]; then
    echo -e "\n${YELLOW}Skipped: ${#SKIPPED[@]}${NC}"
    for name in "${SKIPPED[@]}"; do
        echo -e "  ${YELLOW}↷ $name${NC}"
    done
fi

if [ ${#FAILED[@]} -gt 0 ]; then
    echo -e "\n${RED}Failed: ${#FAILED[@]}${NC}"
    for name in "${FAILED[@]}"; do
        echo -e "  ${RED}✗ $name${NC}"
    done
    echo -e "\n${RED}━━━ CI チェック失敗 ━━━${NC}"
    exit 1
else
    if [ "$CHECK_MODE" = "full" ] || [ "$CHECK_MODE" = "ci" ]; then
        if [ "$TIDY_SCOPE" != "all" ]; then
            CHECK_MODE="partial"
            echo -e "${YELLOW}⚠ tidy_scope が all ではないため check_mode=partial に降格します${NC}"
        fi
        if [ ${#SKIPPED[@]} -gt 0 ]; then
            CHECK_MODE="partial"
            echo -e "${YELLOW}⚠ 一部のチェックがスキップされたため check_mode=partial に降格します${NC}"
        fi
    fi
    write_stamp
    echo -e "\n${GREEN}━━━ 全チェック通過！コミットしてOK ━━━${NC}"
    exit 0
fi
