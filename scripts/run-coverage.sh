#!/bin/bash
# run-coverage.sh - コードカバレッジレポートを生成
#
# 使い方:
#   ./scripts/run-coverage.sh              # カバレッジビルド＆テスト＆レポート生成
#   ./scripts/run-coverage.sh --html       # HTMLレポートも生成
#   ./scripts/run-coverage.sh --open       # レポート生成後にブラウザで開く
#   ./scripts/run-coverage.sh --clean      # カバレッジデータをクリア
#
# 必要なツール:
#   GCC: gcov, lcov, genhtml
#   Clang: llvm-profdata, llvm-cov

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

# 共通ライブラリ読み込み
source "$SCRIPT_DIR/lib/common.sh"

cd "$PROJECT_ROOT"

# ===========================================================================
# オプション解析
# ===========================================================================
BUILD_DIR="${SAPPP_BUILD_DIR:-build-coverage}"
GENERATE_HTML=false
OPEN_REPORT=false
CLEAN_ONLY=false
COMPILER="${SAPPP_COVERAGE_COMPILER:-gcc}"

for arg in "$@"; do
    case $arg in
        --html)
            GENERATE_HTML=true
            ;;
        --open)
            GENERATE_HTML=true
            OPEN_REPORT=true
            ;;
        --clean)
            CLEAN_ONLY=true
            ;;
        --gcc)
            COMPILER="gcc"
            ;;
        --clang)
            COMPILER="clang"
            ;;
        --build-dir=*)
            BUILD_DIR="${arg#*=}"
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "コードカバレッジレポートを生成します。"
            echo ""
            echo "Options:"
            echo "  --html        HTMLレポートを生成"
            echo "  --open        レポート生成後にブラウザで開く"
            echo "  --clean       カバレッジデータをクリアして終了"
            echo "  --gcc         GCC (gcov) を使用（デフォルト）"
            echo "  --clang       Clang (llvm-cov) を使用"
            echo "  --build-dir=  ビルドディレクトリを指定"
            echo "  --help, -h    このヘルプを表示"
            echo ""
            echo "Examples:"
            echo "  $0                    # 基本のカバレッジレポート"
            echo "  $0 --html --open      # HTMLレポートをブラウザで開く"
            exit 0
            ;;
    esac
done

COVERAGE_DIR="$BUILD_DIR/coverage"
REPORT_DIR="$COVERAGE_DIR/report"

# ===========================================================================
# クリーン処理
# ===========================================================================
if [ "$CLEAN_ONLY" = true ]; then
    echo -e "${BLUE}▶ カバレッジデータをクリア中...${NC}"
    rm -rf "$COVERAGE_DIR"
    find "$BUILD_DIR" -name "*.gcda" -delete 2>/dev/null || true
    find "$BUILD_DIR" -name "*.gcno" -delete 2>/dev/null || true
    rm -f "$BUILD_DIR/default.profraw" "$BUILD_DIR/default.profdata" 2>/dev/null || true
    echo -e "${GREEN}✓ クリア完了${NC}"
    exit 0
fi

# ===========================================================================
# ツール検出
# ===========================================================================
echo -e "${YELLOW}━━━ SAP++ Coverage Report ━━━${NC}"
echo ""

if [ "$COMPILER" = "gcc" ]; then
    if ! command -v gcov &> /dev/null; then
        echo -e "${RED}Error: gcov が見つかりません${NC}"
        exit 1
    fi
    if [ "$GENERATE_HTML" = true ]; then
        if ! command -v lcov &> /dev/null; then
            echo -e "${RED}Error: lcov が見つかりません（sudo apt install lcov）${NC}"
            exit 1
        fi
        if ! command -v genhtml &> /dev/null; then
            echo -e "${RED}Error: genhtml が見つかりません（lcov パッケージに含まれます）${NC}"
            exit 1
        fi
    fi
    CC="$(detect_gcc14 | sed 's/g++/gcc/')"
    CXX="$(detect_gcc14)"
else
    if ! command -v llvm-profdata &> /dev/null && ! command -v llvm-profdata-19 &> /dev/null; then
        echo -e "${RED}Error: llvm-profdata が見つかりません${NC}"
        exit 1
    fi
    if ! command -v llvm-cov &> /dev/null && ! command -v llvm-cov-19 &> /dev/null; then
        echo -e "${RED}Error: llvm-cov が見つかりません${NC}"
        exit 1
    fi
    # Clang コンパイラの検出
    if command -v clang-19 &> /dev/null; then
        CC="clang-19"
        CXX="clang++-19"
    elif command -v clang &> /dev/null; then
        CC="clang"
        CXX="clang++"
    else
        echo -e "${RED}Error: clang が見つかりません${NC}"
        exit 1
    fi
fi

echo -e "${BLUE}Compiler: $COMPILER ($CXX)${NC}"
echo ""

# ===========================================================================
# カバレッジビルド
# ===========================================================================
echo -e "${BLUE}▶ カバレッジビルド中...${NC}"

mkdir -p "$BUILD_DIR"

GENERATOR="$(get_cmake_generator)"

