# Find or fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

set(SAPPP_QUICK_LABELS
    build_capture
    determinism
    po
    schemas
    validator
)

function(sappp_register_gtest target label)
    set(labels "${label}")
    if (label IN_LIST SAPPP_QUICK_LABELS)
        list(APPEND labels quick)
    endif()

    gtest_discover_tests(${target}
        DISCOVERY_MODE PRE_TEST
        TEST_PREFIX "${label}."
        PROPERTIES LABELS "${labels}"
    )
endfunction()

# Schema validation tests
add_subdirectory(schemas)

# Build capture tests
add_subdirectory(build_capture)

# Determinism tests
add_subdirectory(determinism)

# End-to-end tests
add_subdirectory(end_to_end)

# PO generator tests
add_subdirectory(po)

# Validator tests
add_subdirectory(validator)

# Report tests
add_subdirectory(report)

# Frontend (Clang) tests
if(SAPPP_BUILD_CLANG_FRONTEND)
    add_subdirectory(frontend_clang)
endif()
