# Report helpers library
add_library(sappp_report
    report.cpp
)

target_include_directories(sappp_report PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(sappp_report PUBLIC nlohmann_json::nlohmann_json)

sappp_target_strict_warnings(sappp_report)
