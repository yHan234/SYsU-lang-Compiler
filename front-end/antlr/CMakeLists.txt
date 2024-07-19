project (antlr)

file(GLOB PROJECT_SOURCES ${PROJECT_SOURCE_DIR}/*.cpp)
add_library(${PROJECT_NAME} ${PROJECT_SOURCES})
target_link_libraries(${PROJECT_NAME} antlr4_static)
