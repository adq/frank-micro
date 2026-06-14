cmake_minimum_required(VERSION 3.13)
project(embed_tool C CXX)
set(CMAKE_CXX_STANDARD 17)

set(SRC_ROOT ../..)

add_executable(embed_tool
        main.cpp
        ${SRC_ROOT}/sdf-geo.c
)

target_include_directories(embed_tool PRIVATE
        ${SRC_ROOT}
        ${SRC_ROOT}/pico/stub_allegro5/include)

target_compile_definitions(embed_tool PRIVATE PICO_NO_HARDWARE=1)
target_link_libraries(embed_tool PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>)
