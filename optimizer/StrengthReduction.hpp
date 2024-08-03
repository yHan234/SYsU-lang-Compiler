#pragma once

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/raw_ostream.h>

class StrengthReduction : public llvm::PassInfoMixin<StrengthReduction> {
public:
  explicit StrengthReduction(llvm::raw_ostream &out) : mOut(out) {}

  llvm::PreservedAnalyses run(llvm::Function &Func,
                              llvm::FunctionAnalysisManager &AM);

private:
  llvm::raw_ostream &mOut;
};
