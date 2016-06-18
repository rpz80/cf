cmake_minimum_required(VERSION 3.0)
project("cf")
set(CMAKE_CXX_FLAGS "-std=c++14 -Wall -pedantic")

set(INCLUDES "${CMAKE_SOURCE_DIR}")

include_directories(${INCLUDES})

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory("cf")
add_subdirectory("test")
