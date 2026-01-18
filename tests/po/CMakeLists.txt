add_executable(test_po_generator
    test_po_generator.cpp
)

target_link_libraries(test_po_generator PRIVATE
    sappp_po
    sappp_common
    sappp_canonical
    nlohmann_json::nlohmann_json
    GTest::gtest_main
)

target_compile_definitions(test_po_generator PRIVATE
    SAPPP_SCHEMA_DIR="${CMAKE_SOURCE_DIR}/schemas"
)

sappp_register_gtest(test_po_generator po)
