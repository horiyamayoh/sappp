#!/bin/bash
# common.sh - 品質ゲートスクリプト共通ライブラリ
#
# このファイルは品質ゲート関連スクリプトで共通使用される関数・定数を提供します。
# 単一ソース化により、Makefile / pre-commit-check.sh / quick-check.sh / CI 間の
# 不整合を構造的に防止します。
#
# 使い方:
#   source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"
#   または
#   source "$SCRIPT_DIR/lib/common.sh"

# ===========================================================================
# 色定義（端末が対応している場合）
# ===========================================================================
export RED='\033[0;31m'
export GREEN='\033[0;32m'
export YELLOW='\033[1;33m'
export BLUE='\033[0;34m'
export NC='\033[0m'  # No Color

# ===========================================================================
# プロジェクト設定（単一ソース）
# ===========================================================================

# C++ ソースファイルの対象ディレクトリ
export SAPPP_SOURCE_DIRS="libs tools include"

# テストディレクトリ（clang-format は対象、clang-tidy は対象外）
export SAPPP_TEST_DIR="tests"

# clang-tidy 除外パターン（正規表現）
export SAPPP_TIDY_EXCLUDE_PATTERNS="libs/frontend_clang/"

# C++ ファイル拡張子
export SAPPP_CPP_EXTENSIONS="cpp|cc|cxx"
export SAPPP_HEADER_EXTENSIONS="hpp|h|hxx"
export SAPPP_ALL_CPP_EXTENSIONS="cpp|cc|cxx|hpp|h|hxx"

# ===========================================================================
# コンパイラ警告検出パターン（単一ソース）
# ===========================================================================

# GCC/Clang の警告パターン
export SAPPP_WARNING_PATTERN=':[0-9]+:[0-9]+: warning:'

# ===========================================================================
# ツール検出関数
# ===========================================================================

# clang-format を検出
# 戻り値: コマンド名（見つからない場合は空文字）
detect_clang_format() {
    if command -v clang-format-19 &> /dev/null; then
        echo "clang-format-19"
    elif command -v clang-format &> /dev/null; then
        echo "clang-format"
    else
        echo ""
    fi
}

# clang-tidy を検出
detect_clang_tidy() {
    if command -v clang-tidy-19 &> /dev/null; then
        echo "clang-tidy-19"
    elif command -v clang-tidy &> /dev/null; then
        echo "clang-tidy"
    else
        echo ""
    fi
}

# GCC 14 を検出
detect_gcc14() {
    if command -v g++-14 &> /dev/null; then
        echo "g++-14"
    elif command -v g++ &> /dev/null; then
        echo "g++"
    else
        echo ""
    fi
}

# GCC 14 C compiler を検出
detect_gcc14_c() {
    if command -v gcc-14 &> /dev/null; then
        echo "gcc-14"
    elif command -v gcc &> /dev/null; then
        echo "gcc"
    else
        echo ""
    fi
}

# Clang 19 を検出
detect_clang19() {
    if command -v clang++-19 &> /dev/null; then
        echo "clang++-19"
    elif command -v clang++ &> /dev/null; then
        echo "clang++"
    else
        echo ""
    fi
}

# Clang 19 C compiler を検出
detect_clang19_c() {
    if command -v clang-19 &> /dev/null; then
        echo "clang-19"
    elif command -v clang &> /dev/null; then
        echo "clang"
    else
        echo ""
    fi
}

# ajv-cli を検出
detect_ajv() {
    if command -v ajv &> /dev/null; then
        echo "ajv"
    else
        echo ""
    fi
}

# Ninja を検出
detect_ninja() {
    if command -v ninja &> /dev/null; then
        echo "ninja"
    else
        echo ""
    fi
}

# ripgrep を検出（高速 grep）
detect_ripgrep() {
    if command -v rg &> /dev/null; then
        echo "rg"
    else
        echo ""
    fi
}

# ccache を検出
detect_ccache() {
    if command -v ccache &> /dev/null; then
        echo "ccache"
    else
        echo ""
    fi
}

# ===========================================================================
# 並列度取得
# ===========================================================================

get_build_jobs() {
    if [ -n "${SAPPP_BUILD_JOBS:-}" ]; then
        echo "$SAPPP_BUILD_JOBS"
    elif command -v nproc &> /dev/null; then
        nproc
    else
        echo "1"
    fi
}

# ===========================================================================
# CMake ジェネレータ取得
# ===========================================================================

get_cmake_generator() {
    if [ -n "$(detect_ninja)" ]; then
        echo "-G Ninja"
    else
        echo ""
    fi
}

# ===========================================================================
# ccache オプション取得
# ===========================================================================

get_ccache_cmake_opts() {
    local use_ccache="${SAPPP_USE_CCACHE:-1}"
    if [ "$use_ccache" = "1" ] && [ -n "$(detect_ccache)" ]; then
        echo "-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
    else
        echo ""
    fi
}

