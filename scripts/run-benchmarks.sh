#!/bin/bash
# run-benchmarks.sh - ベンチマークを実行し、結果を比較
#
# 使い方:
#   ./scripts/run-benchmarks.sh              # ベンチマーク実行
#   ./scripts/run-benchmarks.sh --baseline   # ベースライン保存
#   ./scripts/run-benchmarks.sh --compare    # ベースラインと比較
#   ./scripts/run-benchmarks.sh --json       # JSON形式で出力
#
# 必要なツール:
#   - Google Benchmark（CMakeで自動取得）
#   - compare.py（Google Benchmark付属、比較時に使用）

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
BUILD_DIR="${SAPPP_BUILD_DIR:-build}"
BASELINE_DIR="$PROJECT_ROOT/.benchmarks"
BASELINE_FILE="$BASELINE_DIR/baseline.json"
OUTPUT_JSON=false
SAVE_BASELINE=false
COMPARE_BASELINE=false
THRESHOLD="${SAPPP_BENCHMARK_THRESHOLD:-10}"  # 性能劣化の許容閾値（%）

for arg in "$@"; do
    case $arg in
        --baseline)
            SAVE_BASELINE=true
            OUTPUT_JSON=true
            ;;
        --compare)
            COMPARE_BASELINE=true
            OUTPUT_JSON=true
            ;;
        --json)
            OUTPUT_JSON=true
            ;;
        --threshold=*)
            THRESHOLD="${arg#*=}"
            ;;
        --build-dir=*)
            BUILD_DIR="${arg#*=}"
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "ベンチマークを実行し、性能回帰を検出します。"
            echo ""
            echo "Options:"
            echo "  --baseline     結果をベースラインとして保存"
            echo "  --compare      ベースラインと比較して回帰を検出"
            echo "  --json         JSON形式で出力"
            echo "  --threshold=N  性能劣化の許容閾値（%、デフォルト: 10）"
            echo "  --build-dir=   ビルドディレクトリを指定"
            echo "  --help, -h     このヘルプを表示"
            echo ""
            echo "Examples:"
            echo "  $0                        # ベンチマーク実行"
            echo "  $0 --baseline             # ベースライン保存"
            echo "  $0 --compare              # ベースラインと比較"
            echo "  $0 --compare --threshold=5  # 5%以上の劣化で警告"
            exit 0
            ;;
    esac
done

# ===========================================================================
# ビルド確認
# ===========================================================================
echo -e "${YELLOW}━━━ SAP++ Benchmark Runner ━━━${NC}"
echo ""

BENCHMARK_BIN="$BUILD_DIR/bin/bench_canonical"

if [ ! -f "$BENCHMARK_BIN" ]; then
    echo -e "${BLUE}▶ ベンチマークをビルド中...${NC}"
    
    GENERATOR="$(get_cmake_generator)"
    CC="$(detect_gcc14 | sed 's/g++/gcc/' || echo gcc)"
    CXX="$(detect_gcc14 || echo g++)"
    
    cmake -S . -B "$BUILD_DIR" $GENERATOR \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_CXX_COMPILER="$CXX" \
        -DSAPPP_BUILD_TESTS=ON \
        -DSAPPP_BUILD_BENCHMARKS=ON \
        -DSAPPP_BUILD_CLANG_FRONTEND=OFF
    
    cmake --build "$BUILD_DIR" --target benchmarks --parallel "$(get_build_jobs)"
    
    echo -e "${GREEN}✓ ビルド完了${NC}"
fi

# ===========================================================================
# ベンチマーク実行
# ===========================================================================
echo -e "\n${BLUE}▶ ベンチマーク実行中...${NC}"

RESULT_FILE="$BUILD_DIR/benchmark_results.json"

if [ "$OUTPUT_JSON" = true ]; then
    "$BENCHMARK_BIN" \
        --benchmark_format=json \
        --benchmark_out="$RESULT_FILE" \
        --benchmark_counters_tabular=true
else
    "$BENCHMARK_BIN" \
        --benchmark_format=console \
        --benchmark_counters_tabular=true
fi

