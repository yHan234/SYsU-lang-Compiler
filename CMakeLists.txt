cmake_minimum_required(VERSION 3.20)

project(SYsU-compiler)

include(config.cmake)
include(CTest)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_subdirectory(front-end)
add_subdirectory(test)
