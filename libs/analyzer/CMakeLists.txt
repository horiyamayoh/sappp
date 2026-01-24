add_library(sappp_analyzer
    analyzer.cpp
)

sappp_target_strict_warnings(sappp_analyzer)

target_include_directories(sappp_analyzer PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/libs/analyzer
)

target_link_libraries(sappp_analyzer PUBLIC
    sappp_common
    sappp_certstore
    sappp_specdb
    sappp_canonical
    nlohmann_json::nlohmann_json
)
