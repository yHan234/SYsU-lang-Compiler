#include "PromoteMemToReg.hpp"
#include <llvm/IR/Constants.h>
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/Analysis/SimplifyQuery.h"

using namespace llvm;

bool isAllocaPromotable(const AllocaInst *AI) {
  for (const User *U : AI->users()) {
    if (const LoadInst *LI = dyn_cast<LoadInst>(U)) {
      if (LI->getType() != AI->getAllocatedType())
        return false;
    } else if (const StoreInst *SI = dyn_cast<StoreInst>(U)) {
      // store 指令只能将其作为目标地址而不是存储的内容
      if (SI->getValueOperand() == AI ||
          SI->getValueOperand()->getType() != AI->getAllocatedType())
        return false;
    }
    return false;
  }

  return true;
}

/// 记录对 allca 变量进行了 store 或 load 的基本块，和一些额外信息
/// 如果在一个基本块内进行了多次 store 或 load，那么它在对应 vector
/// 中也会出现多次
///
/// 对 allca 变量的 store 和 load，也就是对 SSA 变量的 define 和 use
struct AllocaInfo {
  // 对 alloca 的每一个 store，就是对 SSA 变量的一次 defining。
  SmallVector<BasicBlock *, 32> DefiningBlocks;
  // 对 alloca 的每一个 load，就是对 SSA 变量的一次 using。
  SmallVector<BasicBlock *, 32> UsingBlocks;

  StoreInst *OnlyStore; // 当这个 alloca 只有单个 store 指令时才有意义
  BasicBlock *OnlyBlock;
  bool OnlyUsedInOneBlock;

  void clear() {
    DefiningBlocks.clear();
    UsingBlocks.clear();
    OnlyStore = nullptr;
    OnlyBlock = nullptr;
    OnlyUsedInOneBlock = true;
  }

  // 扫描传入的 AllocaInst，填充 AllocaInfo。
  void AnalyzeAlloca(AllocaInst *AI) {
    clear();

    for (User *U : AI->users()) {
      Instruction *User = cast<Instruction>(U);

      if (StoreInst *SI = dyn_cast<StoreInst>(User)) {
        DefiningBlocks.push_back(SI->getParent());
        OnlyStore = SI;
      } else {
        LoadInst *LI = cast<LoadInst>(User);
        UsingBlocks.push_back(LI->getParent());
      }

      if (OnlyUsedInOneBlock) {
        if (!OnlyBlock) // 第一次循环还未给 OnlyBlock 赋值的时候
          OnlyBlock = User->getParent();
        else if (OnlyBlock != User->getParent())
          OnlyUsedInOneBlock = false;
      }
    }
  }
};

/// Data package used by RenamePass().
struct RenamePassData {
  using ValVector = std::vector<Value *>;

  RenamePassData(BasicBlock *B, BasicBlock *P, ValVector V)
      : BB(B), Pred(P), Values(std::move(V)) {}

  BasicBlock *BB;
  BasicBlock *Pred;
  ValVector Values;
};

struct PromoteMem2Reg {
  /// 要被提升的 alloca 指令
  std::vector<AllocaInst *> Allocas;

  DominatorTree &DT;

  const SimplifyQuery SQ;

  /// alloca 指令到它的下标的反向映射
  DenseMap<AllocaInst *, unsigned> AllocaLookup;

  /// 正在添加的 PHI 节点
  ///
  /// That map is used to simplify some Phi nodes as we iterate over it, so
  /// it should have deterministic iterators.  We could use a MapVector, but
  /// since we already maintain a map from BasicBlock* to a stable numbering
  /// (BBNumbers), the DenseMap is more efficient (also supports removal).
  DenseMap<std::pair<unsigned, unsigned>, PHINode *> NewPhiNodes;

  /// For each PHI node, keep track of which entry in Allocas it corresponds
  /// to.
  DenseMap<PHINode *, unsigned> PhiToAllocaMap;

  /// The set of basic blocks the renamer has already visited.
  SmallPtrSet<BasicBlock *, 16> Visited;