# ===========================================================================
# 警告検出関数
# ===========================================================================

# ログファイルにコンパイラ警告があるか確認
# 引数: $1 = ログファイルパス
# 戻り値: 0 = 警告あり, 1 = 警告なし
has_compiler_warnings() {
    local log_file="$1"
    if [ -n "$(detect_ripgrep)" ]; then
        rg -n "$SAPPP_WARNING_PATTERN" "$log_file" > /dev/null 2>&1
    else
        grep -nE "$SAPPP_WARNING_PATTERN" "$log_file" > /dev/null 2>&1
    fi
}

# コンパイラ警告を表示（最大20件）
# 引数: $1 = ログファイルパス
print_compiler_warnings() {
    local log_file="$1"
    local max_lines="${2:-20}"
    if [ -n "$(detect_ripgrep)" ]; then
        rg -n "$SAPPP_WARNING_PATTERN" "$log_file" | head -"$max_lines"
    else
        grep -nE "$SAPPP_WARNING_PATTERN" "$log_file" | head -"$max_lines"
    fi
}

# ===========================================================================
# Git 変更ファイル収集
# ===========================================================================

# 変更された C++ ファイルを収集
# 引数: $1 = 対象ディレクトリ（スペース区切り、デフォルト: libs tools tests include）
# 出力: 変更ファイル一覧（改行区切り）
collect_changed_cpp_files() {
    local target_dirs="${1:-$SAPPP_SOURCE_DIRS $SAPPP_TEST_DIR}"
    
    if ! git rev-parse --git-dir > /dev/null 2>&1; then
        return 0
    fi

    local files=""
    if git rev-parse --verify HEAD > /dev/null 2>&1; then
        files=$(
            {
                git diff --name-only --diff-filter=ACMR HEAD -- $target_dirs
                git diff --name-only --cached --diff-filter=ACMR HEAD -- $target_dirs
                git ls-files --others --exclude-standard -- $target_dirs
            } | sort -u
        )
    else
        # shellcheck disable=SC2086
        files=$(git ls-files -- $target_dirs | sort -u)
    fi

    if [ -z "$files" ]; then
        return 0
    fi

    # C++ ファイルのみフィルタ
    local cpp_pattern="\\.($SAPPP_ALL_CPP_EXTENSIONS)$"
    if [ -n "$(detect_ripgrep)" ]; then
        printf '%s\n' "$files" | rg -e "$cpp_pattern" || true
    else
        printf '%s\n' "$files" | grep -E "$cpp_pattern" || true
    fi
}

# ===========================================================================
# ファイル収集ヘルパー
# ===========================================================================

# 指定ディレクトリから C++ ソースファイルを収集
# 引数: $1 = 対象ディレクトリ（スペース区切り）
#       $2 = 除外パターン（正規表現、オプション）
collect_cpp_source_files() {
    local target_dirs="$1"
    local exclude_pattern="${2:-}"
    
    local files
    # shellcheck disable=SC2086
    files=$(find $target_dirs \( -name "*.cpp" -o -name "*.cc" -o -name "*.cxx" \) -print 2>/dev/null | sort -u)
    
    if [ -n "$exclude_pattern" ] && [ -n "$files" ]; then
        printf '%s\n' "$files" | grep -vE "$exclude_pattern" || true
    else
        printf '%s\n' "$files"
    fi
}

# 指定ディレクトリから全 C++ ファイル（ヘッダ含む）を収集
collect_all_cpp_files() {
    local target_dirs="$1"
    local exclude_pattern="${2:-}"
    
    local files
    # shellcheck disable=SC2086
    files=$(find $target_dirs \( -name "*.cpp" -o -name "*.cc" -o -name "*.cxx" -o -name "*.hpp" -o -name "*.h" -o -name "*.hxx" \) -print 2>/dev/null | sort -u)
    
    if [ -n "$exclude_pattern" ] && [ -n "$files" ]; then
        printf '%s\n' "$files" | grep -vE "$exclude_pattern" || true
    else
        printf '%s\n' "$files"
    fi
}

# ===========================================================================
# エラーハンドリング
# ===========================================================================

# ツールが見つからない場合のエラー処理
# 引数: $1 = ツール名
#       $2 = strict モードかどうか (true/false)
#       $3 = インストールコマンド（オプション）
handle_tool_not_found() {
    local tool_name="$1"
    local strict_mode="${2:-false}"
    local install_cmd="${3:-}"
    
    if [ "$strict_mode" = true ]; then
        echo -e "${RED}Error: $tool_name not found${NC}" >&2
        if [ -n "$install_cmd" ]; then
            echo "Install with: $install_cmd" >&2
        fi
        return 1
    else
        echo -e "${YELLOW}Warning: $tool_name not found, skipping${NC}" >&2
        return 2  # スキップを示す特別なコード
    fi
}

# ===========================================================================
# 初期化確認
# ===========================================================================

# このファイルが source されたことを示すフラグ
export SAPPP_COMMON_LOADED=1
