add_executable(test_validator
    test_validator.cpp
)

target_link_libraries(test_validator PRIVATE
    sappp_common
    sappp_canonical
    sappp_certstore
    sappp_validator
    nlohmann_json::nlohmann_json
    GTest::gtest_main
)

target_compile_definitions(test_validator PRIVATE
    SAPPP_SCHEMA_DIR="${CMAKE_SOURCE_DIR}/schemas"
)

include(GoogleTest)
gtest_discover_tests(test_validator)
