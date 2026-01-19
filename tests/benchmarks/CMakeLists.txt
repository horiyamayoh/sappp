# ===========================================================================
# SAP++ Benchmark Tests
# ===========================================================================
#
# ベンチマークテストは Google Benchmark を使用します。
# 性能回帰テストの自動化に使用されます。
#
# 使い方:
#   cmake -DSAPPP_BUILD_BENCHMARKS=ON ...
#   cmake --build build --target benchmarks
#   ./build/bin/bench_canonical

if(NOT SAPPP_BUILD_BENCHMARKS)
    return()
endif()

# Google Benchmark を取得
include(FetchContent)
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
)
# Google Test は既にあるので、Benchmark のテストは無効化
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

# ===========================================================================
# ベンチマークターゲット登録関数
# ===========================================================================
function(sappp_register_benchmark target)
    # ベンチマークには厳格な警告を適用しない（Google Benchmark ヘッダーとの互換性のため）
    # ただし、基本的な警告は有効
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        # Google Benchmark ヘッダーの警告を抑制
        -Wno-useless-cast
        -Wno-switch-default
    )
    target_link_libraries(${target} PRIVATE benchmark::benchmark benchmark::benchmark_main)
    
    # ベンチマーク用のカスタムターゲット
    add_custom_target(run_${target}
        COMMAND ${target}
        DEPENDS ${target}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running benchmark: ${target}"
    )
endfunction()

# ===========================================================================
# Canonical JSON ベンチマーク
# ===========================================================================
add_executable(bench_canonical
    bench_canonical.cpp
)
target_link_libraries(bench_canonical PRIVATE
    sappp_canonical
    sappp_common
)
sappp_register_benchmark(bench_canonical)

# ===========================================================================
# 全ベンチマーク実行ターゲット
# ===========================================================================
add_custom_target(benchmarks
    DEPENDS bench_canonical
    COMMENT "Building all benchmarks"
)

add_custom_target(run_benchmarks
    COMMAND bench_canonical --benchmark_format=console --benchmark_counters_tabular=true
    DEPENDS benchmarks
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all benchmarks"
)

# ベンチマーク結果をJSONで出力するターゲット
add_custom_target(run_benchmarks_json
    COMMAND bench_canonical --benchmark_format=json --benchmark_out=${CMAKE_BINARY_DIR}/benchmark_results.json
    DEPENDS benchmarks
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all benchmarks (JSON output)"
)
