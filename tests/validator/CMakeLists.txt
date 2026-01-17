add_executable(test_validator
    test_validator.cpp
)

sappp_target_strict_warnings(test_validator)

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

sappp_register_gtest(test_validator validator)
