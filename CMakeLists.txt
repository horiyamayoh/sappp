cmake_minimum_required(VERSION 3.16)
project(sappp
    VERSION 0.1.0
    DESCRIPTION "Sound, Static Absence-Proving Analyzer for C++"
    LANGUAGES C CXX
)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(SAPPP_BUILD_TESTS "Build tests" ON)
option(SAPPP_BUILD_DOCS "Build documentation" OFF)
option(SAPPP_BUILD_CLANG_FRONTEND "Build Clang frontend (requires LLVM/Clang)" OFF)
option(SAPPP_WERROR "Treat warnings as errors" OFF)

# Export compile_commands.json for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Compiler warnings - strict settings for code quality
# Note: These flags are applied globally but third-party libraries are compiled
# with SYSTEM includes to suppress warnings from them.
set(SAPPP_CXX_FLAGS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    list(APPEND SAPPP_CXX_FLAGS
        -Wall
        -Wextra
        -Wpedantic
        -Werror=return-type
        # Additional strict warnings
        -Wshadow                    # Warn when a variable shadows another
        -Wnon-virtual-dtor          # Warn if a class with virtual functions has non-virtual dtor
        -Wold-style-cast            # Warn on C-style casts
        -Wcast-align                # Warn on potential alignment issues
        -Wunused                    # Warn on anything unused
        -Woverloaded-virtual        # Warn if a virtual function is hidden
        -Wconversion                # Warn on implicit type conversions
        -Wsign-conversion           # Warn on sign conversions
        -Wnull-dereference          # Warn on null pointer dereference
        -Wdouble-promotion          # Warn when float is implicitly promoted to double
        -Wformat=2                  # Strict printf/scanf format checking
        -Wimplicit-fallthrough      # Warn on fallthrough in switch without annotation
        # Even stricter warnings
        -Wcast-qual                 # Warn on cast removing qualifiers (const, volatile)
        -Wzero-as-null-pointer-constant  # Warn on using 0 instead of nullptr
        -Wextra-semi                # Warn on redundant semicolons
        -Wmissing-declarations      # Warn if global function defined without declaration
        -Wswitch-enum               # Warn if switch on enum doesn't handle all values
        -Wundef                     # Warn if undefined macro is used in #if
        -Wfloat-equal               # Warn on direct float equality comparison
        -Wpointer-arith             # Warn on suspicious pointer arithmetic
        -Wwrite-strings             # Warn on deprecated conversion from string literal
        -Wdisabled-optimization     # Warn when optimization is disabled due to code complexity
        -Winit-self                 # Warn on uninitialized self-assignment
        -Wstrict-overflow=2         # Warn on strict overflow (level 2)
    )
    # GCC-specific warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        list(APPEND SAPPP_CXX_FLAGS
            -Wduplicated-cond           # Warn on duplicated conditions
            -Wduplicated-branches       # Warn on duplicated branches
            -Wlogical-op                # Warn on suspicious logical operations
            -Wuseless-cast              # Warn on useless casts
            -Wsuggest-attribute=pure    # Suggest pure attribute
            -Wsuggest-attribute=const   # Suggest const attribute
            -Wsuggest-attribute=noreturn # Suggest noreturn attribute
            -Wsuggest-override          # Suggest override specifier
            -Warray-bounds=2            # Stricter array bounds checking
            -Wattribute-alias=2         # Warn on attribute alias issues
            -Wtrampolines               # Warn on trampoline generation (security)
        )
    endif()
    # Clang-specific warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND SAPPP_CXX_FLAGS
            -Wmost                      # Most warnings
            -Wthread-safety             # Thread safety analysis
            -Wunreachable-code-aggressive # Aggressive unreachable code detection
            -Wno-c++98-compat           # Disable C++98 compatibility warnings
            -Wno-c++98-compat-pedantic  # Disable C++98 pedantic warnings
        )
    endif()
    if(SAPPP_WERROR)
        list(APPEND SAPPP_CXX_FLAGS -Werror)
    endif()
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    list(APPEND SAPPP_CXX_FLAGS
        /W4                         # High warning level
        /permissive-                # Strict conformance mode
        /w14640                     # Warn on thread-unsafe static member init
        /w14826                     # Warn on narrowing conversion
        /w14905                     # Wide string literal cast to LPSTR
        /w14906                     # String literal cast to LPWSTR
        /w14928                     # Illegal copy-initialization
        /w14242                     # Conversion from 'type1' to 'type2', possible loss of data
        /w14254                     # Operator conversion, possible loss of data
        /w14263                     # Member function does not override any base class virtual
        /w14265                     # Class has virtual functions but destructor not virtual
        /w14287                     # Unsigned/negative constant mismatch
        /w14289                     # Loop control variable used outside for-loop scope
        /w14296                     # Expression is always true/false
        /w14311                     # Pointer truncation from 'type' to 'type'
        /w14545                     # Expression before comma evaluates to function
        /w14546                     # Function call before comma missing argument list
        /w14547                     # Operator before comma has no effect
        /w14549                     # Operator before comma has no effect
        /w14555                     # Expression has no effect
        /w14619                     # #pragma warning: nonexistent warning number
        /w14946                     # Reinterpret_cast between related classes
        /w15038                     # Data member not initialized in declaration order
    )
    if(SAPPP_WERROR)
        list(APPEND SAPPP_CXX_FLAGS /WX)
    endif()
endif()

# Function to apply strict warnings to a target
function(sappp_target_strict_warnings target)
    target_compile_options(${target} PRIVATE ${SAPPP_CXX_FLAGS})
endfunction()

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
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install schemas
install(DIRECTORY schemas/
    DESTINATION share/sappp/schemas
    FILES_MATCHING PATTERN "*.schema.json"
)
