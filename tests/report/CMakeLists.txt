add_executable(test_report_explain
    test_explain.cpp
)

sappp_target_strict_warnings(test_report_explain)

target_link_libraries(test_report_explain PRIVATE
    sappp_report
    sappp_common
    sappp_canonical
    nlohmann_json::nlohmann_json
    GTest::gtest_main
)

target_compile_definitions(test_report_explain PRIVATE
    SAPPP_SCHEMA_DIR="${CMAKE_SOURCE_DIR}/schemas"
)

sappp_register_gtest(test_report_explain report)
