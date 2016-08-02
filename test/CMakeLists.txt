project("cf_test")

set(SRC "future.cpp")
add_executable(${PROJECT_NAME} ${SRC})
target_link_libraries(${PROJECT_NAME} cf ${CMAKE_THREAD_LIBS_INIT})
