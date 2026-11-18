cmake_minimum_required(VERSION 3.18)

project(remote_mouse_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SDL2 sources are fetched by scripts/fetch-sdl2.ps1 (or .sh) into
# third_party/SDL2. Five levels up from android/app/src/main/cpp.
set(MCTL_SDL2_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../third_party/SDL2")

if(NOT EXISTS "${MCTL_SDL2_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "SDL2 was not found at ${MCTL_SDL2_DIR}.\n"
        "Run scripts/fetch-sdl2.ps1 (Windows) or scripts/fetch-sdl2.sh "
        "(macOS/Linux) and rebuild.")
endif()

# Build SDL2 as a shared library, without its test programs or install rules.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)

add_subdirectory("${MCTL_SDL2_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/sdl2")

# The activity loads "libSDL2.so" and then "libmain.so" (see SDLActivity).
add_library(main SHARED
    main.cpp
    discovery.cpp
    net.cpp
    server_share.cpp
    touchpad.cpp
    ui.cpp
)

target_include_directories(main PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../shared"
)

target_link_libraries(main PRIVATE
    SDL2::SDL2
    SDL2::SDL2main
    android
    log
)

if(NOT MSVC)
    target_compile_options(main PRIVATE -Wall -Wextra -Wpedantic)
endif()
