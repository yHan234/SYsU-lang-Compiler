#pragma once

#include <fstream>

#include "antlr4-runtime.h"

#include "antlr/CLexer.h"

void print_tokens_clang(antlr4::CommonTokenStream &tokens, std::ofstream &outFile);

// for debug
void print_tokens_antlr(antlr4::CommonTokenStream &tokens, std::ofstream &outFile, const CLexer &lexer);