  /// 包含稳定的基本块编号，以避免不确定的行为。
  DenseMap<BasicBlock *, unsigned> BBNumbers;

  /// Lazily compute the number of predecessors a block has.
  DenseMap<const BasicBlock *, unsigned> BBNumPreds;

public:
  PromoteMem2Reg(ArrayRef<AllocaInst *> Allocas, DominatorTree &DT)
      : Allocas(Allocas.begin(), Allocas.end()), DT(DT),
        SQ(DT.getRoot()->getParent()->getParent()->getDataLayout(), nullptr,
           &DT) {}

  void run();

private:
  void RemoveFromAllocasList(unsigned &AllocaIdx) {
    Allocas[AllocaIdx] = Allocas.back();
    Allocas.pop_back();
    --AllocaIdx;
  }

  unsigned getNumPreds(const BasicBlock *BB) {
    unsigned &NP = BBNumPreds[BB];
    if (NP == 0)
      NP = pred_size(BB) + 1;
    return NP - 1;
  }

  void ComputeLiveInBlocks(AllocaInst *AI, AllocaInfo &Info,
                           const SmallPtrSetImpl<BasicBlock *> &DefBlocks,
                           SmallPtrSetImpl<BasicBlock *> &LiveInBlocks);
  void RenamePass(BasicBlock *BB, BasicBlock *Pred,
                  RenamePassData::ValVector &IncVals,
                  std::vector<RenamePassData> &Worklist);
  bool QueuePhiNode(BasicBlock *BB, unsigned AllocaIdx, unsigned &Version);
};

/// 加载并存储每个基本块中，对应 alloca 指令的 load/store 指令在块中的顺序。
///
/// 这个功能很重要，因为它避免了在处理同一个大基本块中的alloca时，对这个大基本块多次扫描。
class LargeBlockInfo {
  /// 对于我们跟踪的每条指令，记录它的下标。索引从块开始的指令编号开始。
  DenseMap<const Instruction *, unsigned> InstNumbers;

public:
  static bool isInterestingInstruction(const Instruction *I) {
    return (isa<LoadInst>(I) && isa<AllocaInst>(I->getOperand(0))) ||
           (isa<StoreInst>(I) && isa<AllocaInst>(I->getOperand(1)));
  }

  /// 获取或计算给定指令的下标。
  unsigned getInstructionIndex(const Instruction *I) {
    assert(isInterestingInstruction(I) &&
           "Not a load/store to/from an alloca?");

    // 如果已经存储了结果，直接返回。
    DenseMap<const Instruction *, unsigned>::iterator It = InstNumbers.find(I);
    if (It != InstNumbers.end())
      return It->second;

    // 扫描基本块，为块中每个感兴趣的指令记录信息，以避免重复扫描。
    const BasicBlock *BB = I->getParent();
    unsigned InstNo = 0;
    for (const Instruction &BBI : *BB)
      if (isInterestingInstruction(&BBI))
        InstNumbers[&BBI] = InstNo++;
    It = InstNumbers.find(I);

    assert(It != InstNumbers.end() && "Didn't insert instruction?");
    return It->second;
  }

  void deleteValue(const Instruction *I) { InstNumbers.erase(I); }

  void clear() { InstNumbers.clear(); }
};

