#!/bin/bash
# check-env.sh - 開発環境の状態をチェック
#
# 使い方:
#   ./scripts/check-env.sh           # 環境チェック
#   ./scripts/check-env.sh --fix     # 問題があれば修正を試みる
#
# チェック項目:
#   - Git hooks のインストール状態
#   - Docker の利用可否
#   - 必要なツールの存在

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
FIX_MODE=false
for arg in "$@"; do
    case $arg in
        --fix)
            FIX_MODE=true
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "開発環境の状態をチェックします。"
            echo ""
            echo "Options:"
            echo "  --fix       問題があれば修正を試みる"
            echo "  --help, -h  このヘルプを表示"
            exit 0
            ;;
    esac
done

echo -e "${YELLOW}"
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║            SAP++ Environment Check                            ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

WARNINGS=0
ERRORS=0

# ===========================================================================
# 1. Git hooks チェック
# ===========================================================================
echo -e "${BLUE}▶ Git Hooks${NC}"

if [ -f ".git/hooks/pre-commit" ] && grep -q "SAP++" ".git/hooks/pre-commit" 2>/dev/null; then
    echo -e "  ${GREEN}✓ pre-commit hook: インストール済み${NC}"
else
    echo -e "  ${RED}✗ pre-commit hook: 未インストール${NC}"
    WARNINGS=$((WARNINGS + 1))
    if [ "$FIX_MODE" = true ]; then
        echo -e "  ${BLUE}→ インストール中...${NC}"
        ./scripts/install-hooks.sh
    else
        echo -e "  ${YELLOW}  修正: ./scripts/install-hooks.sh${NC}"
    fi
fi

if [ -f ".git/hooks/pre-push" ] && grep -q "SAP++" ".git/hooks/pre-push" 2>/dev/null; then
    echo -e "  ${GREEN}✓ pre-push hook: インストール済み${NC}"
else
    echo -e "  ${YELLOW}⚠ pre-push hook: 未インストール（推奨）${NC}"
    if [ "$FIX_MODE" = true ]; then
        echo -e "  ${BLUE}→ インストール中...${NC}"
        ./scripts/install-hooks.sh
    else
        echo -e "  ${YELLOW}  修正: ./scripts/install-hooks.sh${NC}"
    fi
fi

# ===========================================================================
# 2. Docker チェック
# ===========================================================================
echo -e "\n${BLUE}▶ Docker${NC}"

if command -v docker &> /dev/null; then
    echo -e "  ${GREEN}✓ Docker: インストール済み${NC}"
    
    if docker info &> /dev/null; then
        echo -e "  ${GREEN}✓ Docker daemon: 実行中${NC}"
        
        if docker image inspect sappp-ci &> /dev/null; then
            echo -e "  ${GREEN}✓ sappp-ci イメージ: ビルド済み${NC}"
        else
            echo -e "  ${YELLOW}⚠ sappp-ci イメージ: 未ビルド${NC}"
            if [ "$FIX_MODE" = true ]; then
                echo -e "  ${BLUE}→ ビルド中...${NC}"
                docker build -t sappp-ci docker/
            else
                echo -e "  ${YELLOW}  修正: docker build -t sappp-ci docker/${NC}"
            fi
        fi
    else
        echo -e "  ${RED}✗ Docker daemon: 停止中${NC}"
        WARNINGS=$((WARNINGS + 1))
        echo -e "  ${YELLOW}  修正: sudo service docker start${NC}"
    fi
else
    echo -e "  ${YELLOW}⚠ Docker: 未インストール${NC}"
    WARNINGS=$((WARNINGS + 1))
    echo -e "  ${YELLOW}  推奨: https://docs.docker.com/get-docker/${NC}"
    echo -e "  ${YELLOW}  WSL2: curl -fsSL https://get.docker.com | sudo sh${NC}"
fi

# ===========================================================================
# 3. コンパイラチェック
# ===========================================================================
echo -e "\n${BLUE}▶ コンパイラ${NC}"

if command -v g++-14 &> /dev/null; then
    VERSION=$(g++-14 --version | head -1)
    echo -e "  ${GREEN}✓ GCC 14: $VERSION${NC}"
else
    echo -e "  ${YELLOW}⚠ GCC 14: 未インストール（Docker使用時は不要）${NC}"
fi

if command -v clang++-18 &> /dev/null; then
    VERSION=$(clang++-18 --version | head -1)
    echo -e "  ${GREEN}✓ Clang 18: $VERSION${NC}"
else
    echo -e "  ${YELLOW}⚠ Clang 18: 未インストール（Docker使用時は不要）${NC}"
fi

# ===========================================================================
# 4. 開発ツールチェック
# ===========================================================================
echo -e "\n${BLUE}▶ 開発ツール${NC}"

check_tool() {
    local name="$1"
    local cmd="$2"
    local optional="$3"
    
    if command -v "$cmd" &> /dev/null; then
        echo -e "  ${GREEN}✓ $name: $(command -v $cmd)${NC}"
    elif [ "$optional" = "optional" ]; then
        echo -e "  ${YELLOW}⚠ $name: 未インストール（Docker使用時は不要）${NC}"
    else
        echo -e "  ${RED}✗ $name: 未インストール${NC}"
        ERRORS=$((ERRORS + 1))
    fi
}

check_tool "CMake" "cmake" "optional"
check_tool "Ninja" "ninja" "optional"
check_tool "clang-format" "clang-format-18" "optional"
check_tool "clang-tidy" "clang-tidy-18" "optional"

# ===========================================================================
# 5. ビルドディレクトリチェック
# ===========================================================================
echo -e "\n${BLUE}▶ ビルド状態${NC}"

if [ -f "build/compile_commands.json" ]; then
    echo -e "  ${GREEN}✓ ビルドディレクトリ: 設定済み${NC}"
else
    echo -e "  ${YELLOW}⚠ ビルドディレクトリ: 未設定${NC}"
    echo -e "  ${YELLOW}  修正: make build または ./scripts/docker-ci.sh${NC}"
fi

# ===========================================================================
# サマリー
# ===========================================================================
echo -e "\n${YELLOW}"
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║                        Summary                                 ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

if [ $ERRORS -gt 0 ]; then
    echo -e "${RED}エラー: $ERRORS 件${NC}"
fi

if [ $WARNINGS -gt 0 ]; then
    echo -e "${YELLOW}警告: $WARNINGS 件${NC}"
fi

if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo -e "${GREEN}✓ 環境は正常です${NC}"
fi

echo ""
echo -e "${BLUE}推奨ワークフロー:${NC}"
echo "  1. make install-hooks    # Git hooks インストール"
echo "  2. make docker-ci        # Docker で CI チェック"
echo "  3. git commit / push     # 自動でチェックが実行される"

if [ $ERRORS -gt 0 ]; then
    exit 1
fi
exit 0
