add_library(sector_read INTERFACE)

target_sources(sector_read INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/sector_read.c
        ${CMAKE_CURRENT_LIST_DIR}/xip_stream.c
        )

target_include_directories(sector_read INTERFACE ${CMAKE_CURRENT_LIST_DIR})
if (TARGET hardware_dma)
    target_link_libraries(sector_read INTERFACE hardware_dma)
endif ()
target_link_libraries(sector_read INTERFACE pico_util_buffer)
