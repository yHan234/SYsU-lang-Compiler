#pragma once

#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Instructions.h>

/// 判断 alloca 指令是否可以提升
///
/// 如果这条 alloca 指令仅被 load 或者 store 指令使用，
/// 且 load store 的类型与 alloca 的类型相同，
/// 且 store 指令将其作为目标地址而不是存储的内容，
/// 那么这条 alloca 指令可以被 mem2reg 优化处理。
bool isAllocaPromotable(const llvm::AllocaInst *AI);

/// Promote the specified list of alloca instructions into scalar
/// registers, inserting PHI nodes as appropriate.
///
/// This function makes use of DominanceFrontier information.  This function
/// does not modify the CFG of the function at all.  All allocas must be from
/// the same function.
///
void PromoteMemToReg(llvm::ArrayRef<llvm::AllocaInst *> Allocas,
                     llvm::DominatorTree &DT);