/// 对给定的单 store alloca，重写尽可能多的 load 指令
///
/// 对于一个 store 指令 addr <- val
/// 因为只有一个 store 指令，我们可以简单地将所有被 val 支配到的 load 指令替换为
/// val。 若返回 true，说明已成功将这个 alloca 完全变为 SSA 形式。 若返回
/// false，说明一定有没有被这条 store 支配到的 load，也就是一定有 phi。
static bool rewriteSingleStoreAlloca(AllocaInst *AI, AllocaInfo &Info,
                                     LargeBlockInfo &LBI, DominatorTree &DT) {
  StoreInst *OnlyStore = Info.OnlyStore;
  bool StoringGlobalVal = !isa<Instruction>(OnlyStore->getOperand(
      0)); // StoreInst 的 Op0 是 val，val 不是 Instruction 说明 val 是全局变量
  BasicBlock *StoreBB = OnlyStore->getParent();
  int StoreIndex = -1;

  // 清空 UsingBlocks，将无法处理的 load 指令放入。
  Info.UsingBlocks.clear();

  for (User *U : make_early_inc_range(AI->users())) {
    Instruction *UserInst = cast<Instruction>(U);
    if (UserInst == OnlyStore)
      continue;
    LoadInst *LI = cast<LoadInst>(UserInst);

    // 去除不可重写的 load 指令（没有被支配到）
    if (!StoringGlobalVal) { // 仅处理非全局变量，因为全局变量支配所有指令
      if (LI->getParent() == StoreBB) {
        // 如果 load 指令在唯一的 store 指令前面，则不可重写。
        if (StoreIndex == -1)
          StoreIndex = LBI.getInstructionIndex(OnlyStore);

        if (unsigned(StoreIndex) > LBI.getInstructionIndex(LI)) {
          Info.UsingBlocks.push_back(StoreBB);
          continue;
        }
      } else if (!DT.dominates(StoreBB, LI->getParent())) {
        // 如果 load 和 store 不在同一个基本块，检查 store 是否支配 load。
        // 如果不支配，则不可重写。
        Info.UsingBlocks.push_back(LI->getParent());
        continue;
      }
    }

    // 现在可以安全地处理这条 load 指令
    Value *ReplVal = OnlyStore->getOperand(0);
    //? If the replacement value is the load, this must occur in unreachable
    //? code.
    if (ReplVal == LI)
      ReplVal = PoisonValue::get(LI->getType());

    LI->replaceAllUsesWith(ReplVal);
    LI->eraseFromParent();
    LBI.deleteValue(LI);
  }

  // 如果有不能处理的 load 指令，返回 false
  if (!Info.UsingBlocks.empty())
    return false;

  // 移除不需要的 store 和 alloca
  Info.OnlyStore->eraseFromParent();
  LBI.deleteValue(Info.OnlyStore);

  AI->eraseFromParent();
  return true;
}

/// Does the given value dominate the specified phi node?
static bool valueDominatesPHI(Value *V, PHINode *P, const DominatorTree *DT) {
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I)
    // Arguments and constants dominate all instructions.
    return true;

  // If we have a DominatorTree then do a precise test.
  if (DT)
    return DT->dominates(I, P);

  // Otherwise, if the instruction is in the entry block and is not an invoke,
  // then it obviously dominates all phi nodes.
  if (I->getParent()->isEntryBlock() && !isa<InvokeInst>(I) &&
      !isa<CallBrInst>(I))
    return true;

  return false;
}

/// See if we can fold the given phi. If not, returns null.
static Value *simplifyPHINode(PHINode *PN, const SimplifyQuery &Q) {
  // WARNING: no matter how worthwhile it may seem, we can not perform PHI CSE
  //          here, because the PHI we may succeed simplifying to was not
  //          def-reachable from the original PHI!

  // If all of the PHI's incoming values are the same then replace the PHI node
  // with the common value.
  Value *CommonValue = nullptr;
  bool HasUndefInput = false;
  SmallVector<Value *, 8> IncomingValues(PN->operands());
  for (Value *Incoming : IncomingValues) {
    // If the incoming value is the phi node itself, it can safely be skipped.
    if (Incoming == PN)
      continue;
    if (Q.isUndefValue(Incoming)) {
      // Remember that we saw an undef value, but otherwise ignore them.
      HasUndefInput = true;
      continue;
    }
    if (CommonValue && Incoming != CommonValue)
      return nullptr; // Not the same, bail out.
    CommonValue = Incoming;
  }

  // If CommonValue is null then all of the incoming values were either undef or
  // equal to the phi node itself.
  if (!CommonValue)
    return UndefValue::get(PN->getType());

  if (HasUndefInput) {
    // If we have a PHI node like phi(X, undef, X), where X is defined by some
    // instruction, we cannot return X as the result of the PHI node unless it
    // dominates the PHI block.
    return valueDominatesPHI(CommonValue, PN, Q.DT) ? CommonValue : nullptr;
  }

  return CommonValue;
}

