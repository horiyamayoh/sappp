# Find or fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

# テストラベル定義
# - quick: 高速テスト（30秒以内で完了すべき）
# - slow: 時間のかかるテスト（E2E、統合テスト等）
# - determinism: 決定性テスト
# - benchmark: ベンチマークテスト
set(SAPPP_QUICK_LABELS
    build_capture
    determinism
    po
    report
    schemas
    validator
)

function(sappp_register_gtest target label)
    # ラベルリストを構築
    set(test_labels "${label}")
    
    # quick ラベルの追加判定
    list(FIND SAPPP_QUICK_LABELS "${label}" _quick_index)
    if(NOT _quick_index EQUAL -1)
        list(APPEND test_labels "quick")
    endif()
    
    # セミコロン区切りの文字列に変換
    string(REPLACE ";" "\\;" test_labels_escaped "${test_labels}")
    
    gtest_discover_tests(${target}
        DISCOVERY_MODE PRE_TEST
        TEST_PREFIX "${label}."
        PROPERTIES LABELS "${test_labels_escaped}"
    )
endfunction()

# Schema validation tests
add_subdirectory(schemas)

# Build capture tests
add_subdirectory(build_capture)

# Determinism tests
add_subdirectory(determinism)

# Report tests
add_subdirectory(report)

# End-to-end tests
add_subdirectory(end_to_end)

# PO generator tests
add_subdirectory(po)

# Validator tests
add_subdirectory(validator)

# Benchmark tests
add_subdirectory(benchmarks)

# Frontend (Clang) tests
if(SAPPP_BUILD_CLANG_FRONTEND)
    add_subdirectory(frontend_clang)
endif()
