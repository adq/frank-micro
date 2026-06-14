# not needed as part of regular build
if (0)
    add_executable(gen_tables gen_tables.cpp)
    target_include_directories(gen_tables PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../..)
endif()
