cmake_minimum_required(VERSION 2.8)
project(share_disk)

include_directories(src)
include_directories(plog/include)

file(GLOB SRC_SOURCES src/*.cpp)
add_executable(main ${SRC_SOURCES})
target_compile_options(main PUBLIC --std=c++14 -Wall)
