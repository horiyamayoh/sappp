add_executable(test_determinism
    test_sha256.cpp
    test_path.cpp
    test_canonical_json.cpp
    test_certstore.cpp
    test_po_determinism.cpp
    test_e2e_determinism.cpp
)

sappp_target_strict_warnings(test_determinism)

target_link_libraries(test_determinism PRIVATE
    sappp_common
    sappp_canonical
    sappp_certstore
    GTest::gtest_main
)

target_compile_definitions(test_determinism PRIVATE
    SAPPP_SCHEMA_DIR=\"${CMAKE_SOURCE_DIR}/schemas\"
    SAPPP_TEST_SAPPP_BIN=\"$<TARGET_FILE:sappp>\"
)

if(SAPPP_BUILD_CLANG_FRONTEND)
    target_compile_definitions(test_determinism PRIVATE SAPPP_HAS_CLANG_FRONTEND=1)
endif()

sappp_register_gtest(test_determinism determinism)
