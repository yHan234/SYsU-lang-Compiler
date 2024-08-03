#include "Mem2Reg.hpp"
#include "PromoteMemToReg.hpp"
#include <llvm/ADT/Statistic.h>

using namespace llvm;

#define DEBUG_TYPE "mem2reg"

STATISTIC(NumPromoted, "Number of alloca's promoted");

// 遍历函数中的指令，对于每个 entry 基本块中的 alloca 指令，
// 如果可以提升 (isAllocaPromotable)，则提升 (PromoteMemToReg)
static bool promoteMemoryToRegister(Function &F, DominatorTree &DT) {
  std::vector<AllocaInst *> Allocas;
  BasicBlock &BB = F.getEntryBlock();
  bool Changed = false;

  while (true) {
    Allocas.clear();

    for (BasicBlock::iterator I = BB.begin(), E = --BB.end(); I != E; ++I)
      if (AllocaInst *AI = dyn_cast<AllocaInst>(I))
        if (isAllocaPromotable(AI))
          Allocas.push_back(AI);

    if (Allocas.empty())
      break;

    PromoteMemToReg(Allocas, DT);
    NumPromoted += Allocas.size();
    Changed = true;
  }
  return Changed;
}

PreservedAnalyses Mem2Reg::run(Function &F, FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);

  if (!promoteMemoryToRegister(F, DT))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
