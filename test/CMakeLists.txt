cmake_minimum_required(VERSION 3.0)
project("cf_test")

set(SRC "future.cpp" "../cf/cfuture.h")
add_executable(${PROJECT_NAME} ${SRC})
