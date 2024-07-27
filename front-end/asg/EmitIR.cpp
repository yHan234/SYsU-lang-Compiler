#include "EmitIR.hpp"
#include <llvm/Transforms/Utils/ModuleUtils.h>

#define self (*this)

using namespace asg;

EmitIR::EmitIR(Obj::Mgr &mgr, llvm::LLVMContext &ctx, llvm::StringRef mid)
    : mMgr(mgr), mMod(mid, ctx), mCtx(ctx), mIntTy(llvm::Type::getInt32Ty(ctx)),
      mCurIrb(std::make_unique<llvm::IRBuilder<>>(ctx)),
      mCtorTy(llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), false)), mCurFunc(nullptr)
{
}

llvm::Module &EmitIR::operator()(TranslationUnit *tu)
{
    for (auto &&i : tu->decls)
        self(i);
    return mMod;
}

//==============================================================================
// 类型
//==============================================================================

llvm::Type *EmitIR::operator()(const Type *type)
{
    if (type->texp == nullptr)
    {
        switch (type->spec)
        {
        case Type::Spec::kVoid:
            return llvm::Type::getVoidTy(mCtx);

        case Type::Spec::kInt:
            return llvm::Type::getInt32Ty(mCtx);

        default:
            ABORT();
        }
    }

    Type subt;
    subt.spec = type->spec;
    subt.qual = type->qual;
    subt.texp = type->texp->sub;

    if (auto p = type->texp->dcst<PointerType>())
    {
        return llvm::PointerType::get(self(&subt), 0);
    }

    if (auto p = type->texp->dcst<ArrayType>())
    {
        return llvm::ArrayType::get(self(&subt), p->len);
    }

    if (auto p = type->texp->dcst<FunctionType>())
    {
        std::vector<llvm::Type *> pty;
        for (auto &&param : p->params)
        {
            pty.push_back(self(param));
        }
        return llvm::FunctionType::get(self(&subt), std::move(pty), false);
    }

    ABORT();
}

//==============================================================================
// 表达式
//==============================================================================

llvm::Value *EmitIR::operator()(Expr *obj)
{
    if (auto p = obj->dcst<IntegerLiteral>())
        return self(p);

    if (auto p = obj->dcst<DeclRefExpr>())
        return self(p);

    if (auto p = obj->dcst<ParenExpr>())
        return self(p);

    if (auto p = obj->dcst<UnaryExpr>())
        return self(p);

    if (auto p = obj->dcst<BinaryExpr>())
        return self(p);

    if (auto p = obj->dcst<CallExpr>())
        return self(p);

    if (auto p = obj->dcst<InitListExpr>())
        ABORT();

    if (auto p = obj->dcst<ImplicitInitExpr>())
        return self(p);

    if (auto p = obj->dcst<ImplicitCastExpr>())
        return self(p);

    ABORT();
}

llvm::Constant *EmitIR::operator()(IntegerLiteral *obj)
{
    return llvm::ConstantInt::get(self(obj->type), obj->val);
}

llvm::Value *EmitIR::operator()(DeclRefExpr *obj)
{
    // 变量声明时在 decl->any 中存储了变量的 llvm::Value，现在将其取出
    return reinterpret_cast<llvm::Value *>(obj->decl->any);
}

llvm::Value *EmitIR::operator()(ParenExpr *obj)
{
    return self(obj->sub);
}

llvm::Value *EmitIR::operator()(UnaryExpr *obj)
{
    auto sub = self(obj->sub);

    auto &irb = *mCurIrb;
    switch (obj->op)
    {
    case UnaryExpr::kPos:
        return sub;

    case UnaryExpr::kNeg:
        return irb.CreateNeg(sub);

    case UnaryExpr::kNot:
        return irb.CreateNot(cast_to_i1(sub));

    default:
        ABORT();
    }
}