/// 很多 alloca 指令都只在一个基本块中被使用。
/// 如果是这种情况，直接对这个基本块进行线性扫描。
///
/// 如果在被写之前就被读取，那么不能提升这个 alloca。举例：
///  // A is an alloca with no stores so far
///  for (...) {
///    int t = *A;
///    if (!first_iteration)
///      use(t);
///    *A = 42;
///  }
///
///? 为什么不用线性算法？
static bool promoteSingleBlockAlloca(AllocaInst *AI, const AllocaInfo &Info,
                                     LargeBlockInfo &LBI, DominatorTree &DT) {
  // 这段代码基于对基本块很大的假设而优化，但这并没有明显削弱小基本块的情况。
  // 这里使用了 LargeBlockInfo 来更高效地获取指令下标。

  // 遍历 alloca 的 use-def 列表，获取所有 store 的位置。
  using StoresByIndexTy = SmallVector<std::pair<unsigned, StoreInst *>, 64>;
  StoresByIndexTy StoresByIndex;

  for (User *U : AI->users())
    if (StoreInst *SI = dyn_cast<StoreInst>(U))
      StoresByIndex.push_back(std::make_pair(LBI.getInstructionIndex(SI), SI));

  // 按下标排序，一会要使用二分查找算法。
  llvm::sort(StoresByIndex, less_first());

  // 遍历所有的 load，对于每个 load 用它之前最近的 store 代替他们。
  for (User *U : make_early_inc_range(AI->users())) {
    LoadInst *LI = dyn_cast<LoadInst>(U);
    if (!LI)
      continue;

    unsigned LoadIdx = LBI.getInstructionIndex(LI);

    // 二分找到在 load 之前离它最近的 store。
    StoresByIndexTy::iterator I = llvm::lower_bound(
        StoresByIndex,
        std::make_pair(LoadIdx, static_cast<StoreInst *>(nullptr)),
        less_first());
    Value *ReplVal;
    if (I == StoresByIndex.begin()) {
      if (StoresByIndex.empty())
        // 如果本来就一条 store 指令都没有，那么值为 undef。
        ReplVal = UndefValue::get(LI->getType());
      else
        // 如果在这条load指令之前没有store，退出（可能会被之后的store指令影响）。
        return false;
    } else {
      // 如果 load 前有 store，用 store 的 value 替换。
      ReplVal = std::prev(I)->second->getOperand(0);
    }

    //? If the replacement value is the load, this must occur in unreachable
    //? code.
    if (ReplVal == LI)
      ReplVal = PoisonValue::get(LI->getType());

    LI->replaceAllUsesWith(ReplVal);
    LI->eraseFromParent();
    LBI.deleteValue(LI);
  }

  while (!AI->use_empty()) {
    StoreInst *SI = cast<StoreInst>(AI->user_back());
    SI->eraseFromParent();
    LBI.deleteValue(SI);
  }
  AI->eraseFromParent();
  return true;
}

