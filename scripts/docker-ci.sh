#!/bin/bash
# docker-ci.sh - CIと完全同一のDocker環境でチェックを実行
#
# 使い方:
#   ./scripts/docker-ci.sh              # フルCIチェック
#   ./scripts/docker-ci.sh --quick      # 高速チェックのみ
#   ./scripts/docker-ci.sh --build-only # ビルドのみ
#   ./scripts/docker-ci.sh --shell      # コンテナ内でシェルを起動
#
# このスクリプトはGitHub Actions CIと完全に同一の環境を提供します。
# 「ローカルでは通ったのにCIで落ちる」問題を根本的に解決します。

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

# デフォルト設定
IMAGE_NAME="sappp-ci"
COMMAND="./scripts/pre-commit-check.sh"
INTERACTIVE=false

# オプション解析
for arg in "$@"; do
    case $arg in
        --quick)
            COMMAND="./scripts/quick-check.sh"
            ;;
        --build-only)
            COMMAND="cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 -DSAPPP_BUILD_TESTS=ON -DSAPPP_WERROR=ON && cmake --build build --parallel"
            ;;
        --shell)
            COMMAND="/bin/bash"
            INTERACTIVE=true
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "CIと完全同一のDocker環境でチェックを実行します。"
            echo ""
            echo "Options:"
            echo "  --quick       高速チェックのみ（format + build + test）"
            echo "  --build-only  ビルドのみ実行"
            echo "  --shell       コンテナ内でシェルを起動（デバッグ用）"
            echo "  --help, -h    このヘルプを表示"
            echo ""
            echo "Examples:"
            echo "  $0              # フルCIチェック"
            echo "  $0 --quick      # コミット前の高速チェック"
            echo "  $0 --shell      # コンテナ内で手動デバッグ"
            exit 0
            ;;
    esac
done

# Dockerイメージのビルド（必要な場合）
build_image() {
    echo -e "${BLUE}▶ Dockerイメージをビルド中...${NC}"
    
    # イメージが存在するかチェック
    if docker image inspect "$IMAGE_NAME" &> /dev/null; then
        # Dockerfileの更新チェック
        DOCKERFILE_HASH=$(sha256sum docker/Dockerfile | cut -d' ' -f1)
        EXISTING_HASH=$(docker image inspect "$IMAGE_NAME" --format '{{.Config.Labels.dockerfile_hash}}' 2>/dev/null || echo "")
        
        if [ "$DOCKERFILE_HASH" = "$EXISTING_HASH" ]; then
            echo -e "${GREEN}✓ イメージは最新です${NC}"
            return 0
        fi
        echo -e "${YELLOW}Dockerfileが更新されています。再ビルドします...${NC}"
    fi
    
    docker build \
        --label "dockerfile_hash=$DOCKERFILE_HASH" \
        -t "$IMAGE_NAME" \
        docker/
    
    echo -e "${GREEN}✓ Dockerイメージのビルド完了${NC}"
}

# メイン実行
main() {
    echo -e "${YELLOW}"
    echo "╔════════════════════════════════════════════════════════════════╗"
    echo "║            SAP++ Docker CI Environment                        ║"
    echo "╚════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
    
    # Docker確認
    if ! command -v docker &> /dev/null; then
        echo -e "${RED}Error: Docker がインストールされていません${NC}"
        echo "https://docs.docker.com/get-docker/ からインストールしてください"
        exit 1
    fi
    
    # イメージビルド
    build_image
    
    echo -e "\n${BLUE}▶ コンテナ内でチェックを実行中...${NC}"
    echo -e "${BLUE}  Command: $COMMAND${NC}\n"
    
    # Docker実行オプション
    DOCKER_OPTS=(
        --rm
        -v "$PROJECT_ROOT:/workspace"
        -w /workspace
        -e "TERM=xterm-256color"
        -e "SAPPP_CI_ENV=docker"
    )
    
    if [ "$INTERACTIVE" = true ]; then
        DOCKER_OPTS+=(-it)
    fi
    
    # 実行
    if docker run "${DOCKER_OPTS[@]}" "$IMAGE_NAME" bash -c "$COMMAND"; then
        echo -e "\n${GREEN}━━━ Docker CI チェック通過！ ━━━${NC}"
        exit 0
    else
        echo -e "\n${RED}━━━ Docker CI チェック失敗 ━━━${NC}"
        exit 1
    fi
}

main
