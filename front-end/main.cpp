#include <fstream>
#include <iostream>

#include "antlr/CLexer.h"
#include "antlr/CParser.h"

#include "print_tokens.hpp"

#include "asg/Obj.hpp"
#include "asg/asg.hpp"
#include "asg/Typing.hpp"
#include "asg/Asg2Json.hpp"
#include "asg/Json2Asg.hpp"
#include "asg/EmitIR.hpp"
#include "Ast2Asg.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/Support/MemoryBuffer.h>

int main_task1(int argc, char *argv[])
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

    return 0;
}

int main_task2(int argc, char *argv[])
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

    std::error_code ec;
    llvm::StringRef outPath(argv[2]);
    llvm::raw_fd_ostream outFile(outPath, ec);
    if (ec)
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
    CParser parser(&tokens);

    auto ast = parser.compilationUnit();
    Obj::Mgr mgr;

    asg::Ast2Asg ast2asg(mgr);
    auto asg = ast2asg(ast->translationUnit());
    mgr.mRoot = asg;
    mgr.gc();

    asg::Typing inferType(mgr);
    inferType(asg);
    mgr.gc();

    asg::Asg2Json asg2json;
    llvm::json::Value json = asg2json(asg);

    outFile << json << '\n';

    return 0;
}

int main_task3(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cout << "Usage: " << argv[0] << " <input> <output>\n";
        return -1;
    }

    auto inFileOrErr = llvm::MemoryBuffer::getFile(argv[1]);
    if (auto err = inFileOrErr.getError())
    {
        std::cout << "Error: unable to open input file: " << argv[1] << '\n';
        return -2;
    }
    auto inFile = std::move(inFileOrErr.get());
    std::error_code ec;
    llvm::StringRef outPath(argv[2]);
    llvm::raw_fd_ostream outFile(outPath, ec);
    if (ec)
    {
        std::cout << "Error: unable to open output file: " << argv[2] << '\n';
        return -3;
    }

    auto json = llvm::json::parse(inFile->getBuffer());
    if (!json)
    {
        std::cout << "Error: unable to parse input file: " << argv[1] << '\n';
        return 1;
    }

    // 读取 JSON，转换为 ASG
    Obj::Mgr mgr;
    Json2Asg json2asg(mgr);
    auto asg = json2asg(json.get());
    mgr.mRoot = asg;
    mgr.gc();

    // 从 ASG 发射到 LLVM IR
    llvm::LLVMContext ctx;
    EmitIR emitIR(mgr, ctx);
    auto &mod = emitIR(asg);
    mgr.gc();

    // 先把 LLVM IR 写出到文件里，再检查合不合法
    mod.print(outFile, nullptr, false, true);
    if (llvm::verifyModule(mod, &llvm::outs()))
        return 3;

    return 0;
}

int main(int argc, char *argv[])
{
    // main_task1(argc, argv);
    // main_task2(argc, argv);
    main_task3(argc, argv);
}