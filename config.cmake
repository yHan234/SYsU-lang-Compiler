# 实验一的日志级别，级别从低到高为0-3
set(TASK1_LOG_LEVEL 3)

# 是否在实验二复活，ON或OFF
# 由于没有加入实验一的输出（Token Stream）对应的 Parser，暂不启用实验二复活
set(TASK2_REVIVE OFF)
# 实验二的日志级别，级别从低到高为0-3
set(TASK2_LOG_LEVEL 3)

# 是否在实验三复活，ON或OFF
set(TASK3_REVIVE ON)

# 是否在实验四复活，ON或OFF
set(TASK4_REVIVE ON)

# ANTLR4
if(DEFINED ENV{ANTLR_DIR})
  message("ANTLR directory: $ENV{ANTLR_DIR}")
  set(antlr4-runtime_DIR "$ENV{ANTLR_DIR}/install/lib/cmake/antlr4-runtime")
  set(antlr4-generator_DIR "$ENV{ANTLR_DIR}/install/lib/cmake/antlr4-generator")
  set(ANTLR4_JAR_LOCATION "$ENV{ANTLR_DIR}/antlr-4.13.1-complete.jar")
  set(antlr4-runtime-include_DIR "$ENV{ANTLR_DIR}/install/include/antlr4-runtime")
else()
  set(antlr_DIR "${CMAKE_SOURCE_DIR}/third-party/antlr")
  message("ANTLR directory: ${antlr_DIR}")
  set(antlr4-runtime_DIR "${antlr_DIR}/install/lib/cmake/antlr4-runtime")
  set(antlr4-generator_DIR "${antlr_DIR}/install/lib/cmake/antlr4-generator")
  set(ANTLR4_JAR_LOCATION "${antlr_DIR}/antlr-4.13.1-complete.jar")
  set(antlr4-runtime-include_DIR "${antlr_DIR}/install/include/antlr4-runtime")
endif()

# llvm clang
if(DEFINED ENV{LLVM_DIR})
  message("LLVM directory: $ENV{LLVM_DIR}")
  set(LLVM_DIR "$ENV{LLVM_DIR}/install/lib/cmake/llvm")
  set(CLANG_EXECUTABLE "$ENV{LLVM_DIR}/install/bin/clang")
  set(CLANG_PLUS_EXECUTABLE "$ENV{LLVM_DIR}/install/bin/clang++")
else()
  set(llvm_DIR "${CMAKE_SOURCE_DIR}/third-party/llvm")
  message("LLVM directory: ${llvm_DIR}")
  set(LLVM_DIR "${llvm_DIR}/install/lib/cmake/llvm")
  set(CLANG_EXECUTABLE "${llvm_DIR}/install/bin/clang")
  set(CLANG_PLUS_EXECUTABLE "${llvm_DIR}/install/bin/clang++")
  endif()

# 测试运行时限（秒）
set(CTEST_TEST_TIMEOUT 3)

# 实验一排除测例名的正则式
set(TASK1_EXCLUDE_REGEX "^performance/.*")
# 实验一测例表，非空时忽略 EXCLUDE_REGEX
set(TASK1_CASES_TXT "")

# 实验二排除测例名的正则式
set(TASK2_EXCLUDE_REGEX "^performance/.*")
# 实验二测例表，非空时忽略 EXCLUDE_REGEX
set(TASK2_CASES_TXT "")

# 实验三排除测例名的正则式
set(TASK3_EXCLUDE_REGEX "^performance/.*")
# 实验三测例表，非空时忽略 EXCLUDE_REGEX
set(TASK3_CASES_TXT "")

# 实验四排除测例名的正则式
set(TASK4_EXCLUDE_REGEX "^functional-.*|^mini-performance/.*")
# 实验四测例表，非空时忽略 EXCLUDE_REGEX
set(TASK4_CASES_TXT "")