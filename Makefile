# SAP++ Makefile - 統一ビルド・チェックインターフェース
#
# 使い方:
#   make help       # コマンド一覧を表示
#   make quick      # 高速チェック（作業中に推奨）
#   make ci         # CI互換の厳格チェック（ローカル）
#   make docker-ci  # Docker環境でCI互換チェック
#
# このMakefileはCI/ローカルで共通のコマンドを提供します。

.PHONY: help quick smart ci docker-ci build test clean format tidy install-hooks check-env

# デフォルトターゲット
.DEFAULT_GOAL := help

# 色定義（端末が対応している場合）
BLUE := \033[0;34m
GREEN := \033[0;32m
YELLOW := \033[1;33m
NC := \033[0m

# コンパイラ設定
CXX := $(shell command -v g++-14 2>/dev/null || echo g++)
CC := $(shell command -v gcc-14 2>/dev/null || echo gcc)

# ビルド設定
BUILD_DIR := build
BUILD_TYPE := Debug

BUILD_JOBS := $(shell command -v nproc >/dev/null 2>&1 && nproc || echo 1)
ifneq ($(SAPPP_BUILD_JOBS),)
BUILD_JOBS := $(SAPPP_BUILD_JOBS)
endif

CMAKE_LAUNCHER_OPTS :=
ifneq ($(SAPPP_USE_CCACHE),)
ifneq ($(SAPPP_USE_CCACHE),0)
CMAKE_LAUNCHER_OPTS := -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
endif
endif

CMAKE_OPTS := -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
              -DCMAKE_C_COMPILER=$(CC) \
              -DCMAKE_CXX_COMPILER=$(CXX) \
              -DSAPPP_BUILD_TESTS=ON \
              -DSAPPP_BUILD_CLANG_FRONTEND=OFF \
              -DSAPPP_WERROR=ON \
              -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
              $(CMAKE_LAUNCHER_OPTS)

# Ninja優先
GENERATOR := $(shell command -v ninja >/dev/null 2>&1 && echo "-G Ninja" || echo "")

# ===========================================================================
# ヘルプ
# ===========================================================================
help:
	@echo ""
	@echo "$(YELLOW)SAP++ Build System$(NC)"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo ""
	@echo "$(BLUE)開発ワークフロー（3段階ゲート）:$(NC)"
	@echo "  make quick       L0: 高速チェック（30秒以内、作業中に推奨）"
	@echo "  make smart       L1: コミットゲート（変更最適）"
	@echo "  make ci          L2: CI互換の厳格チェック（ローカル）"
	@echo "  make docker-ci   L2: Docker CI（CI環境を完全再現）"
	@echo ""
	@echo "$(BLUE)追加のチェックモード:$(NC)"
	@echo "  ./scripts/pre-commit-check.sh  full ゲート（ローカルフル）"
	@echo ""
	@echo "$(BLUE)ビルド & テスト:$(NC)"
	@echo "  make build       プロジェクトをビルド"
	@echo "  make test        全テストを実行"
	@echo "  make test-quick  高速テストのみ（quickラベル）"
	@echo "  make test-determinism  決定性テストのみ"
	@echo "  make test-scripts      スクリプトセルフテストを実行"
	@echo "  make clean       ビルドディレクトリを削除"
	@echo ""
	@echo "$(BLUE)コード品質:$(NC)"
	@echo "  make format      clang-formatを適用（自動修正）"
	@echo "  make format-check フォーマットをチェック（修正しない）"
	@echo "  make tidy        clang-tidyを実行"
	@echo ""
	@echo "$(BLUE)カバレッジ & ベンチマーク:$(NC)"
	@echo "  make coverage          カバレッジレポート生成（HTML）"
	@echo "  make coverage-open     カバレッジレポートをブラウザで開く"
	@echo "  make benchmarks        ベンチマーク実行"
	@echo "  make benchmarks-baseline  ベースライン保存"
	@echo "  make benchmarks-compare   ベースラインと比較"
	@echo ""
	@echo "$(BLUE)セットアップ:$(NC)"
	@echo "  make install-hooks Git hooksをインストール"
	@echo "  make check-env     環境の状態をチェック"
	@echo "  make docker-build  Dockerイメージをビルド"
	@echo ""
	@echo "$(BLUE)推奨ワークフロー:$(NC)"
	@echo "  1. make check-env       # 環境確認"
	@echo "  2. make install-hooks   # Git hooks インストール"
	@echo "  3. （コーディング）"
	@echo "  4. git commit           # pre-commit hookが smart チェック"
	@echo "  5. git push             # pre-push hookがスタンプ確認"
	@echo ""

# ===========================================================================
# 高速チェック（作業中）
# ===========================================================================
quick:
	@./scripts/quick-check.sh

# ===========================================================================
# 変更内容に応じたチェック
# ===========================================================================
smart:
	@./scripts/pre-commit-check.sh --smart

# ===========================================================================
# CI互換の厳格チェック
# ===========================================================================
ci:
	@./scripts/pre-commit-check.sh --ci

# ===========================================================================
# Docker CIチェック（CI互換）
# ===========================================================================
docker-ci:
	@./scripts/docker-ci.sh --ci

docker-quick:
	@./scripts/docker-ci.sh --quick

docker-shell:
	@./scripts/docker-ci.sh --shell

docker-build:
	@docker build -t sappp-ci docker/

# ===========================================================================
# 環境チェック
# ===========================================================================
check-env:
	@./scripts/check-env.sh

check-env-fix:
	@./scripts/check-env.sh --fix

# ===========================================================================
# ビルド
# ===========================================================================
$(BUILD_DIR)/Makefile $(BUILD_DIR)/build.ninja:
	@echo "$(BLUE)▶ CMake設定中...$(NC)"
	@cmake -S . -B $(BUILD_DIR) $(GENERATOR) $(CMAKE_OPTS)

configure: $(BUILD_DIR)/Makefile $(BUILD_DIR)/build.ninja

build: configure
	@echo "$(BLUE)▶ ビルド中...$(NC)"
	@cmake --build $(BUILD_DIR) --parallel $(BUILD_JOBS)
	@echo "$(GREEN)✓ ビルド完了$(NC)"

# ===========================================================================
# テスト
# ===========================================================================
test: build
	@echo "$(BLUE)▶ テスト実行中...$(NC)"
	@ctest --test-dir $(BUILD_DIR) --output-on-failure -j$(BUILD_JOBS)
	@echo "$(GREEN)✓ テスト完了$(NC)"

test-determinism: build
	@echo "$(BLUE)▶ 決定性テスト実行中...$(NC)"
	@ctest --test-dir $(BUILD_DIR) -R determinism --output-on-failure -j$(BUILD_JOBS)
	@echo "$(GREEN)✓ 決定性テスト完了$(NC)"

test-scripts:
	@echo "$(BLUE)▶ スクリプトセルフテスト実行中...$(NC)"
	@./scripts/lib/test-common.sh
	@echo "$(GREEN)✓ スクリプトセルフテスト完了$(NC)"

test-quick:
	@echo "$(BLUE)▶ 高速テスト実行中（quickラベル）...$(NC)"
	@ctest --test-dir $(BUILD_DIR) -L quick --output-on-failure -j$(BUILD_JOBS)
	@echo "$(GREEN)✓ 高速テスト完了$(NC)"

# ===========================================================================
# コード品質（単一スクリプトで対象範囲を一元管理）
# ===========================================================================

format:
	@# 単一スクリプトで対象範囲を一元管理
	@./scripts/run-clang-format.sh --fix

format-check:
	@# 単一スクリプトで対象範囲を一元管理
	@./scripts/run-clang-format.sh --check

tidy: configure
	@# 単一スクリプトで対象範囲を一元管理
	@./scripts/run-clang-tidy.sh --build-dir $(BUILD_DIR) --jobs $(BUILD_JOBS)

# ===========================================================================
# カバレッジ & ベンチマーク
# ===========================================================================
coverage:
	@./scripts/run-coverage.sh --html

coverage-open:
	@./scripts/run-coverage.sh --html --open

benchmarks:
	@./scripts/run-benchmarks.sh

benchmarks-baseline:
	@./scripts/run-benchmarks.sh --baseline

benchmarks-compare:
	@./scripts/run-benchmarks.sh --compare

# ===========================================================================
# セットアップ
# ===========================================================================
install-hooks:
	@./scripts/install-hooks.sh

# ===========================================================================
# クリーンアップ
# ===========================================================================
clean:
	@echo "$(BLUE)▶ クリーンアップ中...$(NC)"
	@rm -rf $(BUILD_DIR) build-clang build-coverage
	@echo "$(GREEN)✓ クリーンアップ完了$(NC)"

distclean: clean
	@rm -rf .cache .benchmarks
