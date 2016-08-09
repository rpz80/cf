cmake_minimum_required(VERSION 2.8)
project("cf-root")
if (MSVC)
  set(CMAKE_CXX_FLAGS "/EHsc /bigobj")
else()
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "-std=c++14 -pedantic -stdlib=libc++")
  elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "-std=c++14 -pedantic")
  endif()

  find_package (Threads)
endif()

set(INCLUDES "${CMAKE_SOURCE_DIR}")

include_directories(${INCLUDES})

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory("test")
add_subdirectory("cf")
