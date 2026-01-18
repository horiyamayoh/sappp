#!/bin/bash
# pre-commit-check.sh - CI相当のローカルチェックを実行
#
# 使い方:
#   ./scripts/pre-commit-check.sh         # 全チェック
#   ./scripts/pre-commit-check.sh --quick # 最小限のチェック（ビルド+テスト+フォーマット）
#   ./scripts/pre-commit-check.sh --help  # ヘルプ表示

set -e

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

for arg in "$@"; do
    case $arg in
        --quick)
            QUICK_MODE=true
            SKIP_CLANG_BUILD=true
            SKIP_TIDY=true
            SKIP_SCHEMA=true
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
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --quick        最小限のチェック（GCCビルド+テスト+フォーマット）"
            echo "  --skip-clang   Clang 18 ビルドをスキップ"
            echo "  --skip-tidy    clang-tidy をスキップ"
            echo "  --skip-schema  スキーマ検証をスキップ"
            echo "  --help, -h     このヘルプを表示"
            exit 0
            ;;
    esac
done

# 結果追跡
PASSED=()
FAILED=()

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
    if command -v clang-format-18 &> /dev/null; then
        CLANG_FORMAT=clang-format-18
    elif command -v clang-format &> /dev/null; then
        CLANG_FORMAT=clang-format
    else
        echo "Warning: clang-format not found, skipping"
        exit 0
    fi
    find libs tools tests include \
        \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) \
        -print0 2>/dev/null | xargs -0 $CLANG_FORMAT --dry-run --Werror
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
    
    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=$C_COMPILER \
        -DCMAKE_CXX_COMPILER=$CXX_COMPILER \
        -DSAPPP_BUILD_TESTS=ON \
        -DSAPPP_BUILD_CLANG_FRONTEND=OFF \
        -DSAPPP_WERROR=ON \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        2>&1 | tail -20
    
    cmake --build build --parallel 2>&1
'

# ===========================================================================
# 3. テスト実行
# ===========================================================================
run_check "All Tests" '
    ctest --test-dir build --output-on-failure
'

# ===========================================================================
# 4. 決定性テスト
# ===========================================================================
run_check "Determinism Tests" '
    ctest --test-dir build -R determinism --output-on-failure
'

# ===========================================================================
# 5. Clang 18 ビルド（オプション）
#    NOTE: libc++ 18 は std::views::enumerate 未実装のため libstdc++ を使用
# ===========================================================================
if [ "$SKIP_CLANG_BUILD" = false ]; then
    run_check "Build (Clang 18)" '
        if ! command -v clang++-18 &> /dev/null; then
            echo "Warning: clang++-18 not found, skipping"
            exit 0
        fi
        
        rm -rf build-clang
        cmake -S . -B build-clang \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_COMPILER=clang-18 \
            -DCMAKE_CXX_COMPILER=clang++-18 \
            -DSAPPP_BUILD_TESTS=ON \
            -DSAPPP_BUILD_CLANG_FRONTEND=OFF \
            -DSAPPP_WERROR=ON \
            2>&1 | tail -20
        
        cmake --build build-clang --parallel 2>&1
        ctest --test-dir build-clang --output-on-failure
    '
fi

# ===========================================================================
# 6. clang-tidy（オプション）
# ===========================================================================
if [ "$SKIP_TIDY" = false ]; then
    run_check "Static Analysis (clang-tidy)" '
        if command -v clang-tidy-18 &> /dev/null; then
            CLANG_TIDY=clang-tidy-18
        elif command -v clang-tidy &> /dev/null; then
            CLANG_TIDY=clang-tidy
        else
            echo "Warning: clang-tidy not found, skipping"
            exit 0
        fi
        
        # 変更されたファイルのみチェック（高速化）
        CHANGED_FILES=$(git diff --name-only HEAD~1 -- "libs/*.cpp" 2>/dev/null || find libs -name "*.cpp")
        if [ -z "$CHANGED_FILES" ]; then
            echo "No C++ files to check"
            exit 0
        fi
        
        echo "Checking files:"
        echo "$CHANGED_FILES"
        echo "$CHANGED_FILES" | xargs -P$(nproc) -I{} $CLANG_TIDY -p build --warnings-as-errors="*" {}
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
                for schema in schemas/*.schema.json; do
                    echo "Validating: $schema"
                    python3 -c "import json; json.load(open(\"$schema\"))" || exit 1
                done
                exit 0
            else
                echo "Warning: No JSON validator found, skipping"
                exit 0
            fi
        fi
        
        # NOTE: ajv-formats is required for date-time format validation
        for schema in schemas/*.schema.json; do
            echo "Validating: $schema"
            ajv compile -s "$schema" --spec=draft2020 -c ajv-formats || exit 1
        done
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
    echo -e "\n${GREEN}━━━ 全チェック通過！コミットしてOK ━━━${NC}"
    exit 0
fi
