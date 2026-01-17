add_library(sappp_common
    sha256.cpp
    path.cpp
    schema_validate.cpp
)

sappp_target_strict_warnings(sappp_common)

include(FetchContent)
FetchContent_Declare(
    valijson
    GIT_REPOSITORY https://github.com/tristanpenman/valijson.git
    GIT_TAG v1.0.2
)
FetchContent_MakeAvailable(valijson)

# Use SYSTEM to suppress warnings from third-party headers
target_include_directories(sappp_common PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_include_directories(sappp_common SYSTEM PUBLIC
    ${valijson_SOURCE_DIR}/include
)

target_link_libraries(sappp_common PUBLIC
    nlohmann_json::nlohmann_json
)