llvm::Value *EmitIR::operator()(BinaryExpr *obj)
{
    llvm::Value *lhs, *rhs;

    lhs = self(obj->lft);
    if (obj->op != BinaryExpr::kAnd && obj->op != BinaryExpr::kOr) // 短路
    {
        rhs = self(obj->rht);
    }

    auto &irb = *mCurIrb;
    switch (obj->op)
    {
    case BinaryExpr::kMul:
        return irb.CreateMul(lhs, rhs);

    case BinaryExpr::kDiv:
        return irb.CreateSDiv(lhs, rhs);

    case BinaryExpr::kMod:
        return irb.CreateSRem(lhs, rhs);

    case BinaryExpr::kAdd:
        return irb.CreateAdd(lhs, rhs);

    case BinaryExpr::kSub:
        return irb.CreateSub(lhs, rhs);

    case BinaryExpr::kGt:
        return irb.CreateICmpSGT(lhs, rhs);

    case BinaryExpr::kLt:
        return irb.CreateICmpSLT(lhs, rhs);

    case BinaryExpr::kGe:
        return irb.CreateICmpSGE(lhs, rhs);

    case BinaryExpr::kLe:
        return irb.CreateICmpSLE(lhs, rhs);

    case BinaryExpr::kEq:
        return irb.CreateICmpEQ(lhs, rhs);

    case BinaryExpr::kNe:
        return irb.CreateICmpNE(lhs, rhs);

    case BinaryExpr::kAnd: {
        auto endBB = llvm::BasicBlock::Create(mCtx, "", mCurFunc);
        auto lhsBB = irb.GetInsertBlock(); // 左操作数在当前基本块计算
        auto rhsBeginBB = llvm::BasicBlock::Create(mCtx, "land.lhs.true", mCurFunc);

        auto cond = cast_to_i1(lhs);
        irb.CreateCondBr(cond, rhsBeginBB, endBB);

        irb.SetInsertPoint(rhsBeginBB);
        rhs = cast_to_i1(self(obj->rht));
        irb.CreateBr(endBB);
        auto rhsEndBB = irb.GetInsertBlock();

        irb.SetInsertPoint(endBB);
        auto phi = irb.CreatePHI(llvm::Type::getInt1Ty(mCtx), 2);
        phi->addIncoming(irb.getInt1(false), lhsBB);
        phi->addIncoming(rhs, rhsEndBB);

        return phi;
    }

    case BinaryExpr::kOr: {
        auto endBB = llvm::BasicBlock::Create(mCtx, "", mCurFunc);
        auto lhsBB = irb.GetInsertBlock(); // 左操作数在当前基本块计算
        auto rhsBeginBB = llvm::BasicBlock::Create(mCtx, "lor.lhs.false", mCurFunc);

        auto cond = cast_to_i1(lhs);
        irb.CreateCondBr(cond, endBB, rhsBeginBB);

        irb.SetInsertPoint(rhsBeginBB);
        rhs = cast_to_i1(self(obj->rht));
        irb.CreateBr(endBB);
        auto rhsEndBB = irb.GetInsertBlock();

        irb.SetInsertPoint(endBB);
        auto phi = irb.CreatePHI(llvm::Type::getInt1Ty(mCtx), 2);
        phi->addIncoming(irb.getInt1(true), lhsBB);
        phi->addIncoming(rhs, rhsEndBB);

        return phi;
    }

    case BinaryExpr::kAssign:
        return irb.CreateStore(rhs, lhs);

    case BinaryExpr::kIndex: {
        // lft 是一个指针，获得其指向的类型
        auto ltype = obj->lft->type;
        Type subt;
        subt.spec = ltype->spec;
        subt.qual = ltype->qual;
        subt.texp = ltype->texp->sub;

        auto type = self(&subt);

        return irb.CreateGEP(llvm::ArrayType::get(type, 0), lhs, {irb.getInt64(0), rhs});
    }

    default:
        ABORT();
    }
}

llvm::Value *EmitIR::operator()(CallExpr *obj)
{
    std::vector<llvm::Value *> args;
    for (auto arg : obj->args)
    {
        args.push_back(self(arg));
    }
    return mCurIrb->CreateCall(llvm::dyn_cast<llvm::Function>(self(obj->head)), args);
}

llvm::Value *EmitIR::operator()(ImplicitCastExpr *obj)
{
    auto sub = self(obj->sub);

    auto &irb = *mCurIrb;
    switch (obj->kind)
    {
    // 在 LLVM IR 层面，左值体现为指向值的指针，经过 load 指令变成右值
    case ImplicitCastExpr::kLValueToRValue: {
        auto ty = self(obj->sub->type);
        auto loadVal = irb.CreateLoad(ty, sub);
        return loadVal;
    }

    case ImplicitCastExpr::kIntegralCast: {
        ABORT();
    }

    case ImplicitCastExpr::kArrayToPointerDecay: {
        return sub;
    }

    case ImplicitCastExpr::kFunctionToPointerDecay: {
        return sub;
    }

    default:
        ABORT();
    }
}

//==============================================================================
// 语句
//==============================================================================

void EmitIR::operator()(Stmt *obj)
{
    // 若 return/break/continue 之后还有语句，会导致尝试向已终止的基本块插入指令，抛弃这些指令
    if (mCurIrb->GetInsertBlock()->getTerminator())
        return;

    if (auto p = obj->dcst<NullStmt>())
        return;

    if (auto p = obj->dcst<DeclStmt>())
        return self(p);

    if (auto p = obj->dcst<ExprStmt>())
        return self(p);

    if (auto p = obj->dcst<CompoundStmt>())
        return self(p);

    if (auto p = obj->dcst<IfStmt>())
        return self(p);

    if (auto p = obj->dcst<WhileStmt>())
        return self(p);

    if (auto p = obj->dcst<BreakStmt>())
        return self(p);

    if (auto p = obj->dcst<ContinueStmt>())
        return self(p);

    if (auto p = obj->dcst<ReturnStmt>())
        return self(p);

    ABORT();
}

