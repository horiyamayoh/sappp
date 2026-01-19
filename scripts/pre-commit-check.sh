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

# スクリプトのディレクトリからプロジェクトルートを取得
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# 共通ライブラリ読み込み
source "$SCRIPT_DIR/lib/common.sh"

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
# 並列度（共通ライブラリ使用）
BUILD_JOBS="$(get_build_jobs)"

# CMakeジェネレータ（共通ライブラリ使用）
GENERATOR="$(get_cmake_generator)"

# ccache オプション（共通ライブラリ使用）
CMAKE_LAUNCHER_OPTS="$(get_ccache_cmake_opts)"

filter_paths() {
    local pattern="$1"
    if [ -n "$(detect_ripgrep)" ]; then
        rg -e "$pattern"
    else
        grep -E "$pattern"
    fi
}

# collect_changed_files は共通ライブラリの collect_changed_cpp_files を使用
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

# has_compiler_warnings / print_compiler_warnings は共通ライブラリから使用

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

# ビルドディレクトリ（SAPPP_BUILD_DIR/SAPPP_BUILD_CLANG_DIR で変更可能）
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

# clang-tidy 対象ファイル収集は scripts/run-clang-tidy.sh に委譲
# 単一ソース化により Makefile / pre-commit-check.sh / CI の不整合を防止

run_check() {
    local name="$1"
    local cmd="$2"
    
    echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}▶ $name${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    if ( eval "$cmd" ); then
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
# 1. clang-format チェック（単一スクリプトに委譲）
# ===========================================================================
run_check "Format Check (clang-format)" '
    if [ "$SMART_MODE" = true ] && [ "$HAS_CPP_CHANGES" = false ] && [ "$HAS_HEADER_CHANGES" = false ]; then
        echo "No C++ changes; skipping format check"
        exit 2
    fi

    # 単一スクリプトで対象範囲を一元管理
    FORMAT_SCOPE="--all"
    if [ "$SMART_MODE" = true ]; then
        FORMAT_SCOPE="--changed"
    fi

    ./scripts/run-clang-format.sh --check $FORMAT_SCOPE || {
        local status=$?
        if [ $status -eq 0 ]; then
            exit 0
        elif [ $status -eq 2 ]; then
            # clang-format not found
            if [ "$STRICT_TOOLS" = true ]; then
                echo "Error: clang-format not found"
                exit 1
            fi
            exit 2
        else
            exit 1
        fi
    }
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
#    単一スクリプト run-clang-tidy.sh に委譲（対象範囲の一元管理）
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

        # 単一スクリプトで対象範囲を一元管理
        TIDY_MODE="--all"
        if [ "$TIDY_SCOPE" = "changed" ]; then
            TIDY_MODE="--changed"
        fi

        # SAPPP_BUILD_DIR / SAPPP_BUILD_CLANG_DIR は run-clang-tidy.sh が参照
        export SAPPP_BUILD_DIR="$BUILD_DIR"
        export SAPPP_BUILD_CLANG_DIR="$BUILD_CLANG_DIR"
        export SAPPP_BUILD_JOBS="$BUILD_JOBS"

        ./scripts/run-clang-tidy.sh $TIDY_MODE || {
            local status=$?
            if [ $status -eq 0 ]; then
                exit 0
            elif [ $status -eq 2 ]; then
                # clang-tidy not found（非strict時）
                if [ "$STRICT_TOOLS" = true ]; then
                    echo "Error: clang-tidy not found"
                    exit 1
                fi
                exit 2
            else
                exit 1
            fi
        }
    '
fi

# ===========================================================================
# 7. スキーマ検証（単一スクリプトに委譲）
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

        # 単一スクリプトで検証を一元管理
        ./scripts/run-schema-validation.sh || {
            local status=$?
            if [ $status -eq 0 ]; then
                exit 0
            elif [ $status -eq 2 ]; then
                # ajv not found
                if [ "$STRICT_TOOLS" = true ]; then
                    echo "Error: ajv-cli not found"
                    exit 1
                fi
                exit 2
            else
                exit 1
            fi
        }
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
