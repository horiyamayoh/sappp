add_library(sappp_frontend_clang
    frontend.cpp
)

if (LLVM_FOUND)
    llvm_map_components_to_libnames(LLVM_LIBS
        support
        core
        option
        irreader
    )
endif()

target_include_directories(sappp_frontend_clang PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_SOURCE_DIR}/libs/ir
)

target_link_libraries(sappp_frontend_clang PUBLIC
    sappp_common
    sappp_canonical
    sappp_ir
    nlohmann_json::nlohmann_json
    clangTooling
    clangFrontend
    clangSerialization
    clangDriver
    clangParse
    clangSema
    clangAST
    clangAnalysis
    clangEdit
    clangLex
    clangBasic
    clangRewrite
    clangFrontendTool
    ${LLVM_LIBS}
)
