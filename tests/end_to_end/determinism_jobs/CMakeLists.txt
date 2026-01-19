add_executable(test_end_to_end_determinism_jobs
    test_determinism_jobs.cpp
)

sappp_target_strict_warnings(test_end_to_end_determinism_jobs)

target_link_libraries(test_end_to_end_determinism_jobs PRIVATE
    sappp_certstore
)

add_dependencies(test_end_to_end_determinism_jobs sappp)

add_test(NAME end_to_end.determinism_jobs
    COMMAND test_end_to_end_determinism_jobs
        --sappp $<TARGET_FILE:sappp>
        --schema-dir ${CMAKE_SOURCE_DIR}/schemas
        --repo-root ${CMAKE_SOURCE_DIR}
        --compiler ${CMAKE_CXX_COMPILER}
)

set_tests_properties(end_to_end.determinism_jobs PROPERTIES
    LABELS "end_to_end;determinism"
)
