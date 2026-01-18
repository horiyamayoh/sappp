# SAP++ Makefile - 統一ビルド・チェックインターフェース
#
# 使い方:
#   make help       # コマンド一覧を表示
#   make quick      # 高速チェック（コミット前推奨）
#   make ci         # フルCIチェック（プッシュ前推奨）
#   make docker-ci  # Docker環境でフルCIチェック
#
# このMakefileはCI/ローカルで共通のコマンドを提供します。

.PHONY: help quick ci docker-ci build test clean format tidy install-hooks check-env

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
CMAKE_OPTS := -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
              -DCMAKE_C_COMPILER=$(CC) \
              -DCMAKE_CXX_COMPILER=$(CXX) \
              -DSAPPP_BUILD_TESTS=ON \
              -DSAPPP_BUILD_CLANG_FRONTEND=OFF \
              -DSAPPP_WERROR=ON \
              -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

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
	@echo "$(BLUE)開発ワークフロー:$(NC)"
	@echo "  make quick       高速チェック（30秒以内、コミット前推奨）"
	@echo "  make ci          フルCIチェック（プッシュ前推奨）"
	@echo "  make docker-ci   Docker環境でフルCIチェック（確実）"
	@echo ""
	@echo "$(BLUE)ビルド:$(NC)"
	@echo "  make build       プロジェクトをビルド"
	@echo "  make test        テストを実行"
	@echo "  make clean       ビルドディレクトリを削除"
	@echo ""
	@echo "$(BLUE)コード品質:$(NC)"
	@echo "  make format      clang-formatを適用（自動修正）"
	@echo "  make format-check フォーマットをチェック（修正しない）"
	@echo "  make tidy        clang-tidyを実行"
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
	@echo "  4. git commit           # pre-commit hookが自動実行"
	@echo "  5. git push             # pre-push hookが自動実行"
	@echo ""

# ===========================================================================
# 高速チェック（コミット前）
# ===========================================================================
quick:
	@./scripts/quick-check.sh

# ===========================================================================
# フルCIチェック（プッシュ前）
# ===========================================================================
ci:
	@./scripts/pre-commit-check.sh

# ===========================================================================
# Docker CIチェック（確実）
# ===========================================================================
docker-ci:
	@./scripts/docker-ci.sh

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
	@cmake --build $(BUILD_DIR) --parallel
	@echo "$(GREEN)✓ ビルド完了$(NC)"

# ===========================================================================
# テスト
# ===========================================================================
test: build
	@echo "$(BLUE)▶ テスト実行中...$(NC)"
	@ctest --test-dir $(BUILD_DIR) --output-on-failure
	@echo "$(GREEN)✓ テスト完了$(NC)"

test-determinism: build
	@echo "$(BLUE)▶ 決定性テスト実行中...$(NC)"
	@ctest --test-dir $(BUILD_DIR) -R determinism --output-on-failure
	@echo "$(GREEN)✓ 決定性テスト完了$(NC)"

# ===========================================================================
# コード品質
# ===========================================================================
CLANG_FORMAT := $(shell command -v clang-format-18 2>/dev/null || command -v clang-format 2>/dev/null || echo "")
CLANG_TIDY := $(shell command -v clang-tidy-18 2>/dev/null || command -v clang-tidy 2>/dev/null || echo "")

CPP_FILES := $(shell find libs tools tests include -name '*.cpp' -o -name '*.hpp' -o -name '*.h' 2>/dev/null)

format:
ifeq ($(CLANG_FORMAT),)
	@echo "$(YELLOW)Warning: clang-format not found$(NC)"
else
	@echo "$(BLUE)▶ フォーマット適用中...$(NC)"
	@$(CLANG_FORMAT) -i $(CPP_FILES)
	@echo "$(GREEN)✓ フォーマット完了$(NC)"
endif

format-check:
ifeq ($(CLANG_FORMAT),)
	@echo "$(YELLOW)Warning: clang-format not found$(NC)"
else
	@echo "$(BLUE)▶ フォーマットチェック中...$(NC)"
	@$(CLANG_FORMAT) --dry-run --Werror $(CPP_FILES)
	@echo "$(GREEN)✓ フォーマットOK$(NC)"
endif

tidy: configure
ifeq ($(CLANG_TIDY),)
	@echo "$(YELLOW)Warning: clang-tidy not found$(NC)"
else
	@echo "$(BLUE)▶ clang-tidy実行中...$(NC)"
	@find libs -name '*.cpp' -print0 | xargs -0 -P$$(nproc) -I{} $(CLANG_TIDY) -p $(BUILD_DIR) --warnings-as-errors='*' {}
	@echo "$(GREEN)✓ clang-tidy完了$(NC)"
endif

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
	@rm -rf $(BUILD_DIR) build-clang
	@echo "$(GREEN)✓ クリーンアップ完了$(NC)"

distclean: clean
	@rm -rf .cache
