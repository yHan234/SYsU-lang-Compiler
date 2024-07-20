project (sysuc)

find_package(antlr4-runtime 4.13 REQUIRED)
include_directories(${antlr4-runtime-include_DIR})

# find_package(LLVM 18.1 REQUIRED)
# llvm_map_components_to_libnames(LLVM_LIBS core support transformutils irreader passes)

# find_package(antlr4-generator 4.13 REQUIRED)
# antlr4_generate(
#   C                                 # unique target name
#   ${CMAKE_CURRENT_SOURCE_DIR}/C.g4  # input file
#   BOTH                              # LEXER / PARSER / BOTH
#   FALSE                             # generate listener
#   FALSE                             # generate visitor
# )

add_subdirectory(antlr) # lexer parser

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB PROJECT_SOURCES ${PROJECT_SOURCE_DIR}/*.cpp)
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

target_link_libraries(${PROJECT_NAME} antlr)
