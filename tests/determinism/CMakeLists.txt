add_executable(test_determinism
    test_sha256.cpp
    test_path.cpp
    test_canonical_json.cpp
)

target_link_libraries(test_determinism PRIVATE
    sappp_common
    sappp_canonical
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_determinism)
