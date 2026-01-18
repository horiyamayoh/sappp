#!/bin/bash
# install-hooks.sh - Git hooks をインストール
#
# 使い方:
#   ./scripts/install-hooks.sh           # pre-commit hook をインストール
#   ./scripts/install-hooks.sh --remove  # hooks を削除
#
# インストールされるフック:
#   pre-commit: フルチェック（Docker優先）
#   pre-push:   フルチェック済みスタンプ確認（高速）

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

# Git リポジトリチェック
if [ ! -d ".git" ]; then
    echo -e "${RED}Error: .git ディレクトリが見つかりません${NC}"
    exit 1
fi

HOOKS_DIR=".git/hooks"

# オプション解析
for arg in "$@"; do
    case $arg in
        --remove)
            echo -e "${BLUE}▶ Git hooks を削除中...${NC}"
            rm -f "$HOOKS_DIR/pre-commit"
            rm -f "$HOOKS_DIR/pre-push"
            echo -e "${GREEN}✓ pre-commit hook を削除しました${NC}"
            echo -e "${GREEN}✓ pre-push hook を削除しました${NC}"
            exit 0
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Git hooks をインストールします。"
            echo ""
            echo "Options:"
            echo "  --remove    hooks を削除"
            echo "  --help, -h  このヘルプを表示"
            exit 0
            ;;
    esac
done

# ===========================================================================
# pre-commit hook のインストール
# ===========================================================================
echo -e "${BLUE}▶ pre-commit hook をインストール中...${NC}"

cat > "$HOOKS_DIR/pre-commit" << 'EOF'
#!/bin/bash
# SAP++ pre-commit hook
# インストール: ./scripts/install-hooks.sh
# 削除: ./scripts/install-hooks.sh --remove

set -euo pipefail

# 色付き出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${YELLOW}━━━ SAP++ Pre-commit Check ━━━${NC}"

# スキップオプション
if [ -n "${SKIP_PRE_COMMIT:-}" ]; then
    echo -e "${YELLOW}⚠ SKIP_PRE_COMMIT が設定されています。チェックをスキップ${NC}"
    exit 0
fi

# スタンプ保存先
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
export SAPPP_CI_STAMP_FILE="${SAPPP_CI_STAMP_FILE:-$REPO_ROOT/.git/sappp/ci-stamp.json}"
cd "$REPO_ROOT"

# 実行モード（full/quick）
PRE_COMMIT_MODE="${SAPPP_PRE_COMMIT_MODE:-full}"

# Docker環境内かどうかを検出
if [ -n "${SAPPP_CI_ENV:-}" ]; then
    # Docker/DevContainer内 - 直接実行
    if [ "$PRE_COMMIT_MODE" = "quick" ]; then
        CHECK_CMD="./scripts/quick-check.sh"
    else
        CHECK_CMD="./scripts/pre-commit-check.sh"
    fi
    if $CHECK_CMD; then
        echo -e "${GREEN}✓ Pre-commit check passed${NC}"
        exit 0
    else
        echo -e "${RED}✗ Pre-commit check failed${NC}"
        echo -e "${YELLOW}修正後に再度コミットしてください${NC}"
        echo -e "${YELLOW}スキップ: SKIP_PRE_COMMIT=1 git commit ...${NC}"
        exit 1
    fi
fi

# ローカル環境 - Dockerが使えるならDockerで実行
if command -v docker &> /dev/null && docker info &> /dev/null; then
    # Dockerイメージがあるか確認
    if docker image inspect sappp-ci &> /dev/null; then
        echo -e "${BLUE}Docker環境で実行中...${NC}"
        if [ "$PRE_COMMIT_MODE" = "quick" ]; then
            DOCKER_CMD="./scripts/docker-ci.sh --quick"
        else
            DOCKER_CMD="./scripts/docker-ci.sh"
        fi
        if $DOCKER_CMD; then
            echo -e "${GREEN}✓ Pre-commit check passed${NC}"
            exit 0
        else
            echo -e "${RED}✗ Pre-commit check failed${NC}"
            echo -e "${YELLOW}修正後に再度コミットしてください${NC}"
            exit 1
        fi
    fi
