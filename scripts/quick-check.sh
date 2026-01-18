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

# 色付き出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# スクリプトのディレクトリからプロジェクトルートを取得
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

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

# ccache（デフォルト有効。無効化は SAPPP_USE_CCACHE=0）
if [ -z "${SAPPP_USE_CCACHE:-}" ]; then
    SAPPP_USE_CCACHE=1
fi
CMAKE_LAUNCHER_OPTS=""
if [ "${SAPPP_USE_CCACHE}" = "1" ] && command -v ccache &> /dev/null; then
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

# 開始時刻
START_TIME=$(date +%s)

echo -e "${YELLOW}━━━ SAP++ Quick Check ━━━${NC}"

# ===========================================================================
# 1. 変更ファイルの検出
# ===========================================================================
if [ "$CHECK_ALL" = true ]; then
    CHANGED_CPP=$(find libs tools tests include \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print 2>/dev/null | head -100)
else
    # git diff で変更ファイルを検出（staged + unstaged）
    CHANGED_CPP=""
    if git rev-parse --git-dir > /dev/null 2>&1; then
        if git rev-parse --verify HEAD > /dev/null 2>&1; then
            CHANGED_CPP=$( { git diff --name-only --diff-filter=ACMR HEAD -- libs tools tests include; \
                git diff --name-only --cached --diff-filter=ACMR HEAD -- libs tools tests include; } | sort -u )
        else
            CHANGED_CPP=$(git ls-files -- libs tools tests include 2>/dev/null || true)
        fi
    fi

    if [ -n "$CHANGED_CPP" ]; then
        if command -v rg &> /dev/null; then
            CHANGED_CPP=$(echo "$CHANGED_CPP" | rg -e '\.(cpp|hpp|h)$' || true)
        else
            CHANGED_CPP=$(echo "$CHANGED_CPP" | grep -E '\.(cpp|hpp|h)$' || true)
        fi
    fi
fi

if [ -z "$CHANGED_CPP" ]; then
    echo -e "${GREEN}✓ 変更されたC++ファイルはありません${NC}"
    CHANGED_CPP=""
fi

# ===========================================================================
# 2. clang-format チェック
# ===========================================================================
echo -e "\n${BLUE}▶ Format Check${NC}"

if [ -n "$CHANGED_CPP" ]; then
    if command -v clang-format-19 &> /dev/null; then
        CLANG_FORMAT=clang-format-19
    elif command -v clang-format &> /dev/null; then
        CLANG_FORMAT=clang-format
    else
        echo -e "${YELLOW}Warning: clang-format not found, skipping${NC}"
        CLANG_FORMAT=""
    fi
    
    if [ -n "$CLANG_FORMAT" ]; then
        FORMAT_ERRORS=0
        for file in $CHANGED_CPP; do
            if [ -f "$file" ]; then
                if ! $CLANG_FORMAT --dry-run --Werror "$file" 2>/dev/null; then
                    echo -e "${RED}  ✗ $file${NC}"
                    FORMAT_ERRORS=$((FORMAT_ERRORS + 1))
                fi
            fi
        done
        
        if [ $FORMAT_ERRORS -gt 0 ]; then
            echo -e "${RED}Format errors found. Run: ${CLANG_FORMAT} -i <files>${NC}"
            exit 1
        fi
        echo -e "${GREEN}✓ Format check passed${NC}"
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
    
    # コンパイラ検出
    if command -v g++-14 &> /dev/null; then
        CXX=g++-14
        CC=gcc-14
    else
        CXX=g++
        CC=gcc
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
