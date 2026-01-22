add_library(sappp_specdb
    specdb.cpp
)

sappp_target_strict_warnings(sappp_specdb)

target_include_directories(sappp_specdb PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(sappp_specdb PUBLIC
    sappp_common
    sappp_canonical
    nlohmann_json::nlohmann_json
)
