file(GLOB_RECURSE TESTS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.cpp)

foreach(FILE ${TESTS})
  string(REGEX REPLACE "[./]" "_" NAME ${FILE})
    add_executable(${NAME} ${FILE})
    add_test(NAME ${NAME} COMMAND ${NAME})
    target_link_libraries(${NAME} BlazeLib)
endforeach(FILE ${TESTS})
include_directories(..)