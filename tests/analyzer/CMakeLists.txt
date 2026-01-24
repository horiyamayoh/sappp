add_executable(analyzer_tests
    test_analyzer_contracts.cpp
)

sappp_target_strict_warnings(analyzer_tests)

target_compile_definitions(analyzer_tests PRIVATE
    SAPPP_SCHEMA_DIR="${CMAKE_SOURCE_DIR}/schemas"
)

target_link_libraries(analyzer_tests PRIVATE
    sappp_analyzer
    sappp_certstore
    GTest::gtest_main
)

sappp_register_gtest(analyzer_tests analyzer)
