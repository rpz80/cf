cmake_minimum_required(VERSION 2.8)
project("cf")

file(GLOB SRC "*.cpp" "*.h")
add_library(${PROJECT_NAME} ${SRC})