cmake -S . -B "$BUILD_DIR" $GENERATOR \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DSAPPP_BUILD_TESTS=ON \
    -DSAPPP_BUILD_CLANG_FRONTEND=OFF \
    -DSAPPP_COVERAGE=ON

cmake --build "$BUILD_DIR" --parallel "$(get_build_jobs)"

echo -e "${GREEN}✓ ビルド完了${NC}"

# ===========================================================================
# テスト実行
# ===========================================================================
echo -e "\n${BLUE}▶ テスト実行中...${NC}"

if [ "$COMPILER" = "clang" ]; then
    # Clang: プロファイルデータ出力先を設定
    export LLVM_PROFILE_FILE="$BUILD_DIR/coverage-%p.profraw"
fi

ctest --test-dir "$BUILD_DIR" --output-on-failure -j"$(get_build_jobs)"

echo -e "${GREEN}✓ テスト完了${NC}"

# ===========================================================================
# カバレッジレポート生成
# ===========================================================================
echo -e "\n${BLUE}▶ カバレッジレポート生成中...${NC}"

mkdir -p "$COVERAGE_DIR"
mkdir -p "$REPORT_DIR"

if [ "$COMPILER" = "gcc" ]; then
    # GCC: lcov を使用
    LCOV_OPTS=(
        --capture
        --directory "$BUILD_DIR"
        --output-file "$COVERAGE_DIR/coverage.info"
        --ignore-errors mismatch
    )
    
    # gcov バージョン指定
    if command -v gcov-14 &> /dev/null; then
        LCOV_OPTS+=(--gcov-tool gcov-14)
    fi
    
    lcov "${LCOV_OPTS[@]}"
    
    # 外部ライブラリを除外
    lcov --remove "$COVERAGE_DIR/coverage.info" \
        '/usr/*' \
        '*/build/_deps/*' \
        '*/tests/*' \
        --output-file "$COVERAGE_DIR/coverage-filtered.info" \
        --ignore-errors unused
    
    # サマリー出力
    echo ""
    lcov --summary "$COVERAGE_DIR/coverage-filtered.info"
    
    if [ "$GENERATE_HTML" = true ]; then
        echo -e "\n${BLUE}▶ HTMLレポート生成中...${NC}"
        genhtml "$COVERAGE_DIR/coverage-filtered.info" \
            --output-directory "$REPORT_DIR" \
            --title "SAP++ Coverage Report" \
            --legend \
            --show-details
        echo -e "${GREEN}✓ HTMLレポート: $REPORT_DIR/index.html${NC}"
    fi
    
else
    # Clang: llvm-cov を使用
    LLVM_PROFDATA="llvm-profdata"
    LLVM_COV="llvm-cov"
    if command -v llvm-profdata-19 &> /dev/null; then
        LLVM_PROFDATA="llvm-profdata-19"
        LLVM_COV="llvm-cov-19"
    fi
    
    # プロファイルデータをマージ
    "$LLVM_PROFDATA" merge -sparse "$BUILD_DIR"/coverage-*.profraw \
        -o "$COVERAGE_DIR/coverage.profdata"
    
    # テストバイナリを収集
    TEST_BINARIES=()
    for bin in "$BUILD_DIR/bin/test_"*; do
        if [ -x "$bin" ]; then
            TEST_BINARIES+=("-object" "$bin")
        fi
    done
    
    # サマリー出力
    "$LLVM_COV" report \
        "${TEST_BINARIES[@]}" \
        -instr-profile="$COVERAGE_DIR/coverage.profdata" \
        -ignore-filename-regex='(build/_deps|/usr|tests/)' \
        > "$COVERAGE_DIR/summary.txt"
    
    cat "$COVERAGE_DIR/summary.txt"
    
    if [ "$GENERATE_HTML" = true ]; then
        echo -e "\n${BLUE}▶ HTMLレポート生成中...${NC}"
        "$LLVM_COV" show \
            "${TEST_BINARIES[@]}" \
            -instr-profile="$COVERAGE_DIR/coverage.profdata" \
            -format=html \
            -output-dir="$REPORT_DIR" \
            -ignore-filename-regex='(build/_deps|/usr|tests/)' \
            -show-line-counts-or-regions \
            -show-instantiations
        echo -e "${GREEN}✓ HTMLレポート: $REPORT_DIR/index.html${NC}"
    fi
fi

# ===========================================================================
# ブラウザで開く
# ===========================================================================
if [ "$OPEN_REPORT" = true ] && [ -f "$REPORT_DIR/index.html" ]; then
    echo -e "\n${BLUE}▶ レポートをブラウザで開いています...${NC}"
    if command -v xdg-open &> /dev/null; then
        xdg-open "$REPORT_DIR/index.html" &
    elif command -v open &> /dev/null; then
        open "$REPORT_DIR/index.html"
    else
        echo -e "${YELLOW}ブラウザで開けません。手動で開いてください: $REPORT_DIR/index.html${NC}"
    fi
fi

echo -e "\n${GREEN}━━━ カバレッジレポート完了 ━━━${NC}"
