#!/bin/bash
# install-hooks.sh - Git hooks をインストール
#
# 使い方:
#   ./scripts/install-hooks.sh           # pre-commit hook をインストール
#   ./scripts/install-hooks.sh --remove  # hooks を削除
#
# インストールされるフック:
#   pre-commit: quick-check.sh を実行（30秒以内の高速チェック）

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

# 色付き出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}━━━ SAP++ Pre-commit Check ━━━${NC}"

# スキップオプション
if [ -n "$SKIP_PRE_COMMIT" ]; then
    echo -e "${YELLOW}⚠ SKIP_PRE_COMMIT が設定されています。チェックをスキップ${NC}"
    exit 0
fi

# Docker環境内かどうかを検出
if [ -n "$SAPPP_CI_ENV" ]; then
    # Docker/DevContainer内 - quick-check.sh を直接実行
    if ./scripts/quick-check.sh; then
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
        if ./scripts/docker-ci.sh --quick; then
            echo -e "${GREEN}✓ Pre-commit check passed${NC}"
            exit 0
        else
            echo -e "${RED}✗ Pre-commit check failed${NC}"
            echo -e "${YELLOW}修正後に再度コミットしてください${NC}"
            exit 1
        fi
    fi
fi

# フォールバック: ローカルで quick-check.sh を実行
if ./scripts/quick-check.sh; then
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
# push前にフルCIチェックを実行します（Docker推奨）

# 色付き出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${YELLOW}━━━ SAP++ Pre-push Check ━━━${NC}"

# スキップオプション
if [ -n "$SKIP_PRE_PUSH" ]; then
    echo -e "${YELLOW}⚠ SKIP_PRE_PUSH が設定されています。チェックをスキップ${NC}"
    echo -e "${RED}警告: CI で失敗する可能性があります${NC}"
    exit 0
fi

# Docker環境内かどうかを検出
if [ -n "$SAPPP_CI_ENV" ]; then
    # Docker/DevContainer内 - pre-commit-check.sh を直接実行
    echo -e "${BLUE}フルCIチェックを実行中...${NC}"
    if ./scripts/pre-commit-check.sh; then
        echo -e "${GREEN}✓ Pre-push check passed${NC}"
        exit 0
    else
        echo -e "${RED}✗ Pre-push check failed${NC}"
        echo -e "${YELLOW}修正後に再度プッシュしてください${NC}"
        echo -e "${YELLOW}スキップ: SKIP_PRE_PUSH=1 git push ...${NC}"
        exit 1
    fi
fi

# ローカル環境 - Dockerが使えるならDockerで実行（フルチェック）
if command -v docker &> /dev/null && docker info &> /dev/null; then
    if docker image inspect sappp-ci &> /dev/null; then
        echo -e "${BLUE}Docker環境でフルCIチェックを実行中...${NC}"
        echo -e "${YELLOW}（これには数分かかる場合があります）${NC}"
        if ./scripts/docker-ci.sh; then
            echo -e "${GREEN}✓ Pre-push check passed${NC}"
            exit 0
        else
            echo -e "${RED}✗ Pre-push check failed${NC}"
            echo -e "${YELLOW}修正後に再度プッシュしてください${NC}"
            echo -e "${YELLOW}スキップ: SKIP_PRE_PUSH=1 git push ...${NC}"
            exit 1
        fi
    else
        echo -e "${YELLOW}⚠ Docker イメージがありません${NC}"
        echo -e "${YELLOW}  ビルド: docker build -t sappp-ci docker/${NC}"
    fi
else
    echo -e "${YELLOW}⚠ Docker が利用できません${NC}"
fi

# フォールバック: ローカルで pre-commit-check.sh を実行
echo -e "${YELLOW}⚠ ローカル環境でチェックを実行します（CI環境と異なる可能性があります）${NC}"
if ./scripts/pre-commit-check.sh; then
    echo -e "${GREEN}✓ Pre-push check passed${NC}"
    echo -e "${YELLOW}⚠ Docker CI での確認を推奨: ./scripts/docker-ci.sh${NC}"
    exit 0
else
    echo -e "${RED}✗ Pre-push check failed${NC}"
    echo -e "${YELLOW}修正後に再度プッシュしてください${NC}"
    echo -e "${YELLOW}スキップ: SKIP_PRE_PUSH=1 git push ...${NC}"
    exit 1
fi
EOF

chmod +x "$HOOKS_DIR/pre-push"
echo -e "${GREEN}✓ pre-push hook をインストールしました${NC}"

# ===========================================================================
# サマリー
# ===========================================================================
echo -e "\n${GREEN}━━━ Git Hooks インストール完了 ━━━${NC}"
echo ""
echo "インストールされたフック:"
echo "  • pre-commit: コミット前に quick-check.sh を実行（Docker優先）"
echo "  • pre-push:   プッシュ前にフルCIチェックを実行（Docker優先）"
echo ""
echo "使い方:"
echo "  • 通常のコミット: git commit -m '...'"
echo "  • 通常のプッシュ: git push"
echo "  • コミットスキップ: SKIP_PRE_COMMIT=1 git commit -m '...'"
echo "  • プッシュスキップ: SKIP_PRE_PUSH=1 git push"
echo "  • フック削除: ./scripts/install-hooks.sh --remove"
