#!/bin/bash
# agent-final-check.sh - Codex 完了判定用フルチェック（リトライ対応）
#
# 使い方:
#   ./scripts/agent-final-check.sh                 # 自動判定（Docker優先）
#   ./scripts/agent-final-check.sh --docker        # Dockerを強制
#   ./scripts/agent-final-check.sh --local         # ローカルを強制
#   ./scripts/agent-final-check.sh --quick         # quick チェックで実行
#   ./scripts/agent-final-check.sh --retries=2     # 失敗時の再試行回数
#   ./scripts/agent-final-check.sh --until-ok      # 成功まで再試行（無制限）
#   ./scripts/agent-final-check.sh --help          # ヘルプ表示

set -euo pipefail

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

USE_DOCKER="${SAPPP_AGENT_USE_DOCKER:-auto}" # auto|force|local
RETRIES="${SAPPP_AGENT_RETRIES:-2}"
SLEEP_SECS="${SAPPP_AGENT_SLEEP:-3}"
UNTIL_OK=false
MODE="full"

for arg in "$@"; do
    case $arg in
        --docker)
            USE_DOCKER="force"
            ;;
        --local)
            USE_DOCKER="local"
            ;;
        --quick)
            MODE="quick"
            ;;
        --retries=*)
            RETRIES="${arg#*=}"
            ;;
        --sleep=*)
            SLEEP_SECS="${arg#*=}"
            ;;
        --until-ok)
            UNTIL_OK=true
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Codex 完了判定用のフルチェックを実行し、失敗時は再試行します。"
            echo ""
            echo "Options:"
            echo "  --docker        Dockerを強制（CI完全再現）"
            echo "  --local         ローカル実行を強制"
            echo "  --quick         quick チェックで実行"
            echo "  --retries=NUM   失敗時の再試行回数（デフォルト: 2）"
            echo "  --sleep=SEC     再試行前の待機秒数（デフォルト: 3）"
            echo "  --until-ok      成功まで再試行（無制限）"
            echo "  --help, -h      このヘルプを表示"
            exit 0
            ;;
    esac
done

if [ "$MODE" = "quick" ]; then
    LOCAL_CMD="./scripts/quick-check.sh"
    DOCKER_CMD="./scripts/docker-ci.sh --quick"
else
    LOCAL_CMD="./scripts/pre-commit-check.sh"
    DOCKER_CMD="./scripts/docker-ci.sh"
fi

select_command() {
    if [ "$USE_DOCKER" = "force" ]; then
        echo "$DOCKER_CMD"
        return 0
    fi
    if [ "$USE_DOCKER" = "local" ]; then
        echo "$LOCAL_CMD"
        return 0
    fi
    if command -v docker &> /dev/null && docker info &> /dev/null; then
        if docker image inspect sappp-ci &> /dev/null; then
            echo "$DOCKER_CMD"
            return 0
        fi
    fi
    echo "$LOCAL_CMD"
}

CMD="$(select_command)"

attempt=1
while true; do
    echo -e "${BLUE}▶ Final Check Attempt ${attempt}: ${CMD}${NC}"
    if eval "$CMD"; then
        echo -e "${GREEN}✓ Final check passed${NC}"
        exit 0
    fi

    if [ "$UNTIL_OK" = true ]; then
        echo -e "${YELLOW}⚠ Final check failed. Retrying...${NC}"
        attempt=$((attempt + 1))
        sleep "$SLEEP_SECS"
        continue
    fi

    if [ "$attempt" -ge $((RETRIES + 1)) ]; then
        echo -e "${RED}✗ Final check failed after ${attempt} attempts${NC}"
        exit 1
    fi

    echo -e "${YELLOW}⚠ Final check failed. Retrying...${NC}"
    attempt=$((attempt + 1))
    sleep "$SLEEP_SECS"
done
