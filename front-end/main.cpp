#include <fstream>
#include <iostream>

#include "antlr/CLexer.h"
#include "print_tokens.hpp"

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cout << "Usage: " << argv[0] << " <input> <output>\n";
        return -1;
    }

    std::ifstream inFile(argv[1]);
    if (!inFile)
    {
        std::cout << "Error: unable to open input file: " << argv[1] << '\n';
        return -2;
    }

    std::ofstream outFile(argv[2]);
    if (!outFile)
    {
        std::cout << "Error: unable to open output file: " << argv[2] << '\n';
        return -3;
    }

    std::cout << "程序 '" << argv[0] << std::endl;
    std::cout << "输入 '" << argv[1] << std::endl;
    std::cout << "输出 '" << argv[2] << std::endl;

    antlr4::ANTLRInputStream input(inFile);
    CLexer lexer(&input);

    antlr4::CommonTokenStream tokens(&lexer);
    tokens.fill();

    print_tokens_clang(tokens, outFile);
}