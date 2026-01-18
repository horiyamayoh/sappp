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
# Suppress valijson's legacy CMake deprecation warning to keep warning-zero policy.
set(_sappp_warn_deprecated_was_set FALSE)
if(DEFINED CMAKE_WARN_DEPRECATED)
    set(_sappp_warn_deprecated_was_set TRUE)
    set(_sappp_old_warn_deprecated "${CMAKE_WARN_DEPRECATED}")
endif()
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(valijson)
if(_sappp_warn_deprecated_was_set)
    set(CMAKE_WARN_DEPRECATED "${_sappp_old_warn_deprecated}" CACHE BOOL "" FORCE)
else()
    unset(CMAKE_WARN_DEPRECATED CACHE)
endif()

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
