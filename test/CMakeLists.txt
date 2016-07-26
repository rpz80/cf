project("cf_test")

set(SRC "future.cpp" "../cf/cfuture.h")
add_executable(${PROJECT_NAME} ${SRC})
target_link_libraries(${PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})
