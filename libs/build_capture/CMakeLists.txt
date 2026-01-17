add_library(sappp_build_capture
    build_capture.cpp
)

target_include_directories(sappp_build_capture PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(sappp_build_capture PUBLIC
    sappp_common
    sappp_canonical
    nlohmann_json::nlohmann_json
)
