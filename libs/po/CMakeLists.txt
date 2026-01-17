add_library(sappp_po
    po_generator.cpp
)

target_include_directories(sappp_po PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/libs/po
)

target_link_libraries(sappp_po PUBLIC
    sappp_common
    sappp_canonical
    nlohmann_json::nlohmann_json
)