/// 确定 value 是 live-in（活跃进入）的块。
///
/// 也就是 value 在这个基本块内部 use，但是在 use 之前没有 def。
/// 只有这些块才需要插入 PHI 指令。
void PromoteMem2Reg::ComputeLiveInBlocks(
    AllocaInst *AI, AllocaInfo &Info,
    const SmallPtrSetImpl<BasicBlock *> &DefBlocks,
    SmallPtrSetImpl<BasicBlock *> &LiveInBlocks) {
  // 为了确定块是否 live-in，我们必须遍历块 def 了 value 的前驱。
  // 将需要检查前驱的块加入到 LiveInBlockWorklist。
  // 一开始将所有有 use 的块都放入。
  SmallVector<BasicBlock *, 64> LiveInBlockWorklist(Info.UsingBlocks.begin(),
                                                    Info.UsingBlocks.end());

  // 如果任何一个块既有 use 又有 def，检查 def 是否在 use 之前。
  // 如果 def 是在 use 之前的，那么 value 不是 live-in 的。
  for (unsigned i = 0, e = LiveInBlockWorklist.size(); i != e; ++i) {
    BasicBlock *BB = LiveInBlockWorklist[i];
    if (!DefBlocks.count(BB))
      continue;

    // 这个一个同时 use 和 def 了 value 的块。如果第一个对 allca 的引用是一个
    // def (store)，就可知道 value 不是 live-in 的，将其移除。
    for (BasicBlock::iterator I = BB->begin();; ++I) {
      if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
        if (SI->getOperand(1) != AI)
          continue;

        LiveInBlockWorklist[i] = LiveInBlockWorklist.back();
        LiveInBlockWorklist.pop_back();
        --i;
        --e;
        break;
      }

      if (LoadInst *LI = dyn_cast<LoadInst>(I))
        if (LI->getOperand(0) == AI)
          break;
    }
  }

  // 现在我们有了一些 live-in 的块，
  // 递归添加他们的前驱直到我们发现整个区域的 value 都 live（活跃）。
  while (!LiveInBlockWorklist.empty()) {
    BasicBlock *BB = LiveInBlockWorklist.pop_back_val();

    // 确认块是 live in 的，将他们插入到 set。
    // 如果它已经在 set 里了，那就是已经处理完毕，也不需要处理它的前驱了。
    if (!LiveInBlocks.insert(BB).second)
      continue;

    // value 是活跃进入这个块的，那么对于它的前驱，要么也是活跃进入的，
    // 要么就是对 value 进行了 def。添加前驱直到遇到定义的块。
    // 这时候加入的块是没有 use 对应 value 的（use 了的已经在刚才加了）。
    for (BasicBlock *P : predecessors(BB)) {
      // 如果块 def 了 value，那么它不是 live 的。
      if (DefBlocks.count(P))
        continue;

      // 否则是 live 的，添加到 worklist。
      LiveInBlockWorklist.push_back(P);
    }
  }
}

/// 将要添加到特定 alloca 的基本块中的 phi 节点排队
///
/// 如果该变量还没有 phi 节点，则返回 true
bool PromoteMem2Reg::QueuePhiNode(BasicBlock *BB, unsigned AllocaNo,
                                  unsigned &Version) {
  // 寻找要添加的 phi 节点
  PHINode *&PN = NewPhiNodes[std::make_pair(BBNumbers[BB], AllocaNo)];

  // 如果该基本块已经有了对应的 phi 节点，那么返回
  if (PN)
    return false;

  // 创建 phi 节点，添加到基本块
  PN = PHINode::Create(Allocas[AllocaNo]->getAllocatedType(), getNumPreds(BB),
                       Allocas[AllocaNo]->getName() + "." + Twine(Version++));
  PN->insertBefore(&*(BB->begin()));
  PhiToAllocaMap[PN] = AllocaNo;
  return true;
}

