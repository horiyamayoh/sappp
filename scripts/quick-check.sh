#!/bin/bash
# quick-check.sh - 高速チェック（30秒以内目標）
#
# 使い方:
#   ./scripts/quick-check.sh           # 変更ファイルのみチェック
#   ./scripts/quick-check.sh --all     # 全ファイルチェック
#
# このスクリプトはコミット前の高速フィードバック用です。
# フルチェックは pre-commit-check.sh または docker-ci.sh を使用してください。

set -e

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

# 開始時刻
START_TIME=$(date +%s)

echo -e "${YELLOW}━━━ SAP++ Quick Check ━━━${NC}"

# ===========================================================================
# 1. 変更ファイルの検出
# ===========================================================================
if [ "$CHECK_ALL" = true ]; then
    CHANGED_CPP=$(find libs tools tests include -name '*.cpp' -o -name '*.hpp' -o -name '*.h' 2>/dev/null | head -100)
else
    # git diff で変更ファイルを検出（staged + unstaged）
    CHANGED_CPP=$(git diff --name-only --diff-filter=ACMR HEAD -- '*.cpp' '*.hpp' '*.h' 2>/dev/null || true)
    STAGED_CPP=$(git diff --name-only --cached --diff-filter=ACMR -- '*.cpp' '*.hpp' '*.h' 2>/dev/null || true)
    CHANGED_CPP=$(echo -e "$CHANGED_CPP\n$STAGED_CPP" | sort -u | grep -v '^$' || true)
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
    if command -v clang-format-18 &> /dev/null; then
        CLANG_FORMAT=clang-format-18
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
            echo -e "${RED}Format errors found. Run: clang-format-18 -i <files>${NC}"
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

# ビルドディレクトリがなければ設定
if [ ! -f "build/Makefile" ] && [ ! -f "build/build.ninja" ]; then
    echo "Configuring CMake..."
    
    # コンパイラ検出
    if command -v g++-14 &> /dev/null; then
        CXX=g++-14
        CC=gcc-14
    else
        CXX=g++
        CC=gcc
    fi
    
    # Ninja優先
    if command -v ninja &> /dev/null; then
        GENERATOR="-G Ninja"
    else
        GENERATOR=""
    fi
    
    cmake -S . -B build $GENERATOR \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=$CC \
        -DCMAKE_CXX_COMPILER=$CXX \
        -DSAPPP_BUILD_TESTS=ON \
        -DSAPPP_BUILD_CLANG_FRONTEND=OFF \
        -DSAPPP_WERROR=ON \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        > /dev/null 2>&1
fi

# ビルド（並列、出力抑制）
if cmake --build build --parallel 2>&1 | grep -E "(error:|warning:)" | head -20; then
    # エラーがあれば失敗
    if cmake --build build --parallel 2>&1 | grep -q "error:"; then
        echo -e "${RED}✗ Build failed${NC}"
        exit 1
    fi
fi
echo -e "${GREEN}✓ Build passed${NC}"

# ===========================================================================
# 4. テスト実行（高速モード）
# ===========================================================================
echo -e "\n${BLUE}▶ Quick Tests${NC}"

# 失敗したテストのみ表示
if ! ctest --test-dir build --output-on-failure -j$(nproc) 2>&1 | tail -5; then
    echo -e "${RED}✗ Tests failed${NC}"
    exit 1
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

echo -e "${BLUE}フルチェック: ./scripts/pre-commit-check.sh${NC}"
echo -e "${BLUE}Docker CI:    ./scripts/docker-ci.sh${NC}"