# ===========================================================================
# ベースライン保存
# ===========================================================================
if [ "$SAVE_BASELINE" = true ]; then
    echo -e "\n${BLUE}▶ ベースラインを保存中...${NC}"
    mkdir -p "$BASELINE_DIR"
    cp "$RESULT_FILE" "$BASELINE_FILE"
    
    # メタデータを追加
    COMMIT_HASH=$(git rev-parse HEAD 2>/dev/null || echo "unknown")
    TIMESTAMP=$(date -Iseconds)
    
    cat > "$BASELINE_DIR/metadata.json" << EOF
{
    "commit": "$COMMIT_HASH",
    "timestamp": "$TIMESTAMP",
    "threshold_percent": $THRESHOLD
}
EOF
    
    echo -e "${GREEN}✓ ベースライン保存: $BASELINE_FILE${NC}"
fi

# ===========================================================================
# ベースライン比較
# ===========================================================================
if [ "$COMPARE_BASELINE" = true ]; then
    echo -e "\n${BLUE}▶ ベースラインと比較中...${NC}"
    
    if [ ! -f "$BASELINE_FILE" ]; then
        echo -e "${RED}Error: ベースラインが見つかりません: $BASELINE_FILE${NC}"
        echo -e "${YELLOW}  まず --baseline で保存してください${NC}"
        exit 1
    fi
    
    # Python スクリプトで比較（Google Benchmark の compare.py がなくても動作）
    python3 - "$BASELINE_FILE" "$RESULT_FILE" "$THRESHOLD" << 'PY'
import json
import sys

baseline_path = sys.argv[1]
current_path = sys.argv[2]
threshold = float(sys.argv[3])

with open(baseline_path) as f:
    baseline = json.load(f)
with open(current_path) as f:
    current = json.load(f)

# ベンチマーク結果を名前でインデックス化
baseline_by_name = {b["name"]: b for b in baseline.get("benchmarks", [])}
current_by_name = {b["name"]: b for b in current.get("benchmarks", [])}

regressions = []
improvements = []
unchanged = []

for name, curr in current_by_name.items():
    if name not in baseline_by_name:
        continue
    
    base = baseline_by_name[name]
    base_time = base.get("real_time", base.get("cpu_time", 0))
    curr_time = curr.get("real_time", curr.get("cpu_time", 0))
    
    if base_time == 0:
        continue
    
    change_percent = ((curr_time - base_time) / base_time) * 100
    
    if change_percent > threshold:
        regressions.append((name, base_time, curr_time, change_percent))
    elif change_percent < -threshold:
        improvements.append((name, base_time, curr_time, change_percent))
    else:
        unchanged.append((name, base_time, curr_time, change_percent))

# 結果表示
print("\n" + "=" * 70)
print("Benchmark Comparison Results")
print("=" * 70)

if regressions:
    print(f"\n\033[31m⚠ REGRESSIONS (>{threshold}% slower):\033[0m")
    for name, base, curr, pct in sorted(regressions, key=lambda x: -x[3]):
        print(f"  {name}")
        print(f"    Baseline: {base:.2f} ns → Current: {curr:.2f} ns ({pct:+.1f}%)")

if improvements:
    print(f"\n\033[32m✓ IMPROVEMENTS (>{threshold}% faster):\033[0m")
    for name, base, curr, pct in sorted(improvements, key=lambda x: x[3]):
        print(f"  {name}")
        print(f"    Baseline: {base:.2f} ns → Current: {curr:.2f} ns ({pct:+.1f}%)")

if unchanged:
    print(f"\n\033[34mℹ UNCHANGED (within ±{threshold}%):\033[0m")
    for name, base, curr, pct in unchanged:
        print(f"  {name}: {pct:+.1f}%")

print("\n" + "=" * 70)

if regressions:
    print(f"\033[31mResult: {len(regressions)} regression(s) detected!\033[0m")
    sys.exit(1)
else:
    print(f"\033[32mResult: No regressions detected.\033[0m")
    sys.exit(0)
PY
    
    COMPARE_EXIT=$?
    if [ $COMPARE_EXIT -ne 0 ]; then
        echo -e "\n${RED}━━━ 性能回帰が検出されました ━━━${NC}"
        exit 1
    fi
fi

echo -e "\n${GREEN}━━━ ベンチマーク完了 ━━━${NC}"
