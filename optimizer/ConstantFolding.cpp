#include "ConstantFolding.hpp"

using namespace llvm;

PreservedAnalyses ConstantFolding::run(Function &Fund,
                                       FunctionAnalysisManager &FAM) {
  int ConstFoldTimes = 0;

  for (auto &BB : Fund) {
    std::vector<Instruction *> InstToErase;

    for (auto &I : BB) {
      // 判断当前指令是否是二元运算指令
      if (auto BinOp = dyn_cast<BinaryOperator>(&I)) {
        // 获取二元运算指令的左右操作数，并尝试转换为常整数
        Value *LHS = BinOp->getOperand(0);
        Value *RHS = BinOp->getOperand(1);
        auto ConstLhs = dyn_cast<ConstantInt>(LHS);
        auto ConstRhs = dyn_cast<ConstantInt>(RHS);
        switch (BinOp->getOpcode()) {
        case Instruction::Add: {
          // 若左右操作数均为整数常量，则进行常量折叠与use替换
          if (ConstLhs && ConstRhs) {
            BinOp->replaceAllUsesWith(ConstantInt::getSigned(
                BinOp->getType(),
                ConstLhs->getSExtValue() + ConstRhs->getSExtValue()));
            InstToErase.push_back(BinOp);
            ++ConstFoldTimes;
          }
          break;
        }
        case Instruction::Sub: {
          if (ConstLhs && ConstRhs) {
            BinOp->replaceAllUsesWith(ConstantInt::getSigned(
                BinOp->getType(),
                ConstLhs->getSExtValue() - ConstRhs->getSExtValue()));
            InstToErase.push_back(BinOp);
            ++ConstFoldTimes;
          }
          break;
        }
        case Instruction::Mul: {
          if (ConstLhs && ConstRhs) {
            BinOp->replaceAllUsesWith(ConstantInt::getSigned(
                BinOp->getType(),
                ConstLhs->getSExtValue() * ConstRhs->getSExtValue()));
            InstToErase.push_back(BinOp);
            ++ConstFoldTimes;
          }
          break;
        }
        case Instruction::UDiv:
        case Instruction::SDiv: {
          if (ConstLhs && ConstRhs) {
            BinOp->replaceAllUsesWith(ConstantInt::getSigned(
                BinOp->getType(),
                ConstLhs->getSExtValue() / ConstRhs->getSExtValue()));
            InstToErase.push_back(BinOp);
            ++ConstFoldTimes;
          }
          break;
        }
        default:
          break;
        }
      }
    }
    // 统一删除被折叠为常量的指令
    for (auto &I : InstToErase)
      I->eraseFromParent();
  }

  mOut << "ConstantFolding running...\nTo eliminate " << ConstFoldTimes
       << " instructions\n";
  return PreservedAnalyses::all();
}
