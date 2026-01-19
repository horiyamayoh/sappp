#!/bin/bash
# test-common.sh - common.sh のセルフテスト
#
# 使い方:
#   ./scripts/lib/test-common.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# 色付き出力
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

PASSED=0
FAILED=0

test_case() {
    local name="$1"
    shift
    if "$@"; then
        echo -e "${GREEN}✓ $name${NC}"
        PASSED=$((PASSED + 1))
        return 0
    else
        echo -e "${RED}✗ $name${NC}"
        FAILED=$((FAILED + 1))
        return 1
    fi
}

test_not() {
    ! "$@"
}

test_nonempty() {
    [ -n "$1" ]
}

echo "━━━ common.sh セルフテスト ━━━"
echo ""

# -----------------
# 設定値テスト
# -----------------
echo "▶ 設定値テスト"

test_case "SAPPP_SOURCE_DIRS は空でない" test_nonempty "${SAPPP_SOURCE_DIRS:-}"
test_case "SAPPP_TEST_DIR は空でない" test_nonempty "${SAPPP_TEST_DIR:-}"
test_case "SAPPP_WARNING_PATTERN は空でない" test_nonempty "${SAPPP_WARNING_PATTERN:-}"
test_case "SAPPP_TIDY_EXCLUDE_PATTERNS が定義されている" declare -p SAPPP_TIDY_EXCLUDE_PATTERNS &>/dev/null

echo ""

# -----------------
# ツール検出テスト
# -----------------
echo "▶ ツール検出テスト"

# clang-format 検出
CLANG_FORMAT=$(detect_clang_format || echo "")
if [ -n "$CLANG_FORMAT" ]; then
    test_case "detect_clang_format が実行可能なコマンドを返す" command -v "$CLANG_FORMAT"
else
    echo -e "${RED}⚠ clang-format が見つかりません（スキップ）${NC}"
fi

# clang-tidy 検出
CLANG_TIDY=$(detect_clang_tidy || echo "")
if [ -n "$CLANG_TIDY" ]; then
    test_case "detect_clang_tidy が実行可能なコマンドを返す" command -v "$CLANG_TIDY"
else
    echo -e "${RED}⚠ clang-tidy が見つかりません（スキップ）${NC}"
fi

# gcc14 検出
GCC14=$(detect_gcc14 || echo "")
if [ -n "$GCC14" ]; then
    test_case "detect_gcc14 が実行可能なコマンドを返す" command -v "$GCC14"
else
    echo -e "${RED}⚠ gcc-14 が見つかりません（スキップ）${NC}"
fi

echo ""

# -----------------
# ビルドジョブ数テスト
# -----------------
echo "▶ ビルドジョブ数テスト"

JOBS=$(get_build_jobs)
test_case "get_build_jobs は正の整数を返す" test "$JOBS" -gt 0

JOBS_CUSTOM=$(SAPPP_BUILD_JOBS=4 get_build_jobs)
test_case "SAPPP_BUILD_JOBS=4 で 4 を返す" test "$JOBS_CUSTOM" -eq 4

echo ""

# -----------------
# CMakeジェネレータテスト
# -----------------
echo "▶ CMakeジェネレータテスト"

GENERATOR=$(get_cmake_generator)
test_case "get_cmake_generator は空でない値を返す" test_nonempty "$GENERATOR"

echo ""

# -----------------
# 警告検出テスト
# -----------------
echo "▶ 警告検出テスト"

# テンポラリファイルを使用
TMPDIR_TEST=$(mktemp -d)
trap 'rm -rf "$TMPDIR_TEST"' EXIT

# 警告なしの入力
cat > "$TMPDIR_TEST/no_warn.log" << 'EOF'
Scanning dependencies of target foo
Building CXX object main.cpp.o
Linking CXX executable foo
Built target foo
EOF

test_case "警告なしの場合 has_compiler_warnings は失敗" test_not has_compiler_warnings "$TMPDIR_TEST/no_warn.log"

# 警告ありの入力
cat > "$TMPDIR_TEST/warn.log" << 'EOF'
main.cpp:10:5: warning: unused variable 'x' [-Wunused-variable]
EOF

test_case "警告ありの場合 has_compiler_warnings は成功" has_compiler_warnings "$TMPDIR_TEST/warn.log"

# error: は検出されない（warningのみ）
cat > "$TMPDIR_TEST/error.log" << 'EOF'
main.cpp:10:5: error: expected ';' after expression
EOF

test_case "エラー(error:)は警告パターンに含まれない" test_not has_compiler_warnings "$TMPDIR_TEST/error.log"

echo ""

# -----------------
# 結果サマリー
# -----------------
echo "━━━ サマリー ━━━"
echo "Passed: $PASSED"
echo "Failed: $FAILED"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
exit 0
