add_library(sappp_certstore
    certstore.cpp
)

target_include_directories(sappp_certstore PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(sappp_certstore PUBLIC
    sappp_canonical
    nlohmann_json::nlohmann_json
)
