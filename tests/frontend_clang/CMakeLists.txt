add_executable(test_frontend_clang
    test_frontend_clang.cpp
)

target_link_libraries(test_frontend_clang PRIVATE
    sappp_frontend_clang
    sappp_canonical
    sappp_common
    GTest::gtest_main
)

target_compile_definitions(test_frontend_clang PRIVATE
    SAPPP_SCHEMA_DIR="${CMAKE_SOURCE_DIR}/schemas"
    SAPPP_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
)

sappp_register_gtest(test_frontend_clang frontend_clang)
