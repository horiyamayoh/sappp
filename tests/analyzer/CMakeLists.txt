add_executable(test_analyzer
    test_analyzer.cpp
)

target_link_libraries(test_analyzer PRIVATE
    sappp_analyzer
    sappp_common
    sappp_canonical
    nlohmann_json::nlohmann_json
    GTest::gtest_main
)

target_compile_definitions(test_analyzer PRIVATE
    SAPPP_SCHEMA_DIR="${CMAKE_SOURCE_DIR}/schemas"
)

include(GoogleTest)
gtest_discover_tests(test_analyzer)
