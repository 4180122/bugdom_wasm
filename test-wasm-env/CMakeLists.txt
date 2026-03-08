cmake_minimum_required(VERSION 3.16)
project(wasm_env_test C)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(wasm_env_test hello.c)

if(EMSCRIPTEN)
    target_link_options(wasm_env_test PRIVATE
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/data@/test"
        "SHELL:-s EXIT_RUNTIME=1"
        "SHELL:-o wasm_env_test.html"
    )
endif()
