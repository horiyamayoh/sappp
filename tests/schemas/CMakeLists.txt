add_executable(test_schema_validate
    test_schema_validate.cpp
)

target_link_libraries(test_schema_validate PRIVATE
    sappp_common
    GTest::gtest_main
)

target_compile_definitions(test_schema_validate PRIVATE
    SAPPP_SCHEMA_DIR=\"${CMAKE_SOURCE_DIR}/schemas\"
)

include(GoogleTest)
gtest_discover_tests(test_schema_validate)
