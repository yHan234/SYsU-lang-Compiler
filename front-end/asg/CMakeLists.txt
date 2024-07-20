project (asg)

file(GLOB PROJECT_SOURCES ${PROJECT_SOURCE_DIR}/*.cpp)
add_library(${PROJECT_NAME} ${PROJECT_SOURCES})
