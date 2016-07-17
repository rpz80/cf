cmake_minimum_required(VERSION 3.0)
project("cf")
if (MSVC)
  set(CMAKE_CXX_FLAGS "/EHsc")
else()
  set(CMAKE_CXX_FLAGS "-std=c++14 -Wall -pedantic")
endif()

set(INCLUDES "${CMAKE_SOURCE_DIR}")

include_directories(${INCLUDES})

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory("test")
