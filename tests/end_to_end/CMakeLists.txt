# End-to-end tests
# Litmus tests for div0, null, oob

add_subdirectory(litmus_div0)
add_subdirectory(litmus_null)
add_subdirectory(litmus_oob)

if(SAPPP_BUILD_CLANG_FRONTEND)
    add_executable(test_litmus_e2e
        test_litmus_e2e.cpp
    )

    sappp_target_strict_warnings(test_litmus_e2e)

    target_link_libraries(test_litmus_e2e PRIVATE
        GTest::gtest_main
        nlohmann_json::nlohmann_json
    )

    target_compile_definitions(test_litmus_e2e PRIVATE
        SAPPP_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\"
        SAPPP_BIN_DIR=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}\"
    )

    add_dependencies(test_litmus_e2e sappp)

    sappp_register_gtest(test_litmus_e2e slow)
endif()
