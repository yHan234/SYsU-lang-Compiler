# SYsU-lang Compiler

这是一个用于学习编译原理和 LLVM 的项目。

本项目重构了 [SYsU-lang2](https://github.com/arcsysu/SYsU-lang2) 的实验框架，将 5 个任务合并。

## 为什么选择 SYsU-lang

因为有详细的[实验文档](https://arcsysu.github.io/SYsU-lang2/)和本地测试。

## 为什么使用 ANTLR

因为比 flex + bison 易用，并且 ANTLR 官方提供了 [C.g4](https://github.com/antlr/grammars-v4/blob/master/c/C.g4)。

## TODO

* [x] task0: Init
* [x] task1: Lexer
* [ ] task2: Parser
* [ ] task3: LLVM IR
* [ ] task4: Optimizer

## 笔记

### 预处理器在源文件开头添加的 `# 1 "<built-in>" 1` 这类语句是什么？

[linemarkers](https://gcc.gnu.org/onlinedocs/cpp/Preprocessor-Output.html)
