# SYsU-lang Compiler

这是一个用于学习编译原理和 LLVM 的项目。

本项目重构了 [SYsU-lang2](https://github.com/arcsysu/SYsU-lang2) 的实验框架，将 5 个任务合并。

## 为什么选择 SYsU-lang

有详细的[实验文档](https://arcsysu.github.io/SYsU-lang2/)和[本地测试](https://github.com/arcsysu/SYsU-lang2/tree/master/test)。

直接提供了[抽象语义图代码](https://github.com/arcsysu/SYsU-lang2/tree/master/task/2/common)，可以专心于编写 IR 间的转换，而不是 AST/ASG 等数据结构的设计。

## 为什么使用 ANTLR

因为比 flex + bison 易用，并且 ANTLR 官方提供了 [C.g4](https://github.com/antlr/grammars-v4/blob/master/c/C.g4)。

## TODO

* [x] task0: Init
* [x] task1: Lexer
* [ ] task2: Parser
* [ ] task3: LLVM IR
* [ ] task4: Optimizer

## 笔记

* [顾宇浩的实验设计手册](https://github.com/arcsysu/SYsU-lang2/tree/master/docs/gyh-manual)
* 预处理器在源文件开头添加的 `# 1 "<built-in>" 1` 这类语句是什么？[linemarkers](https://gcc.gnu.org/onlinedocs/cpp/Preprocessor-Output.html)
