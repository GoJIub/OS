cmake_minimum_required(VERSION 3.15)
project(Lab3_SharedMemory C)

set(CMAKE_C_STANDARD 11)

include_directories(${CMAKE_SOURCE_DIR})

set(SOURCES
    src/parent.c
    src/child.c
    src/utils.c
)

add_executable(parent.out src/parent.c src/utils.c)
add_executable(child.out src/child.c src/utils.c)
