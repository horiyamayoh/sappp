# Report library
add_library(sappp_report
    explain.cpp
)

sappp_target_strict_warnings(sappp_report)

target_include_directories(sappp_report PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(sappp_report PUBLIC
    sappp_common
    sappp_canonical
    nlohmann_json::nlohmann_json
)
