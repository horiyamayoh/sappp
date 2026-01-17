add_library(sappp_common
    sha256.cpp
    path.cpp
    schema_validate.cpp
)

include(FetchContent)
FetchContent_Declare(
    valijson
    GIT_REPOSITORY https://github.com/tristanpenman/valijson.git
    GIT_TAG v1.0.0
)
FetchContent_MakeAvailable(valijson)

target_include_directories(sappp_common PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${valijson_SOURCE_DIR}/include
)

target_link_libraries(sappp_common PUBLIC
    nlohmann_json::nlohmann_json
)
