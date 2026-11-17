add_executable(remote_mouse_server
    src/main.cpp
    src/server.cpp
    src/mouse.cpp
    src/discovery.cpp
    src/config.cpp
    src/log.cpp
    src/platform.cpp
)

target_link_libraries(remote_mouse_server PRIVATE mctl_net)
target_include_directories(remote_mouse_server PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

mctl_enable_warnings(remote_mouse_server)

if(WIN32)
    # GetAdaptersAddresses() for the "Local IPv4" banner line.
    target_link_libraries(remote_mouse_server PRIVATE iphlpapi)
endif()

if(MINGW)
    # Static libgcc/libstdc++ so the exe runs without a MinGW runtime on PATH.
    target_link_options(remote_mouse_server PRIVATE -static-libgcc -static-libstdc++)
endif()
