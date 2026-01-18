add_library(sappp_po
    po.cpp
)

target_include_directories(sappp_po PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(sappp_po PUBLIC
    sappp_common
    sappp_canonical
    nlohmann_json::nlohmann_json
)
