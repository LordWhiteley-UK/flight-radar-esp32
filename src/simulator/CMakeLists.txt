cmake_minimum_required(VERSION 3.16)
project(flight_radar_sim C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)

# Homebrew (Apple Silicon) on the include/lib search path
list(APPEND CMAKE_PREFIX_PATH /opt/homebrew)

# ---- LVGL 8.4 (vendored) as a static lib, using our lv_conf.h ----
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE PATH "lv_conf.h" FORCE)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/lvgl__lvgl
    ${CMAKE_BINARY_DIR}/lvgl_build
    EXCLUDE_FROM_ALL)

# ---- SDL2 ----
find_package(SDL2 QUIET)
# Homebrew (Apple Silicon) always on the search path — some sdl2-compat keg
# configs expose SDL2::SDL2 without a usable include dir, so add it explicitly.
set(HOMEBREW_INC /opt/homebrew/include)
set(HOMEBREW_LIB /opt/homebrew/lib)
if(NOT EXISTS ${HOMEBREW_INC}/SDL2/SDL.h)
    message(FATAL_ERROR "SDL2 headers not found under /opt/homebrew/include/SDL2 — run `brew install sdl2`")
endif()

# ---- sources ----
set(MAIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../main)

set(CORE_SRCS
    ${MAIN_DIR}/radar.c
    ${MAIN_DIR}/opensky_client.c
    ${MAIN_DIR}/app_state.c)

file(GLOB_RECURSE SIM_UI_SRCS ${MAIN_DIR}/ui/*.c)

set(SIM_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl_main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl_display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform_sdl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/replay.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cjson/cJSON.c)

add_executable(flightradar ${CORE_SRCS} ${SIM_UI_SRCS} ${SIM_SRCS})

target_include_directories(flightradar PRIVATE
    ${MAIN_DIR}
    ${MAIN_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/cjson
    ${HOMEBREW_INC})

target_compile_options(flightradar PRIVATE
    -Wno-format -Wno-unused-function -Wno-unused-variable -Wno-attributes)

# link LVGL (propagates LV_CONF_INCLUDE_SIMPLE + include dirs) and SDL2
target_link_libraries(flightradar PRIVATE lvgl)
if(TARGET SDL2::SDL2)
    target_link_libraries(flightradar PRIVATE SDL2::SDL2)
    # belt-and-braces: ensure the SDL2 system include dir is on the path
    target_include_directories(flightradar PRIVATE ${HOMEBREW_INC})
else()
    # fallback: link the Homebrew dylib directly
    target_link_libraries(flightradar PRIVATE ${HOMEBREW_LIB}/libSDL2.dylib)
endif()