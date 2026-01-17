add_library(sappp_validator
    validator.cpp
)

target_include_directories(sappp_validator PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/libs
)

target_link_libraries(sappp_validator PUBLIC
    sappp_common
    sappp_canonical
    nlohmann_json::nlohmann_json
)
