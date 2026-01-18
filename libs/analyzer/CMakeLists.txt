add_library(sappp_analyzer
    analyzer.cpp
)

sappp_target_strict_warnings(sappp_analyzer)

target_include_directories(sappp_analyzer PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(sappp_analyzer PUBLIC
    sappp_common
    sappp_canonical
    sappp_certstore
    nlohmann_json::nlohmann_json
)
