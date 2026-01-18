cmake_minimum_required(VERSION 3.16)
project(sappp
    VERSION 0.1.0
    DESCRIPTION "Sound, Static Absence-Proving Analyzer for C++"
    LANGUAGES C CXX
)

# C++23 required
set(CMAKE_CXX_STANDARD 23)
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
#
# AI Agent Note: These warnings enforce better C++ coding practices.
# Violations will be flagged during compilation. Fix all warnings before commit.

set(SAPPP_CXX_FLAGS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    list(APPEND SAPPP_CXX_FLAGS
        -Wall
        -Wextra
        -Wpedantic
        
        # =========================================================================
        # Critical warnings (always errors) - prevents common bugs
        # =========================================================================
        -Werror=return-type             # Missing return statement
        -Werror=uninitialized           # Use of uninitialized variable
        -Werror=init-self               # Self-initialization (int x = x)
        -Werror=switch                  # Missing case in switch
        -Werror=implicit-fallthrough    # Fallthrough without annotation
        -Werror=delete-non-virtual-dtor # Delete through non-virtual dtor
        -Werror=odr                     # One Definition Rule violation
        
        # =========================================================================
        # Type safety warnings - prevent implicit conversions that lose data
        # =========================================================================
        -Wconversion                    # Implicit type conversions
        -Wsign-conversion               # Sign conversions
        -Wfloat-conversion              # Float to int conversion
        -Wnarrowing                     # Narrowing conversions in initializers
        -Wdouble-promotion              # Float implicitly promoted to double
        -Wfloat-equal                   # Direct float equality comparison
        
        # =========================================================================
        # Memory safety warnings - prevent buffer overflows and leaks
        # =========================================================================
        -Wnull-dereference              # Null pointer dereference
        -Warray-bounds                  # Array bounds checking
        -Wsizeof-pointer-memaccess      # sizeof(ptr) in memset/memcpy
        -Wvla                           # Variable length arrays (stack overflow risk)
        -Walloca                        # alloca usage (stack overflow risk)
        
        # =========================================================================
        # C++ best practices - enforce modern C++ idioms
        # =========================================================================
        -Wshadow                        # Variable shadows another
        -Wnon-virtual-dtor              # Class with virtual functions has non-virtual dtor
        -Wold-style-cast                # C-style casts
        -Wcast-align                    # Potential alignment issues
        -Wcast-qual                     # Cast removes qualifiers (const, volatile)
        -Woverloaded-virtual            # Virtual function is hidden
        -Wzero-as-null-pointer-constant # Using 0 instead of nullptr
        -Wmissing-declarations          # Global function without declaration
        -Wredundant-decls               # Redundant declarations
        
        # =========================================================================
        # Code quality warnings - improve readability and maintainability
        # =========================================================================
        -Wunused                        # Anything unused
        -Wunused-result                 # Ignoring return value (important for Result<T>)
        -Wextra-semi                    # Redundant semicolons
        -Wmissing-field-initializers    # Missing field initializers
        -Wmissing-braces                # Missing braces in initializers
        -Wformat=2                      # Strict printf/scanf format checking
        -Wswitch-enum                   # Switch doesn't handle all enum values
        -Wswitch-default                # Switch has no default case
        -Wundef                         # Undefined macro in #if
        
        # =========================================================================
        # Reproducibility warnings - for deterministic builds
        # =========================================================================
        -Wdate-time                     # __DATE__ __TIME__ usage (breaks reproducibility)
        
        # =========================================================================
        # Additional strictness
        # =========================================================================
        -Wpointer-arith                 # Suspicious pointer arithmetic
        -Wwrite-strings                 # Deprecated string literal conversion
        -Wdisabled-optimization         # Optimization disabled due to complexity
        -Wpacked                        # Packed attribute problems
        -Winline                        # Inline function cannot be inlined
        -Wtype-limits                   # Type limits (comparison always true/false)
        -Wparentheses                   # Suspicious parentheses
        -Wsequence-point                # Undefined sequence point behavior
        -Wdangling-else                 # Dangling else
    )
    
    # GCC-specific warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        list(APPEND SAPPP_CXX_FLAGS
            # Type safety (GCC-specific)
            -Warith-conversion              # Arithmetic conversion issues
            
            # Additional strictness (GCC-specific)
            -Wstrict-overflow=2             # Strict overflow (level 2)
            -Wshift-overflow=2              # Shift overflow (level 2)
            
            # Logic errors
            -Wduplicated-cond               # Duplicated conditions
            -Wduplicated-branches           # Duplicated branches
            -Wlogical-op                    # Suspicious logical operations
            
            # C++ best practices (GCC)
            -Wuseless-cast                  # Useless casts
            -Wsuggest-override              # Suggest override specifier
            -Wnoexcept                      # Missing noexcept
            -Weffc++                        # Effective C++ violations
            -Wctor-dtor-privacy             # Useless ctor/dtor
            -Wstrict-null-sentinel          # Strict null sentinel
            
            # Memory safety (GCC)
            -Warray-bounds=2                # Stricter array bounds
            -Wstringop-overflow=4           # String operation overflow
            -Wstringop-truncation           # String truncation
            -Wclass-memaccess               # memset/memcpy on non-trivial class
            
            # Format safety (GCC)
            -Wformat-overflow=2             # Format string overflow
            -Wformat-truncation=2           # Format string truncation
            -Wnonnull                       # NULL passed to nonnull param
            -Wnonnull-compare               # nonnull comparison
            -Walloc-zero                    # Zero-size allocation
            
            # Optimization hints
            -Wsuggest-attribute=pure        # Suggest pure attribute
            -Wsuggest-attribute=const       # Suggest const attribute
            -Wsuggest-attribute=noreturn    # Suggest noreturn attribute
            -Wsuggest-final-types           # Suggest final on types
            -Wsuggest-final-methods         # Suggest final on methods
            
            # Security
            -Wtrampolines                   # Trampoline generation
            -Wattribute-alias=2             # Attribute alias issues
            
            # Possibly uninitialized (GCC only, ignored by Clang)
            -Wmaybe-uninitialized
        )
    endif()
    
    # Clang-specific warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND SAPPP_CXX_FLAGS
            -Wmost                          # Most warnings
            
            # Additional strictness (Clang equivalent)
            -Wshift-overflow                # Shift overflow (Clang doesn't support =2)
            
            # Thread safety (Clang has better analysis)
            -Wthread-safety                 # Thread safety analysis
            -Wthread-safety-negative        # Negative thread safety analysis
            -Wthread-safety-beta            # Beta thread safety features
            
            # Code quality (Clang)
            -Wunreachable-code-aggressive   # Aggressive unreachable code detection
            -Wloop-analysis                 # Loop analysis warnings
            -Wrange-loop-analysis           # Range loop analysis
            
            # Move semantics (Clang)
            -Wmove                          # Suspicious move operations
            -Wredundant-move                # Redundant std::move
            -Wself-move                     # Self-move
            -Wreturn-std-move               # Missed return std::move
            -Wpessimizing-move              # Pessimizing move
            
            # Ownership analysis (Clang)
            -Wconsumed                      # Consumed analysis (uniqueness checking)
            
            # Atomics (Clang)
            -Watomic-implicit-seq-cst       # Implicit sequential consistency
            
            # Style (Clang)
            -Wextra-semi-stmt               # Extra semicolons in statements
            -Wnewline-eof                   # Missing newline at end of file
            -Wheader-hygiene                # Header hygiene issues
            -Widiomatic-parentheses         # Non-idiomatic parentheses
            
            # Disable C++98 compatibility (we use C++23)
            -Wno-c++98-compat
            -Wno-c++98-compat-pedantic
            -Wno-c++11-compat
            -Wno-c++14-compat
            -Wno-c++17-compat
            -Wno-c++20-compat
            
            # GNU extensions (warn if used)
            -Wgnu
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