void EmitIR::operator()(DeclStmt *obj)
{
    for (auto &&decl : obj->decls)
        self(decl->dcst<VarDecl>(), false);
}

void EmitIR::operator()(ExprStmt *obj)
{
    self(obj->expr);
}

void EmitIR::operator()(CompoundStmt *obj)
{
    for (auto &&stmt : obj->subs)
    {
        self(stmt);
    }
}

void EmitIR::operator()(IfStmt *obj)
{
    auto &irb = *mCurIrb;

    llvm::Value *cond = cast_to_i1(self(obj->cond));

    auto thenBeginBB = llvm::BasicBlock::Create(mCtx, "if.then", mCurFunc);
    auto endBB = llvm::BasicBlock::Create(mCtx, "if.end", mCurFunc);

    if (obj->else_)
    {
        auto elseBeginBB = llvm::BasicBlock::Create(mCtx, "if.else", mCurFunc);

        irb.CreateCondBr(cond, thenBeginBB, elseBeginBB);

        irb.SetInsertPoint(thenBeginBB);
        self(obj->then);
        // 如果内部有 break/continue/return 则基本块已终结，不再添加跳转指令
        if (!irb.GetInsertBlock()->getTerminator())
            irb.CreateBr(endBB);

        irb.SetInsertPoint(elseBeginBB);
        self(obj->else_);
        // 如果内部有 break/continue/return 则基本块已终结，不再添加跳转指令
        if (!irb.GetInsertBlock()->getTerminator())
            irb.CreateBr(endBB);
    }
    else
    {
        irb.CreateCondBr(cond, thenBeginBB, endBB);

        irb.SetInsertPoint(thenBeginBB);
        self(obj->then);
        // 如果内部有 break/continue/return 则基本块已终结，不再添加跳转指令
        if (!irb.GetInsertBlock()->getTerminator())
            irb.CreateBr(endBB);
    }

    irb.SetInsertPoint(endBB);
}

void EmitIR::operator()(WhileStmt *obj)
{
    auto &irb = *mCurIrb;

    auto condBeginBB = llvm::BasicBlock::Create(mCtx, "while.cond", mCurFunc);
    auto bodyBeginBB = llvm::BasicBlock::Create(mCtx, "while.body", mCurFunc);
    auto endBB = llvm::BasicBlock::Create(mCtx, "while.end", mCurFunc);

    obj->cond->any = condBeginBB;
    obj->body->any = bodyBeginBB;
    obj->any = endBB;

    irb.CreateBr(condBeginBB);

    irb.SetInsertPoint(condBeginBB);
    auto cond = cast_to_i1(self(obj->cond));
    auto condEndBB = irb.GetInsertBlock();

    irb.SetInsertPoint(bodyBeginBB);
    self(obj->body);
    auto bodyEndBB = irb.GetInsertBlock();

    irb.SetInsertPoint(condEndBB);
    irb.CreateCondBr(cond, bodyBeginBB, endBB);

    irb.SetInsertPoint(bodyEndBB);
    // 如果内部有 break/continue/return 则基本块已终结，不再添加跳转指令
    if (!irb.GetInsertBlock()->getTerminator())
        irb.CreateBr(condBeginBB);

    irb.SetInsertPoint(endBB);
}

void EmitIR::operator()(BreakStmt *obj)
{
    // loop->any 存放的是 endBB
    mCurIrb->CreateBr(reinterpret_cast<llvm::BasicBlock *>(obj->loop->any));
}

void EmitIR::operator()(ContinueStmt *obj)
{
    mCurIrb->CreateBr(reinterpret_cast<llvm::BasicBlock *>(obj->loop->dcst<WhileStmt>()->cond->any));
}

void EmitIR::operator()(ReturnStmt *obj)
{
    auto &irb = *mCurIrb;

    llvm::Value *retVal;
    if (!obj->expr)
        retVal = nullptr;
    else
        retVal = self(obj->expr);

    mCurIrb->CreateRet(retVal);
}

//==============================================================================
// 声明
//==============================================================================

void EmitIR::operator()(Decl *obj)
{
    if (auto p = obj->dcst<FunctionDecl>())
        return self(p);

    if (auto p = obj->dcst<VarDecl>())
        return self(p, true);

    ABORT();
}

