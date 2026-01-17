add_executable(test_build_capture
    test_build_capture.cpp
)

target_link_libraries(test_build_capture PRIVATE
    sappp_build_capture
    sappp_canonical
    sappp_common
    GTest::gtest_main
)

target_compile_definitions(test_build_capture PRIVATE
    SAPPP_SCHEMA_DIR=\"${CMAKE_SOURCE_DIR}/schemas\"
)

sappp_register_gtest(test_build_capture build_capture)
