cmake_minimum_required(VERSION 3.20)

project(SYsU-compiler)

include(config.cmake)
include(CTest)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

find_package(LLVM 18.1 REQUIRED)
llvm_map_components_to_libnames(LLVM_LIBS core support transformutils irreader passes)

add_subdirectory(front-end)
add_subdirectory(optimizer)
add_subdirectory(test)
