add_library(sappp_common
    sha256.cpp
    path.cpp
)

target_include_directories(sappp_common PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(sappp_common PUBLIC
    nlohmann_json::nlohmann_json
)
