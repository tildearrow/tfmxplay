cmake_minimum_required(VERSION 3.0)
project(turtest)

include_directories(/usr/include/libdrm)

find_library(HAVE_SDL2 SDL2)

add_executable(turtest src/main.cpp)
add_executable(tfmxplay src/audio/blip_buf.c src/audio/tfmx.cpp src/audio/tfmxplay.cpp src/time.cpp)
target_link_libraries(tfmxplay ${HAVE_SDL2} drm)
