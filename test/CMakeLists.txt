project("cf_test")

file(GLOB CF_HEADERS "../cf/*.h")
message("CF HEADErS: ${CF_HEADERS}")
set(SRC "future.cpp")
add_executable(${PROJECT_NAME} ${CF_HEADERS} ${SRC})
target_link_libraries(${PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})