void EmitIR::operator()(FunctionDecl *obj)
{
    // 创建函数
    auto fty = llvm::dyn_cast<llvm::FunctionType>(self(obj->type));
    auto func = llvm::Function::Create(fty, llvm::GlobalVariable::ExternalLinkage, obj->name, mMod);

    obj->any = func;

    if (obj->body == nullptr)
        return;

    // 创建函数的 entry 基本块
    auto entryBb = llvm::BasicBlock::Create(mCtx, "entry", func);
    mCurIrb->SetInsertPoint(entryBb);
    auto &entryIrb = *mCurIrb;

    // 函数参数
    for (int i = 0; i < obj->params.size(); ++i)
    {
        auto llvm_arg = func->getArg(i);
        auto asg_param = obj->params[i];

        llvm_arg->setName(asg_param->name);
        auto alloca = entryIrb.CreateAlloca(llvm_arg->getType(), nullptr, asg_param->name); // 为参数分配空间
        entryIrb.CreateStore(llvm_arg, alloca); // 将参数值复制到分配的空间
        asg_param->any = alloca;
    }

    // 翻译函数体
    mCurFunc = func;
    self(obj->body);
    auto &exitIrb = *mCurIrb;

    // 如果最后的基本块没有终结指令，那么添加一条
    if (!exitIrb.GetInsertBlock()->getTerminator())
    {
        if (fty->getReturnType()->isVoidTy())
            exitIrb.CreateRetVoid();
        else
            exitIrb.CreateUnreachable();
    }
}

void EmitIR::operator()(VarDecl *obj, bool global)
{
    auto ty = self(obj->type);

    if (global)
    {
        // 这里不能设为 const，因为一会要使用函数初始化
        auto gvar = new llvm::GlobalVariable(mMod, ty, false, llvm::GlobalVariable::ExternalLinkage,
                                             llvm::Constant::getNullValue(ty), obj->name);

        obj->any = gvar;

        if (obj->init == nullptr)
            return;

        // 创建构造函数用于初始化
        mCurFunc = llvm::Function::Create(mCtorTy, llvm::GlobalVariable::PrivateLinkage, "ctor_" + obj->name, mMod);
        llvm::appendToGlobalCtors(mMod, mCurFunc, 65535);

        auto entryBb = llvm::BasicBlock::Create(mCtx, "entry", mCurFunc);
        mCurIrb->SetInsertPoint(entryBb);
        var_init(gvar, obj->init);
        mCurIrb->CreateRet(nullptr);
    }
    else
    {
        // 将函数中所有的 alloca 指令都放到函数的 entry 基本块中，
        // 使得在一开始就为之后函数中会用到的局部变量在栈上分配内存空间，这也是 clang 的做法。

        auto &irb = *mCurIrb;
        const auto &bak = irb.GetInsertBlock(); // 备份当前基本块

        // 如果 entry block 已经有了 terminator，那么变量分配指令要在 terminator 前面。
        auto entry = &mCurFunc->getEntryBlock();
        auto term = entry->getTerminator();
        if (term)
            irb.SetInsertPoint(term);
        else
            irb.SetInsertPoint(entry);

        auto var = irb.CreateAlloca(ty, nullptr, obj->name);
        obj->any = var;

        irb.SetInsertPoint(bak); // 恢复备份的基本块

        if (obj->init)
            var_init(var, obj->init);
    }
}

//============================================================================
// Utils
//============================================================================

// 通过 val != 0 将类型转为 i1
llvm::Value *EmitIR::cast_to_i1(llvm::Value *val)
{
    auto &irb = *mCurIrb;
    if (val->getType() != irb.getInt1Ty())
        val = irb.CreateICmpNE(val, llvm::Constant::getNullValue(val->getType()));
    return val;
}

void EmitIR::var_init(llvm::Value *var, Expr *obj)
{
    auto &irb = *mCurIrb;

    if (auto p = obj->dcst<InitListExpr>())
    {
        bool implicitInit = false;

        if (auto item = p->list[0]->dcst<ImplicitInitExpr>())
        {
            implicitInit = true;

            auto arrTy = obj->type->texp->dcst<ArrayType>();

            int elemCnt = arrTy->len;
            while (arrTy->sub)
            {
                arrTy = arrTy->sub->dcst<ArrayType>();
                if (!arrTy)
                    break;
                elemCnt *= arrTy->len;
            }

            // 假设元素类型均为 i32
            irb.CreateMemSet(var, llvm::Constant::getNullValue(irb.getInt8Ty()), elemCnt * 4, llvm::MaybeAlign());
        }

        // 如果有隐式初始化，则遍历 list 的下标从 1 开始，否则从 0 开始
        // 而 LLVM IR 的数组下标总是从 0 开始
        for (int i = implicitInit; i < p->list.size(); ++i)
        {
            var_init(irb.CreateInBoundsGEP(self(obj->type), var, {irb.getInt64(0), irb.getInt64(i - implicitInit)}),
                     p->list[i]);
        }

        return;
    }

    if (auto p = obj->dcst<Expr>())
    {
        irb.CreateStore(self(p), var);
        return;
    }

    ABORT();
}
