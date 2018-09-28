project("cf_test")

file(GLOB CF_HEADERS "../cf/*.h")
set(SRC "future.cpp")
add_executable(${PROJECT_NAME} ${CF_HEADERS} ${SRC})
target_link_libraries(${PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})
