add_library(usb_host_hid INTERFACE)

target_sources(usb_host_hid INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/usb_host_hid.c
)

target_include_directories(usb_host_hid INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(usb_host_hid INTERFACE pico_stdlib tinyusb_host)
