cmake_minimum_required(VERSION 3.16)
project(sappp
    VERSION 0.1.0
    DESCRIPTION "Sound, Static Absence-Proving Analyzer for C++"
    LANGUAGES CXX
)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(SAPPP_BUILD_TESTS "Build tests" ON)
option(SAPPP_BUILD_DOCS "Build documentation" OFF)
option(SAPPP_BUILD_CLANG_FRONTEND "Build Clang frontend (requires LLVM/Clang)" OFF)

# Export compile_commands.json for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)
endif()

# Find dependencies (LLVM/Clang only if frontend enabled)
if(SAPPP_BUILD_CLANG_FRONTEND)
    find_package(LLVM REQUIRED CONFIG)
    find_package(Clang REQUIRED CONFIG)
    
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
    
    # Include LLVM/Clang headers
    include_directories(${LLVM_INCLUDE_DIRS})
    include_directories(${CLANG_INCLUDE_DIRS})
    add_definitions(${LLVM_DEFINITIONS})
endif()

# nlohmann/json - header only
# Option 1: System installed
# find_package(nlohmann_json 3.9 REQUIRED)
# Option 2: FetchContent (fallback)
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# Project include directory
include_directories(${CMAKE_SOURCE_DIR}/include)

# Libraries
add_subdirectory(libs/common)
add_subdirectory(libs/canonical)
add_subdirectory(libs/build_capture)
add_subdirectory(libs/ir)
add_subdirectory(libs/po)
if(SAPPP_BUILD_CLANG_FRONTEND)
    add_subdirectory(libs/frontend_clang)
endif()
add_subdirectory(libs/analyzer)
add_subdirectory(libs/certstore)
add_subdirectory(libs/validator)
add_subdirectory(libs/report)

# CLI tool
add_subdirectory(tools/sappp)

# Tests
if(SAPPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install schemas
install(DIRECTORY schemas/
    DESTINATION share/sappp/schemas
    FILES_MATCHING PATTERN "*.schema.json"
)
