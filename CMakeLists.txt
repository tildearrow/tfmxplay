cmake_minimum_required(VERSION 3.0)
project(turtest)

# are you kidding me?! macOS 10.15 and still no C++11 by default?!
set(CMAKE_CXX_STANDARD 11)

find_library(HAVE_SDL2 SDL2)
find_library(HAVE_DRM drm)

add_executable(turtest src/main.cpp)
add_executable(tfmxplay src/audio/blip_buf.c src/audio/sinc.cpp src/audio/tfmx.cpp src/audio/tfmxplay.cpp src/time.cpp)

if (HAVE_DRM)
  target_compile_definitions(tfmxplay PUBLIC _SYNC_VBLANK)
  target_link_libraries(tfmxplay ${HAVE_DRM})
endif()

target_link_libraries(tfmxplay ${HAVE_SDL2})
