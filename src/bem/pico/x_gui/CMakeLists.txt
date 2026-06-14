add_library(x_gui INTERFACE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# todo not really required

target_sources(x_gui INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/x_gui.cpp
        ${CMAKE_CURRENT_LIST_DIR}/texture_basic.cpp
        ${CMAKE_CURRENT_LIST_DIR}/font.cpp
        ${CMAKE_CURRENT_LIST_DIR}/font_data.c
        )

target_compile_definitions(x_gui INTERFACE
        X_GUI=1
        #FORCE_44100_OUTPUT=1
        DISPLAY_MENU=1 # nothing loaded without
        PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1)

target_include_directories(x_gui INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(x_gui INTERFACE pico_stdlib sector_read pthread xcb X11 epoxy)

find_package(ALSA REQUIRED)

target_link_libraries(x_gui INTERFACE ${ALSA_LIBRARY})

if (DRM_PRIME)
    find_package(Libdrm REQUIRED)
    find_package(X11_XCB REQUIRED)
    find_package(XCB_DRI3 REQUIRED)

    target_sources(x_gui INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/texture_drm_prime.cpp
            )

    target_link_libraries(x_gui INTERFACE ${Libdrm_LIBRARIES} ${X11_XCB_LIBRARIES} ${XCB_DRI3_LIBRARIES})
    target_include_directories(x_gui INTERFACE ${Libdrm_INCLUDE_DIRS} ${X11_XCB_INCLUDE_DIR} ${XCB_DRI3_INCLUDE_DIR})

    target_compile_definitions(x_gui INTERFACE TEXTURE_TYPE=drm_prime DRM_PRIME=1)
else ()
endif ()

find_Package(PNG)
if (PNG_FOUND)
    add_executable(png_convert
            png_convert.c)
    target_include_directories(png_convert PRIVATE ${PNG_INCLUDE_DIRS})
    target_link_libraries(png_convert ${PNG_LIBRARIES})
endif ()
