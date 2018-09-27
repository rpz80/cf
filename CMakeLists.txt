cmake_minimum_required(VERSION 2.8)
project("cf")
if (MSVC)
  set(CMAKE_CXX_FLAGS "/EHsc /bigobj")
else()
  set(NON_MSVC_CXX_FLAGS "-std=c++1y -pedantic -Werror -Wextra -Wall")
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${NON_MSVC_CXX_FLAGS} -stdlib=libc++")
  elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 4.9)
      set(CMAKE_CXX_FLAGS "${NON_MSVC_CXX_FLAGS}")
    else ()
      set(CMAKE_CXX_FLAGS "-std=c++1y")
    endif()
  endif()

  find_package (Threads)
endif()

set(INCLUDES "${CMAKE_SOURCE_DIR}")

include_directories(${INCLUDES})

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory("test")
