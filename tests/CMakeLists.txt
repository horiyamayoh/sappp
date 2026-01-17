enable_testing()

# Find or fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Schema validation tests
add_subdirectory(schemas)

# Build capture tests
add_subdirectory(build_capture)

# Determinism tests
add_subdirectory(determinism)

# End-to-end tests
add_subdirectory(end_to_end)
