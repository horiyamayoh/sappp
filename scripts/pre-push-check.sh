#!/bin/bash
# pre-push-check.sh - pre-push 時の軽量チェック（フルチェック済みの確認）
#
# 使い方:
#   ./scripts/pre-push-check.sh           # スタンプ確認のみ（デフォルト）
#   ./scripts/pre-push-check.sh --strict  # スタンプ不一致をエラー扱い
#   ./scripts/pre-push-check.sh --help    # ヘルプ表示

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

STRICT_MODE=false
STAMP_FILE="${SAPPP_CI_STAMP_FILE:-$PROJECT_ROOT/.git/sappp/ci-stamp.json}"

for arg in "$@"; do
    case $arg in
        --strict)
            STRICT_MODE=true
            ;;
        --stamp-file=*)
            STAMP_FILE="${arg#*=}"
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "pre-push 時にフルチェック済みスタンプを確認します。"
            echo ""
            echo "Options:"
            echo "  --strict        スタンプ不一致をエラー扱い"
            echo "  --stamp-file=   スタンプファイルのパスを指定"
            echo "  --help, -h      このヘルプを表示"
            exit 0
            ;;
    esac
done

extract_json_value() {
    local key="$1"
    if command -v python3 &> /dev/null; then
        python3 - "$STAMP_FILE" "$key" << 'PY'
import json
import sys

path = sys.argv[1]
key = sys.argv[2]
try:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    value = data.get(key, "")
    if value is None:
        print("")
    else:
        print(value)
except Exception:
    print("")
PY
        return 0
    fi

    if command -v rg &> /dev/null; then
        rg -n "\"$key\"\\s*:\\s*\"?([^\"]*)\"?" "$STAMP_FILE" -r '$1' || true
    else
        grep -E "\"$key\"\\s*:" "$STAMP_FILE" | sed -E "s/.*\"$key\"\\s*:\\s*\"?([^\"]*)\"?.*/\\1/" || true
    fi
}

echo -e "${YELLOW}━━━ SAP++ Pre-push Stamp Check ━━━${NC}"

if ! git rev-parse --git-dir > /dev/null 2>&1; then
    echo -e "${YELLOW}⚠ Git リポジトリが見つかりません。チェックをスキップします。${NC}"
    exit 0
fi

if [ ! -f "$STAMP_FILE" ]; then
    echo -e "${YELLOW}⚠ スタンプが見つかりません: $STAMP_FILE${NC}"
    echo -e "${YELLOW}  推奨: ./scripts/pre-commit-check.sh${NC}"
    if [ "$STRICT_MODE" = true ]; then
        exit 1
    fi
    exit 0
fi

CHECK_MODE="$(extract_json_value "check_mode")"
TREE_HASH="$(extract_json_value "tree_hash")"

HEAD_TREE=""
if git rev-parse --verify HEAD > /dev/null 2>&1; then
    HEAD_TREE="$(git rev-parse HEAD^{tree})"
fi

OK=true
if [ "$CHECK_MODE" != "full" ]; then
    echo -e "${YELLOW}⚠ スタンプはフルチェックではありません (mode=$CHECK_MODE)${NC}"
    OK=false
fi

if [ -z "$TREE_HASH" ] || [ -z "$HEAD_TREE" ]; then
    echo -e "${YELLOW}⚠ スタンプ/HEAD のツリーハッシュが取得できません${NC}"
    OK=false
elif [ "$TREE_HASH" != "$HEAD_TREE" ]; then
    echo -e "${YELLOW}⚠ スタンプとHEADのツリーハッシュが一致しません${NC}"
    echo -e "${YELLOW}  stamp: $TREE_HASH${NC}"
    echo -e "${YELLOW}  head : $HEAD_TREE${NC}"
    OK=false
fi

if [ "$OK" = true ]; then
    echo -e "${GREEN}✓ フルチェック済みスタンプが確認できました${NC}"
    exit 0
fi

echo -e "${YELLOW}⚠ フルチェック済みの確認に失敗しました${NC}"
echo -e "${YELLOW}  推奨: ./scripts/pre-commit-check.sh${NC}"
if [ "$STRICT_MODE" = true ]; then
    exit 1
fi
exit 0
