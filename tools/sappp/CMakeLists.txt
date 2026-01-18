add_executable(sappp
    main.cpp
)

target_link_libraries(sappp PRIVATE
    sappp_common
    sappp_canonical
    sappp_build_capture
    sappp_po
    sappp_validator
    nlohmann_json::nlohmann_json
)

if(SAPPP_BUILD_CLANG_FRONTEND)
    target_link_libraries(sappp PRIVATE sappp_frontend_clang)
    target_compile_definitions(sappp PRIVATE SAPPP_HAS_CLANG_FRONTEND=1)
endif()

install(TARGETS sappp RUNTIME DESTINATION bin)
