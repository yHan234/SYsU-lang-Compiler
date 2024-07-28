project (optim)

file(GLOB _src *.cpp *.hpp *.c *.h)
add_executable(${PROJECT_NAME} ${_src})

target_include_directories(${PROJECT_NAME} PRIVATE . ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${LLVM_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} ${LLVM_LIBS})
