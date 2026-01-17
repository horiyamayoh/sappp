add_library(sappp_canonical
    canonical_json.cpp
)

target_include_directories(sappp_canonical PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(sappp_canonical PUBLIC
    sappp_common
    nlohmann_json::nlohmann_json
)
