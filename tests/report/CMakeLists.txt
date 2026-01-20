add_executable(test_report
    test_report.cpp
)

sappp_target_strict_warnings(test_report)

target_link_libraries(test_report PRIVATE
    sappp_report
    sappp_common
    GTest::gtest_main
)

sappp_register_gtest(test_report report)
