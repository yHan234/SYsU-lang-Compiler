#include "StrengthReduction.hpp"
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

using namespace llvm;

PreservedAnalyses StrengthReduction::run(Function &Func,
                                         FunctionAnalysisManager &AM) {
  int StrengthReductionTimes = 0;

  for (auto &&BB : Func) {
    std::vector<std::pair<Instruction *, Instruction *>> InstToReplace;

    for (auto &&Inst : BB) {
      if (auto BinOp = dyn_cast<BinaryOperator>(&Inst)) {
        // 获取二元运算指令的左右操作数，并尝试转换为常整数
        Value *LHS = BinOp->getOperand(0);
        Value *RHS = BinOp->getOperand(1);
        bool LHSIsConst = isa<ConstantInt>(LHS);
        bool RHSIsConst = isa<ConstantInt>(RHS);

        // 如果两个操作数都是常量，请先进行常量折叠
        if (LHSIsConst && RHSIsConst) {
          mOut << "Strength Reduction Pass: Please add constant fold pass "
                  "first.\n";
          continue;
        }

        // 如果两个操作数都是变量，不进行强度削弱
        if (!LHSIsConst && !RHSIsConst) {
          continue;
        }

        Value *Var;
        ConstantInt *Const;
        if (LHSIsConst) {
          Const = dyn_cast<ConstantInt>(LHS);
          Var = RHS;
        } else {
          Var = LHS;
          Const = dyn_cast<ConstantInt>(RHS);
        }

        int64_t ConstVal = Const->getSExtValue();
        if (ConstVal < 0 || !isPowerOf2_64(ConstVal)) {
          break;
        }

        auto ConstLog2 =
            ConstantInt::getSigned(Var->getType(), Log2_64(ConstVal));

        switch (BinOp->getOpcode()) {
        case Instruction::Mul: {

          // 将 2 的幂次乘法转换为左移
          InstToReplace.emplace_back(BinOp,
                                     BinaryOperator::CreateShl(Var, ConstLog2));

          ++StrengthReductionTimes;
          break;
        }

        case Instruction::SDiv: {
          // 2 的幂次除法与右移不等价，因为当除法除至 0 时，右移结果为 -1。
          break;
        }

        case Instruction::SRem: {
          // Var / Const
          auto tmp = BinaryOperator::CreateSDiv(Var, Const);
          tmp->insertBefore(BB, Inst.getIterator());

          // (Var / Const) << ConstLog2
          tmp = BinaryOperator::CreateShl(tmp, ConstLog2);
          tmp->insertBefore(BB, Inst.getIterator());

          // Var - (Var / Const) << ConstLog2
          InstToReplace.emplace_back(BinOp,
                                     BinaryOperator::CreateSub(Var, tmp));

          ++StrengthReductionTimes;
          break;
        }

        default:
          break;
        }
      }
    }

    for (auto &&[Old, New] : InstToReplace)
      ReplaceInstWithInst(Old, New);
  }

  mOut << "StrengthReduction running...\nTo reduce " << StrengthReductionTimes
       << " instructions\n";
  return PreservedAnalyses::all();
}