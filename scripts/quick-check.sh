#!/bin/bash
# quick-check.sh - 高速チェック（30秒以内目標）
#
# 使い方:
#   ./scripts/quick-check.sh           # 変更ファイルのみチェック
#   ./scripts/quick-check.sh --all     # 全ファイルチェック
#
# このスクリプトはコミット前の高速フィードバック用です。
# フルチェックは pre-commit-check.sh または docker-ci.sh を使用してください。

set -euo pipefail

# スクリプトのディレクトリからプロジェクトルートを取得
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# 共通ライブラリ読み込み
source "$SCRIPT_DIR/lib/common.sh"

# オプション
CHECK_ALL=false
for arg in "$@"; do
    case $arg in
        --all)
            CHECK_ALL=true
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "高速チェック（30秒以内目標）"
            echo ""
            echo "Options:"
            echo "  --all       全ファイルをチェック（デフォルトは変更ファイルのみ）"
            echo "  --help, -h  このヘルプを表示"
            exit 0
            ;;
    esac
done

# 並列度（共通ライブラリ使用）
BUILD_JOBS="$(get_build_jobs)"

# CMakeジェネレータ（共通ライブラリ使用）
GENERATOR="$(get_cmake_generator)"

# ccache オプション（共通ライブラリ使用）
CMAKE_LAUNCHER_OPTS="$(get_ccache_cmake_opts)"

# has_compiler_warnings / print_compiler_warnings は共通ライブラリから使用

# 開始時刻
START_TIME=$(date +%s)

echo -e "${YELLOW}━━━ SAP++ Quick Check ━━━${NC}"

# ===========================================================================
# 1. 変更ファイルの検出
# ===========================================================================
TARGET_DIRS="$SAPPP_SOURCE_DIRS $SAPPP_TEST_DIR"

if [ "$CHECK_ALL" = true ]; then
    CHANGED_CPP=$(collect_all_cpp_files "$TARGET_DIRS" "" | head -100)
else
    CHANGED_CPP=$(collect_changed_cpp_files "$TARGET_DIRS")
fi

if [ -z "$CHANGED_CPP" ]; then
    echo -e "${GREEN}✓ 変更されたC++ファイルはありません${NC}"
    CHANGED_CPP=""
fi

# ===========================================================================
# 2. clang-format チェック（単一スクリプトに委譲）
# ===========================================================================
echo -e "\n${BLUE}▶ Format Check${NC}"

if [ -n "$CHANGED_CPP" ]; then
    FORMAT_SCOPE="--changed"
    if [ "$CHECK_ALL" = true ]; then
        FORMAT_SCOPE="--all"
    fi
    
    if ./scripts/run-clang-format.sh --check $FORMAT_SCOPE 2>/dev/null; then
        echo -e "${GREEN}✓ Format check passed${NC}"
    else
        exit_code=$?
        if [ $exit_code -eq 1 ]; then
            echo -e "${RED}Format errors found. Run: ./scripts/run-clang-format.sh --fix${NC}"
            exit 1
        else
            echo -e "${YELLOW}Warning: clang-format not found, skipping${NC}"
        fi
    fi
else
    echo -e "${GREEN}✓ No files to check${NC}"
fi

# ===========================================================================
# 3. インクリメンタルビルド
# ===========================================================================
echo -e "\n${BLUE}▶ Incremental Build${NC}"

# ビルドディレクトリ（環境変数で上書き可能 - Docker CI用）
BUILD_DIR="${SAPPP_BUILD_DIR:-build}"
LOG_DIR="${SAPPP_LOG_DIR:-$BUILD_DIR/ci-logs}"
mkdir -p "$LOG_DIR"

# ビルドディレクトリがなければ設定
if [ ! -f "$BUILD_DIR/Makefile" ] && [ ! -f "$BUILD_DIR/build.ninja" ]; then
    echo "Configuring CMake..."
    
    # コンパイラ検出（共通ライブラリ使用）
    CXX="$(detect_gcc14)"
    CC="$(detect_gcc14_c)"
    
    if [ -z "$CXX" ]; then
        CXX="g++"
        CC="gcc"
    fi
    
    if ! cmake -S . -B "$BUILD_DIR" $GENERATOR \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=$CC \
        -DCMAKE_CXX_COMPILER=$CXX \
        -DSAPPP_BUILD_TESTS=ON \
        -DSAPPP_BUILD_CLANG_FRONTEND=OFF \
        -DSAPPP_WERROR=ON \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        $CMAKE_LAUNCHER_OPTS \
        > "$LOG_DIR/quick-config.log" 2>&1; then
        echo -e "${RED}✗ CMake configure failed${NC}"
        tail -100 "$LOG_DIR/quick-config.log"
        exit 1
    fi
fi

# ビルド（並列）
BUILD_LOG="$LOG_DIR/quick-build.log"
if ! cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS" > "$BUILD_LOG" 2>&1; then
    echo -e "${RED}✗ Build failed${NC}"
    tail -100 "$BUILD_LOG"
    exit 1
fi
if has_compiler_warnings "$BUILD_LOG"; then
    echo -e "${RED}✗ Compiler warnings found${NC}"
    print_compiler_warnings "$BUILD_LOG"
    exit 1
fi
echo -e "${GREEN}✓ Build passed${NC}"

# ===========================================================================
# 4. テスト実行（高速モード）
# ===========================================================================
echo -e "\n${BLUE}▶ Quick Tests${NC}"

# 失敗したテストのみ表示
if ctest --test-dir "$BUILD_DIR" -N -L quick | grep -q "Total Tests: 0"; then
    if ! ctest --test-dir "$BUILD_DIR" --output-on-failure -j "$BUILD_JOBS"; then
        echo -e "${RED}✗ Tests failed${NC}"
        exit 1
    fi
else
    if ! ctest --test-dir "$BUILD_DIR" -L quick --output-on-failure -j "$BUILD_JOBS"; then
        echo -e "${RED}✗ Tests failed${NC}"
        exit 1
    fi
fi
echo -e "${GREEN}✓ Tests passed${NC}"

# ===========================================================================
# サマリー
# ===========================================================================
END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

echo -e "\n${GREEN}━━━ Quick Check 完了 (${ELAPSED}秒) ━━━${NC}"

if [ $ELAPSED -gt 30 ]; then
    echo -e "${YELLOW}⚠ 目標の30秒を超えました。フルビルドが必要かもしれません。${NC}"
fi

echo -e "${BLUE}smart チェック: ./scripts/pre-commit-check.sh --smart${NC}"
echo -e "${BLUE}CI互換チェック: ./scripts/pre-commit-check.sh --ci${NC}"
echo -e "${BLUE}Docker CI:      ./scripts/docker-ci.sh --ci${NC}"
