project (sysuc)

find_package(antlr4-runtime 4.13 REQUIRED)
include_directories(${antlr4-runtime-include_DIR})

find_package(LLVM 18.1 REQUIRED)
llvm_map_components_to_libnames(LLVM_LIBS core support transformutils irreader passes)
include_directories(${llvm-include_DIR})

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(antlr) # lexer parser
add_subdirectory(asg)

file(GLOB PROJECT_SOURCES ${PROJECT_SOURCE_DIR}/*.cpp)
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

target_link_libraries(${PROJECT_NAME} antlr)
target_link_libraries(${PROJECT_NAME} asg)

target_link_libraries(${PROJECT_NAME} antlr4_static)
target_link_libraries(${PROJECT_NAME} ${LLVM_LIBS})