/// Recursively traverse the CFG of the function, renaming loads and
/// stores to the allocas which we are promoting.
///
/// IncomingVals indicates what value each Alloca contains on exit from the
/// predecessor block Pred.
void PromoteMem2Reg::RenamePass(BasicBlock *BB, BasicBlock *Pred,
                                RenamePassData::ValVector &IncomingVals,
                                std::vector<RenamePassData> &Worklist) {
NextIteration:
  // If we are inserting any phi nodes into this BB, they will already be in the
  // block.
  if (PHINode *APN = dyn_cast<PHINode>(BB->begin())) {
    // If we have PHI nodes to update, compute the number of edges from Pred to
    // BB.
    if (PhiToAllocaMap.count(APN)) {
      // We want to be able to distinguish between PHI nodes being inserted by
      // this invocation of mem2reg from those phi nodes that already existed in
      // the IR before mem2reg was run.  We determine that APN is being inserted
      // because it is missing incoming edges.  All other PHI nodes being
      // inserted by this pass of mem2reg will have the same number of incoming
      // operands so far.  Remember this count.
      unsigned NewPHINumOperands = APN->getNumOperands();

      unsigned NumEdges = llvm::count(successors(Pred), BB);
      assert(NumEdges && "Must be at least one edge from Pred to BB!");

      // Add entries for all the phis.
      BasicBlock::iterator PNI = BB->begin();
      do {
        unsigned AllocaNo = PhiToAllocaMap[APN];

        // Add N incoming values to the PHI node.
        for (unsigned i = 0; i != NumEdges; ++i)
          APN->addIncoming(IncomingVals[AllocaNo], Pred);

        // The currently active variable for this block is now the PHI.
        IncomingVals[AllocaNo] = APN;

        // Get the next phi node.
        ++PNI;
        APN = dyn_cast<PHINode>(PNI);
        if (!APN)
          break;

        // Verify that it is missing entries.  If not, it is not being inserted
        // by this mem2reg invocation so we want to ignore it.
      } while (APN->getNumOperands() == NewPHINumOperands);
    }
  }

  // Don't revisit blocks.
  if (!Visited.insert(BB).second)
    return;

  for (BasicBlock::iterator II = BB->begin(); !II->isTerminator();) {
    Instruction *I = &*II++; // get the instruction, increment iterator

    if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
      AllocaInst *Src = dyn_cast<AllocaInst>(LI->getPointerOperand());
      if (!Src)
        continue;

      DenseMap<AllocaInst *, unsigned>::iterator AI = AllocaLookup.find(Src);
      if (AI == AllocaLookup.end())
        continue;

      Value *V = IncomingVals[AI->second];

      // Anything using the load now uses the current value.
      LI->replaceAllUsesWith(V);
      LI->eraseFromParent();
    } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
      // Delete this instruction and mark the name as the current holder of the
      // value
      AllocaInst *Dest = dyn_cast<AllocaInst>(SI->getPointerOperand());
      if (!Dest)
        continue;

      DenseMap<AllocaInst *, unsigned>::iterator ai = AllocaLookup.find(Dest);
      if (ai == AllocaLookup.end())
        continue;

      // what value were we writing?
      unsigned AllocaNo = ai->second;
      IncomingVals[AllocaNo] = SI->getOperand(0);

      SI->eraseFromParent();
    }
  }

  // 'Recurse' to our successors.
  succ_iterator I = succ_begin(BB), E = succ_end(BB);
  if (I == E)
    return;

  // Keep track of the successors so we don't visit the same successor twice
  SmallPtrSet<BasicBlock *, 8> VisitedSuccs;

  // Handle the first successor without using the worklist.
  VisitedSuccs.insert(*I);
  Pred = BB;
  BB = *I;
  ++I;

  for (; I != E; ++I)
    if (VisitedSuccs.insert(*I).second)
      Worklist.emplace_back(*I, Pred, IncomingVals);

  goto NextIteration;
}

