# End-to-end tests
# Litmus tests for div0, null, oob

add_subdirectory(litmus_div0)
add_subdirectory(litmus_null)
add_subdirectory(litmus_oob)

if(SAPPP_BUILD_CLANG_FRONTEND)
    add_subdirectory(determinism_jobs)
endif()
