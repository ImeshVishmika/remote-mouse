cmake_minimum_required(VERSION 3.20)

project(RemoteMouse VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(REMOTE_MOUSE_BUILD_TESTS "Build the protocol test suite" ON)

# Wire protocol + framing, shared with the Android build.
add_library(mctl_protocol INTERFACE)
target_include_directories(mctl_protocol INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/shared")
target_compile_features(mctl_protocol INTERFACE cxx_std_17)

# Socket helpers (Windows Winsock / POSIX) used by the server and the simulator.
add_library(mctl_net STATIC
    desktop/src/connection.cpp
    desktop/src/net_types.cpp
)
target_include_directories(mctl_net PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/desktop/src")
target_link_libraries(mctl_net PUBLIC mctl_protocol)
if(WIN32)
    target_link_libraries(mctl_net PUBLIC ws2_32)
endif()

function(mctl_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(WIN32)
    add_subdirectory(desktop)
    add_subdirectory(tools)
else()
    message(STATUS "The desktop server targets Windows; skipping desktop/tools on this host.")
endif()

if(REMOTE_MOUSE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