void PromoteMem2Reg::run() {
  Function &F = *DT.getRoot()->getParent();

  AllocaInfo Info;
  LargeBlockInfo LBI;
  ForwardIDFCalculator IDF(DT);

  for (unsigned AllocaNum = 0; AllocaNum != Allocas.size(); ++AllocaNum) {
    AllocaInst *AI = Allocas[AllocaNum];

    assert(isAllocaPromotable(AI) && "Cannot promote non-promotable alloca!");
    assert(AI->getParent()->getParent() == &F &&
           "All allocas should be in the same function, which is same as DF!");

    // 如果这个变量没有被使用过，直接移除。
    if (AI->use_empty()) {
      AI->eraseFromParent();

      // 对这个 alloca 的处理完毕。
      RemoveFromAllocasList(AllocaNum);
      continue;
    }

    Info.AnalyzeAlloca(AI);

    // 如果这条 alloca 指令只有一条对应的 store 指令。
    if (Info.DefiningBlocks.size() == 1) {
      if (rewriteSingleStoreAlloca(AI, Info, LBI, DT)) {
        // 对这个 alloca 的处理完毕。
        RemoveFromAllocasList(AllocaNum);
        continue;
      }
    }

    // 如果这条 alloca 指令对应的读写都在同一个基本块中。
    if (Info.OnlyUsedInOneBlock &&
        promoteSingleBlockAlloca(AI, Info, LBI, DT)) {
      // 对这个 alloca 的处理完毕。
      RemoveFromAllocasList(AllocaNum);
      continue;
    }

    // 如果还没有计算过 BB 在 function 中的 ID，现在计算。
    if (BBNumbers.empty()) {
      unsigned ID = 0;
      for (auto &BB : F)
        BBNumbers[&BB] = ID++;
    }

    // 反向映射 alloca 指令到 id，之后的 rename 环节有用。
    AllocaLookup[Allocas[AllocaNum]] = AllocaNum;

    // 将 DefiningBlocks 存储到 set 以去重，高效查找。
    SmallPtrSet<BasicBlock *, 32> DefBlocks(Info.DefiningBlocks.begin(),
                                            Info.DefiningBlocks.end());

    // 确定 value 是 live-in（活跃进入）的块。
    // 也就是 value 在这个基本块内部被使用，但是在这个基本块的开头处并没有 def。
    SmallPtrSet<BasicBlock *, 32> LiveInBlocks;
    ComputeLiveInBlocks(AI, Info, DefBlocks, LiveInBlocks);

    // IDF.calculate(PHIBlocks) 计算出了所有需要插入 PHI 指令的块
    IDF.setLiveInBlocks(LiveInBlocks);
    IDF.setDefiningBlocks(DefBlocks);
    SmallVector<BasicBlock *, 32> PHIBlocks;
    IDF.calculate(PHIBlocks);
    llvm::sort(PHIBlocks, [this](BasicBlock *A, BasicBlock *B) {
      return BBNumbers.find(A)->second < BBNumbers.find(B)->second;
    });

    unsigned CurrentVersion = 0;
    for (BasicBlock *BB : PHIBlocks)
      QueuePhiNode(BB, AllocaNum, CurrentVersion);
  }

  if (Allocas.empty()) {
    return; // 所有的 alloca 都是简单的
  }
  LBI.clear();

  // 将所有 alloca 的传入值都设为 null，
  // 如果有还没 store 就 load 的情况，就会读取 null。
  RenamePassData::ValVector Values(Allocas.size());
  for (unsigned i = 0, e = Allocas.size(); i != e; ++i)
    Values[i] = UndefValue::get(Allocas[i]->getAllocatedType());

  // RenamePass 会遍历所有的基本块，并插入之前标记必要的 phi 节点。
  std::vector<RenamePassData> RenamePassWorkList;
  RenamePassWorkList.emplace_back(&F.front(), nullptr, std::move(Values));
  do {
    RenamePassData RPD = std::move(RenamePassWorkList.back());
    RenamePassWorkList.pop_back();
    // RenamePass 可能会添加新的 worklist 入口
    RenamePass(RPD.BB, RPD.Pred, RPD.Values, RenamePassWorkList);
  } while (!RenamePassWorkList.empty());

  // The renamer uses the Visited set to avoid infinite loops.  Clear it now.
  Visited.clear();

  // Remove the allocas themselves from the function.
  for (Instruction *A : Allocas) {
    // If there are any uses of the alloca instructions left, they must be in
    // unreachable basic blocks that were not processed by walking the dominator
    // tree. Just delete the users now.
    if (!A->use_empty())
      A->replaceAllUsesWith(PoisonValue::get(A->getType()));
    A->eraseFromParent();
  }

  // Loop over all of the PHI nodes and see if there are any that we can get
  // rid of because they merge all of the same incoming values.  This can
  // happen due to undef values coming into the PHI nodes.  This process is
  // iterative, because eliminating one PHI node can cause others to be removed.
  bool EliminatedAPHI = true;
  while (EliminatedAPHI) {
    EliminatedAPHI = false;

    // Iterating over NewPhiNodes is deterministic, so it is safe to try to
    // simplify and RAUW them as we go.  If it was not, we could add uses to
    // the values we replace with in a non-deterministic order, thus creating
    // non-deterministic def->use chains.
    for (DenseMap<std::pair<unsigned, unsigned>, PHINode *>::iterator
             I = NewPhiNodes.begin(),
             E = NewPhiNodes.end();
         I != E;) {
      PHINode *PN = I->second;

      // If this PHI node merges one value and/or undefs, get the value.
      if (Value *V = simplifyPHINode(PN, SQ)) {
        PN->replaceAllUsesWith(V);
        PN->eraseFromParent();
        NewPhiNodes.erase(I++);
        EliminatedAPHI = true;
        continue;
      }
      ++I;
    }
  }

  // At this point, the renamer has added entries to PHI nodes for all reachable
  // code.  Unfortunately, there may be unreachable blocks which the renamer
  // hasn't traversed.  If this is the case, the PHI nodes may not
  // have incoming values for all predecessors.  Loop over all PHI nodes we have
  // created, inserting poison values if they are missing any incoming values.
  for (DenseMap<std::pair<unsigned, unsigned>, PHINode *>::iterator
           I = NewPhiNodes.begin(),
           E = NewPhiNodes.end();
       I != E; ++I) {
    // We want to do this once per basic block.  As such, only process a block
    // when we find the PHI that is the first entry in the block.
    PHINode *SomePHI = I->second;
    BasicBlock *BB = SomePHI->getParent();
    if (&BB->front() != SomePHI)
      continue;

    // Only do work here if there the PHI nodes are missing incoming values.  We
    // know that all PHI nodes that were inserted in a block will have the same
    // number of incoming values, so we can just check any of them.
    if (SomePHI->getNumIncomingValues() == getNumPreds(BB))
      continue;

    // Get the preds for BB.
    SmallVector<BasicBlock *, 16> Preds(predecessors(BB));

    // Ok, now we know that all of the PHI nodes are missing entries for some
    // basic blocks.  Start by sorting the incoming predecessors for efficient
    // access.
    auto CompareBBNumbers = [this](BasicBlock *A, BasicBlock *B) {
      return BBNumbers.find(A)->second < BBNumbers.find(B)->second;
    };
    llvm::sort(Preds, CompareBBNumbers);

    // Now we loop through all BB's which have entries in SomePHI and remove
    // them from the Preds list.
    for (unsigned i = 0, e = SomePHI->getNumIncomingValues(); i != e; ++i) {
      // Do a log(n) search of the Preds list for the entry we want.
      SmallVectorImpl<BasicBlock *>::iterator EntIt = llvm::lower_bound(
          Preds, SomePHI->getIncomingBlock(i), CompareBBNumbers);
      assert(EntIt != Preds.end() && *EntIt == SomePHI->getIncomingBlock(i) &&
             "PHI node has entry for a block which is not a predecessor!");

      // Remove the entry
      Preds.erase(EntIt);
    }

    // At this point, the blocks left in the preds list must have dummy
    // entries inserted into every PHI nodes for the block.  Update all the phi
    // nodes in this block that we are inserting (there could be phis before
    // mem2reg runs).
    unsigned NumBadPreds = SomePHI->getNumIncomingValues();
    BasicBlock::iterator BBI = BB->begin();
    while ((SomePHI = dyn_cast<PHINode>(BBI++)) &&
           SomePHI->getNumIncomingValues() == NumBadPreds) {
      Value *PoisonVal = PoisonValue::get(SomePHI->getType());
      for (BasicBlock *Pred : Preds)
        SomePHI->addIncoming(PoisonVal, Pred);
    }
  }

  NewPhiNodes.clear();
}

void PromoteMemToReg(ArrayRef<AllocaInst *> Allocas, DominatorTree &DT) {
  PromoteMem2Reg(Allocas, DT).run();
}
