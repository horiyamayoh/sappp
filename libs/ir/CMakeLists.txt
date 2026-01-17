add_library(sappp_ir INTERFACE)

target_include_directories(sappp_ir INTERFACE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/libs/ir
)
