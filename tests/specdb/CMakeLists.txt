add_executable(specdb_tests
    test_specdb.cpp
)

sappp_target_strict_warnings(specdb_tests)

target_compile_definitions(specdb_tests PRIVATE
    SAPPP_SCHEMA_DIR="${CMAKE_SOURCE_DIR}/schemas"
)

target_link_libraries(specdb_tests PRIVATE
    sappp_specdb
    GTest::gtest_main
)

sappp_register_gtest(specdb_tests specdb)