fi

# フォールバック: ローカルで実行
if [ "$PRE_COMMIT_MODE" = "quick" ]; then
    CHECK_CMD="./scripts/quick-check.sh"
else
    CHECK_CMD="./scripts/pre-commit-check.sh"
fi
if $CHECK_CMD; then
    echo -e "${GREEN}✓ Pre-commit check passed${NC}"
    exit 0
else
    echo -e "${RED}✗ Pre-commit check failed${NC}"
    echo -e "${YELLOW}修正後に再度コミットしてください${NC}"
    echo -e "${YELLOW}スキップ: SKIP_PRE_COMMIT=1 git commit ...${NC}"
    exit 1
fi
EOF

chmod +x "$HOOKS_DIR/pre-commit"
echo -e "${GREEN}✓ pre-commit hook をインストールしました${NC}"

# ===========================================================================
# pre-push hook のインストール
# ===========================================================================
echo -e "${BLUE}▶ pre-push hook をインストール中...${NC}"

cat > "$HOOKS_DIR/pre-push" << 'EOF'
#!/bin/bash
# SAP++ pre-push hook
# インストール: ./scripts/install-hooks.sh
# 削除: ./scripts/install-hooks.sh --remove
#
# push前はフルチェック済みスタンプの確認（高速）

set -euo pipefail

# 色付き出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}━━━ SAP++ Pre-push Check ━━━${NC}"

# スキップオプション
if [ -n "${SKIP_PRE_PUSH:-}" ]; then
    echo -e "${YELLOW}⚠ SKIP_PRE_PUSH が設定されています。チェックをスキップ${NC}"
    echo -e "${RED}警告: CI で失敗する可能性があります${NC}"
    exit 0
fi

# スタンプ保存先
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
export SAPPP_CI_STAMP_FILE="${SAPPP_CI_STAMP_FILE:-$REPO_ROOT/.git/sappp/ci-stamp.json}"
cd "$REPO_ROOT"

PRE_PUSH_MODE="${SAPPP_PRE_PUSH_MODE:-stamp}"

case "$PRE_PUSH_MODE" in
    off)
        echo -e "${YELLOW}⚠ pre-push チェックを無効化しています${NC}"
        exit 0
        ;;
    quick)
        ./scripts/quick-check.sh
        ;;
    full)
        ./scripts/pre-commit-check.sh
        ;;
    *)
        ./scripts/pre-push-check.sh
        ;;
esac

echo -e "${GREEN}✓ Pre-push check passed${NC}"
exit 0
EOF

chmod +x "$HOOKS_DIR/pre-push"
echo -e "${GREEN}✓ pre-push hook をインストールしました${NC}"

# ===========================================================================
# サマリー
# ===========================================================================
echo -e "\n${GREEN}━━━ Git Hooks インストール完了 ━━━${NC}"
echo ""
echo "インストールされたフック:"
echo "  • pre-commit: コミット前にフルチェックを実行（Docker優先）"
echo "  • pre-push:   プッシュ前にフルチェック済みスタンプを確認（高速）"
echo ""
echo "使い方:"
echo "  • 通常のコミット: git commit -m '...'"
echo "  • 通常のプッシュ: git push"
echo "  • コミットスキップ: SKIP_PRE_COMMIT=1 git commit -m '...'"
echo "  • プッシュスキップ: SKIP_PRE_PUSH=1 git push"
echo "  • pre-commit 速度優先: SAPPP_PRE_COMMIT_MODE=quick git commit -m '...'"
echo "  • pre-push モード指定: SAPPP_PRE_PUSH_MODE=stamp|quick|full|off git push"
echo "  • フック削除: ./scripts/install-hooks.sh --remove"
