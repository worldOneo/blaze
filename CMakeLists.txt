cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
project(Blaze VERSION 0.0.0 LANGUAGES C CXX)

add_executable(Blaze
    buffer.hh
    httpparser.cpp
    transport.cpp
)
find_library(IO_URING uring)
target_link_libraries(Blaze ${IO_URING})

include(CTest)
add_subdirectory(tests)