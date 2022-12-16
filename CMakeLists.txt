cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
project(Blaze VERSION 0.0.0 LANGUAGES C CXX)

add_library(BlazeLib 
    buffer.hh
    buffer.cpp
    httpparser.hh
    httpparser.cpp
    transport.hh
    transport.cpp
    httpserver.hh
    httpserver.cpp
    pool.hh
)
target_include_directories(BlazeLib PUBLIC "${CMAKE_CURRENT_SROUCE_DIR}")

add_executable(Blaze server.cpp)
find_library(IO_URING uring)
target_link_libraries(Blaze BlazeLib ${IO_URING})

include(FetchContent)

FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.12.x)

FetchContent_Declare(googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG main) 

FetchContent_MakeAvailable(
        googletest
        googlebenchmark)

include(CTest)
add_subdirectory(tests)
add_subdirectory(benchs)