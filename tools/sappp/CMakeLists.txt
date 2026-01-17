add_executable(sappp
    main.cpp
)

target_link_libraries(sappp PRIVATE
    sappp_common
    sappp_canonical
    sappp_build_capture
    nlohmann_json::nlohmann_json
)

install(TARGETS sappp RUNTIME DESTINATION bin)
