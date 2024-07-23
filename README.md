# SYsU-lang Compiler

这是一个用于学习编译原理和 LLVM 的项目。

本项目基于 [SYsU-lang2](https://github.com/arcsysu/SYsU-lang2) 的实验框架进行了修改，每个任务在上一个任务的代码上继续编写。

## 进度

* [x] task0: Init
* [x] task1: Lexer
* [x] task2: Parser
* [ ] task3: LLVM IR
* [ ] task4: Optimizer

### TODO

* 为 Parser 的输出的 JSON 添加更多信息。

## 笔记

### 为什么选择 SYsU-lang？

有详细的[实验文档](https://arcsysu.github.io/SYsU-lang2/)和[本地测试](https://github.com/arcsysu/SYsU-lang2/tree/master/test)，以及[顾宇浩的实验设计手册](https://github.com/arcsysu/SYsU-lang2/tree/master/docs/gyh-manual)也是很好的学习资料。

直接提供了[抽象语义图代码](https://github.com/arcsysu/SYsU-lang2/tree/master/task/2/common)，可以专心于编写 IR 间的转换，而不是各种数据结构的设计。

### 为什么使用 ANTLR？

因为比 flex + bison 易用，并且 ANTLR 官方提供了 [C.g4](https://github.com/antlr/grammars-v4/blob/master/c/C.g4) （但是有 bug ！）。

#### 本项目对 C.g4 的修改

1. `declaration` 中，将 `declarationSpecifiers initDeclaratorList? ';'` 改为 `declarationSpecifiers initDeclaratorList ';'`。
    
    防止将 `int a;` 中的 `a` 识别为 `declarationSpecifiers` 中的 `typedefName`。

2. `initializer` 中，将 `'{' initializerList ','? '}'` 改为 `'{' (initializerList ','?)? '}'`。
    
    以支持 `int a[3] = {};` 这种空初始化（Empty initialization）。

### 预处理器在源文件开头添加的 `# 1 "<built-in>" 1` 这类语句是什么？

[linemarkers](https://gcc.gnu.org/onlinedocs/cpp/Preprocessor-Output.html)
