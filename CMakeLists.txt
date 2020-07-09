cmake_minimum_required(VERSION 3.0)
project(turtest)

add_executable(tfmxplay src/audio/blip_buf.c src/audio/tfmx.cpp src/audio/tfmxplay.cpp)
target_link_libraries(tfmxplay SDL2)
