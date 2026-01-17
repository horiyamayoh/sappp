add_library(sappp_canonical
    canonical_json.cpp
)

sappp_target_strict_warnings(sappp_canonical)

target_include_directories(sappp_canonical PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(sappp_canonical PUBLIC
    sappp_common
    nlohmann_json::nlohmann_json
)
