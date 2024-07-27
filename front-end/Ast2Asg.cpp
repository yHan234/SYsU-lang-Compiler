#include "Ast2Asg.hpp"
#include <unordered_map>

#define self (*this)

namespace asg
{

// 符号表，保存当前作用域的所有声明
struct Ast2Asg::Symtbl : public std::unordered_map<std::string, Decl *>
{
    Ast2Asg &m;
    Symtbl *mPrev;

    Symtbl(Ast2Asg &m) : m(m), mPrev(m.mSymtbl)
    {
        m.mSymtbl = this;
    }

    ~Symtbl()
    {
        m.mSymtbl = mPrev;
    }

    Decl *resolve(const std::string &name);
};

Decl *Ast2Asg::Symtbl::resolve(const std::string &name)
{
    auto iter = find(name);
    if (iter != end())
        return iter->second;
    if (!mPrev)
    {
        std::cerr << "can't resolve indentifier \"" << name << "\n" << '\n';
        ABORT();
    }
    return mPrev->resolve(name);
}

TranslationUnit *Ast2Asg::operator()(ast::TranslationUnitContext *ctx)
{
    auto ret = make<asg::TranslationUnit>();
    if (ctx == nullptr)
        return ret;

    Symtbl localDecls(self);

    for (auto &&i : ctx->externalDeclaration())
    {
        if (auto p = i->declaration())
        {
            auto decls = self(p);
            ret->decls.insert(ret->decls.end(), std::make_move_iterator(decls.begin()),
                              std::make_move_iterator(decls.end()));
        }

        else if (auto p = i->functionDefinition())
        {
            auto funcDecl = self(p);
            ret->decls.push_back(funcDecl);

            // 添加到声明表
            localDecls[funcDecl->name] = funcDecl;
        }

        else
            ABORT();
    }

    return ret;
}

//==============================================================================
// 类型
//==============================================================================

Ast2Asg::SpecQual Ast2Asg::operator()(ast::DeclarationSpecifiersContext *ctx)
{
    SpecQual ret = {Type::Spec::kINVALID, Type::Qual()};

    for (auto &&i : ctx->declarationSpecifier())
    {
        if (auto p = i->typeSpecifier())
        {
            if (ret.first == Type::Spec::kINVALID)
            {
                if (p->Int())
                    ret.first = Type::Spec::kInt;
                else if (p->Void())
                    ret.first = Type::Spec::kVoid;
                else
                    ABORT(); // 未知的类型说明符
            }

            else
                ABORT(); // 未知的类型说明符
        }

        else if (auto p = i->typeQualifier())
        {
            if (p->Const())
                ret.second.const_ = true;
            else
                ABORT(); // 未知的类型限定符
        }

        else
            ABORT();
    }

    return ret;
}

std::pair<TypeExpr *, std::string> Ast2Asg::operator()(ast::DeclaratorContext *ctx, TypeExpr *sub)
{
    return self(ctx->directDeclarator(), sub);
}

static int eval_arrlen(Expr *expr)
{
    if (auto p = expr->dcst<IntegerLiteral>())
        return p->val;

    if (auto p = expr->dcst<DeclRefExpr>())
    {
        if (p->decl == nullptr)
            ABORT();

        auto var = p->decl->dcst<VarDecl>();
        if (!var || !var->type->qual.const_)
            ABORT(); // 数组长度必须是编译期常量

        switch (var->type->spec)
        {
        case Type::Spec::kChar:
        case Type::Spec::kInt:
        case Type::Spec::kLong:
        case Type::Spec::kLongLong:
            return eval_arrlen(var->init);

        default:
            ABORT(); // 长度表达式必须是数值类型
        }
    }

    if (auto p = expr->dcst<UnaryExpr>())
    {
        auto sub = eval_arrlen(p->sub);

        switch (p->op)
        {
        case UnaryExpr::kPos:
            return sub;

        case UnaryExpr::kNeg:
            return -sub;

        default:
            ABORT();
        }
    }

    if (auto p = expr->dcst<BinaryExpr>())
    {
        auto lft = eval_arrlen(p->lft);
        auto rht = eval_arrlen(p->rht);

        switch (p->op)
        {
        case BinaryExpr::kAdd:
            return lft + rht;

        case BinaryExpr::kSub:
            return lft - rht;

        default:
            ABORT();
        }
    }

    if (auto p = expr->dcst<InitListExpr>())
    {
        if (p->list.empty())
            return 0;
        return eval_arrlen(p->list[0]);
    }

    ABORT();
}

std::pair<TypeExpr *, std::string> Ast2Asg::operator()(ast::DirectDeclaratorContext *ctx, TypeExpr *sub)
{
    if (auto p = ctx->Identifier())
        return {sub, p->getText()};

    if (ctx->LeftBracket())
    {
        auto arrayType = make<ArrayType>();
        arrayType->sub = sub;

        if (auto p = ctx->assignmentExpression())
            arrayType->len = eval_arrlen(self(p));
        else
            arrayType->len = ArrayType::kUnLen;

        return self(ctx->directDeclarator(), arrayType);
    }

    if (ctx->LeftParen())
    {
        if (auto params = ctx->parameterTypeList())
        {
            auto funcType = make<FunctionType>();
            funcType->sub = sub;

            for (auto &&paramDecl : params->parameterList()->parameterDeclaration())
            {
                auto paramType = make<Type>();
                funcType->params.push_back(paramType);

                auto sq = self(paramDecl->declarationSpecifiers());
                paramType->spec = sq.first;
                paramType->qual = sq.second;

                auto [texp, name] = self(paramDecl->declarator(), nullptr);
                paramType->texp = texp;
            }

            return self(ctx->directDeclarator(), funcType);
        }
        else if (auto idents = ctx->identifierList())
        {
            ABORT();
        }
        else
        {
            auto funcType = make<FunctionType>();
            funcType->sub = sub;
            return self(ctx->directDeclarator(), funcType);
        }

        ABORT();
    }

    ABORT();
}

//==============================================================================
// 表达式
//==============================================================================

Expr *Ast2Asg::operator()(ast::ExpressionContext *ctx)
{
    auto list = ctx->assignmentExpression();
    Expr *ret = self(list[0]);

    for (unsigned i = 1; i < list.size(); ++i)
    {
        auto node = make<BinaryExpr>();
        node->op = node->kComma;
        node->lft = ret;
        node->rht = self(list[i]);
        ret = node;
    }

    return ret;
}

Expr *Ast2Asg::operator()(ast::AssignmentExpressionContext *ctx)
{
    if (auto p = ctx->conditionalExpression())
        return self(p);

    auto ret = make<BinaryExpr>();
    ret->op = ret->kAssign;
    ret->lft = self(ctx->unaryExpression());
    ret->rht = self(ctx->assignmentExpression());
    return ret;
}

Expr *Ast2Asg::operator()(ast::ConditionalExpressionContext *ctx)
{
    return self(ctx->logicalOrExpression());
}

Expr *Ast2Asg::operator()(ast::LogicalOrExpressionContext *ctx)
{
    auto children = ctx->children;
    Expr *ret = self(dynamic_cast<ast::LogicalAndExpressionContext *>(children[0]));

    for (unsigned i = 1; i < children.size(); ++i)
    {
        auto node = make<BinaryExpr>();
        node->op = node->kOr;
        node->lft = ret;
        node->rht = self(dynamic_cast<ast::LogicalAndExpressionContext *>(children[++i]));
        ret = node;
    }

    return ret;
}

Expr *Ast2Asg::operator()(ast::LogicalAndExpressionContext *ctx)
{
    auto children = ctx->children;
    Expr *ret = self(dynamic_cast<ast::InclusiveOrExpressionContext *>(children[0]));

    for (unsigned i = 1; i < children.size(); ++i)
    {
        auto node = make<BinaryExpr>();
        node->op = node->kAnd;
        node->lft = ret;
        node->rht = self(dynamic_cast<ast::InclusiveOrExpressionContext *>(children[++i]));
        ret = node;
    }

    return ret;
}

Expr *Ast2Asg::operator()(ast::InclusiveOrExpressionContext *ctx)
{
    return self(ctx->exclusiveOrExpression()[0]);
}

Expr *Ast2Asg::operator()(ast::ExclusiveOrExpressionContext *ctx)
{
    return self(ctx->andExpression()[0]);
}

Expr *Ast2Asg::operator()(ast::AndExpressionContext *ctx)
{
    return self(ctx->equalityExpression()[0]);
}

Expr *Ast2Asg::operator()(ast::EqualityExpressionContext *ctx)
{
    auto children = ctx->children;
    Expr *ret = self(dynamic_cast<ast::RelationalExpressionContext *>(children[0]));

    for (unsigned i = 1; i < children.size(); ++i)
    {
        auto node = make<BinaryExpr>();

        auto token = dynamic_cast<antlr4::tree::TerminalNode *>(children[i])->getSymbol()->getType();
        switch (token)
        {
        case ast::Equal:
            node->op = node->kEq;
            break;

        case ast::NotEqual:
            node->op = node->kNe;
            break;

        default:
            ABORT();
        }

        node->lft = ret;
        node->rht = self(dynamic_cast<ast::RelationalExpressionContext *>(children[++i]));
        ret = node;
    }

    return ret;
}

Expr *Ast2Asg::operator()(ast::RelationalExpressionContext *ctx)
{
    auto children = ctx->children;
    Expr *ret = self(dynamic_cast<ast::ShiftExpressionContext *>(children[0]));

    for (unsigned i = 1; i < children.size(); ++i)
    {
        auto node = make<BinaryExpr>();

        auto token = dynamic_cast<antlr4::tree::TerminalNode *>(children[i])->getSymbol()->getType();
        switch (token)
        {
        case ast::Less:
            node->op = node->kLt;
            break;

        case ast::Greater:
            node->op = node->kGt;
            break;

        case ast::LessEqual:
            node->op = node->kLe;
            break;

        case ast::GreaterEqual:
            node->op = node->kGe;
            break;

        default:
            ABORT();
        }

        node->lft = ret;
        node->rht = self(dynamic_cast<ast::ShiftExpressionContext *>(children[++i]));
        ret = node;
    }

    return ret;
}

Expr *Ast2Asg::operator()(ast::ShiftExpressionContext *ctx)
{
    return self(ctx->additiveExpression()[0]);
}

Expr *Ast2Asg::operator()(ast::AdditiveExpressionContext *ctx)
{
    auto children = ctx->children;
    Expr *ret = self(dynamic_cast<ast::MultiplicativeExpressionContext *>(children[0]));

    for (unsigned i = 1; i < children.size(); ++i)
    {
        auto node = make<BinaryExpr>();

        auto token = dynamic_cast<antlr4::tree::TerminalNode *>(children[i])->getSymbol()->getType();
        switch (token)
        {
        case ast::Plus:
            node->op = node->kAdd;
            break;

        case ast::Minus:
            node->op = node->kSub;
            break;

        default:
            ABORT();
        }

        node->lft = ret;
        node->rht = self(dynamic_cast<ast::MultiplicativeExpressionContext *>(children[++i]));
        ret = node;
    }

    return ret;
}

Expr *Ast2Asg::operator()(ast::MultiplicativeExpressionContext *ctx)
{
    auto children = ctx->children;
    Expr *ret = self(dynamic_cast<ast::CastExpressionContext *>(children[0]));

    for (unsigned i = 1; i < children.size(); ++i)
    {
        auto node = make<BinaryExpr>();

        auto token = dynamic_cast<antlr4::tree::TerminalNode *>(children[i])->getSymbol()->getType();
        switch (token)
        {
        case ast::Star:
            node->op = node->kMul;
            break;

        case ast::Div:
            node->op = node->kDiv;
            break;

        case ast::Mod:
            node->op = node->kMod;
            break;

        default:
            ABORT();
        }

        node->lft = ret;
        node->rht = self(dynamic_cast<ast::CastExpressionContext *>(children[++i]));
        ret = node;
    }

    return ret;
}

Expr *Ast2Asg::operator()(ast::CastExpressionContext *ctx)
{
    return self(ctx->unaryExpression());
}

Expr *Ast2Asg::operator()(ast::UnaryExpressionContext *ctx)
{
    if (auto p = ctx->postfixExpression())
        return self(p);

    auto ret = make<UnaryExpr>();

    switch (dynamic_cast<antlr4::tree::TerminalNode *>(ctx->unaryOperator()->children[0])->getSymbol()->getType())
    {
    case ast::Plus:
        ret->op = ret->kPos;
        break;

    case ast::Minus:
        ret->op = ret->kNeg;
        break;

    case ast::Not:
        ret->op = ret->kNot;
        break;

    default:
        ABORT();
    }

    ret->sub = self(ctx->castExpression());

    return ret;
}

Expr *Ast2Asg::operator()(ast::PostfixExpressionContext *ctx)
{
    auto children = ctx->children;
    auto ret = self(dynamic_cast<ast::PrimaryExpressionContext *>(children[0]));
    if (!ret)
        ABORT();

    for (int i = 1; i < children.size();)
    {
        if (children[i]->getText() == "[") // 数组索引
        {
            auto binExpr = make<BinaryExpr>();
            binExpr->lft = ret;
            binExpr->rht = self(dynamic_cast<ast::ExpressionContext *>(children[i + 1]));
            binExpr->op = binExpr->kIndex;
            ret = binExpr;
            i += 3;
        }

        else if (children[i]->getText() == "(") // 函数调用
        {
            auto callExpr = make<CallExpr>();
            callExpr->head = ret;
            if (auto &&p = dynamic_cast<ast::ArgumentExpressionListContext *>(children[i + 1]))
            {
                for (auto &&arg : p->assignmentExpression())
                {
                    callExpr->args.push_back(self(arg));
                }
            }
            ret = callExpr;
            i += 3;
        }

        else
            ABORT();
    }

    return ret;
}

Expr *Ast2Asg::operator()(ast::PrimaryExpressionContext *ctx)
{

    if (auto p = ctx->Identifier())
    {
        auto name = p->getText();
        auto ret = make<DeclRefExpr>();
        ret->decl = mSymtbl->resolve(name);
        return ret;
    }

    if (auto p = ctx->Constant())
    {
        auto text = p->getText();

        auto ret = make<IntegerLiteral>();

        ASSERT(!text.empty());
        if (text[0] != '0')
            ret->val = std::stoll(text);

        else if (text.size() == 1)
            ret->val = 0;

        else if (text[1] == 'x' || text[1] == 'X')
            ret->val = std::stoll(text.substr(2), nullptr, 16);

        else
            ret->val = std::stoll(text.substr(1), nullptr, 8);

        return ret;
    }

    if (auto p = ctx->expression())
    {
        auto ret = make<ParenExpr>();
        ret->sub = self(p);
        return ret;
    }

    ABORT();
}

Expr *Ast2Asg::operator()(ast::InitializerContext *ctx)
{
    if (auto p = ctx->assignmentExpression())
        return self(p);

    if (auto p = ctx->initializerList())
    {
        auto ret = make<InitListExpr>();
        for (auto &&i : p->initializer())
        {
            // 将初始化列表展平
            auto expr = self(i);
            if (auto p = expr->dcst<InitListExpr>())
            {
                for (auto &&sub : p->list)
                    ret->list.push_back(sub);
            }
            else
            {
                ret->list.push_back(expr);
            }
        }
        return ret;
    }
    else
    {
        return make<ImplicitInitExpr>();
    }

    ABORT();
}

//==============================================================================
// 语句
//==============================================================================

Stmt *Ast2Asg::operator()(ast::StatementContext *ctx)
{
    if (auto p = ctx->compoundStatement())
        return self(p);

    if (auto p = ctx->expressionStatement())
        return self(p);

    if (auto p = ctx->selectionStatement())
        return self(p);

    if (auto p = ctx->iterationStatement())
        return self(p);

    if (auto p = ctx->jumpStatement())
        return self(p);

    ABORT();
}

CompoundStmt *Ast2Asg::operator()(ast::CompoundStatementContext *ctx)
{
    auto ret = make<CompoundStmt>();

    if (auto p = ctx->blockItemList())
    {
        Symtbl localDecls(self);

        for (auto &&i : p->blockItem())
        {
            if (auto q = i->declaration())
            {
                auto sub = make<DeclStmt>();
                sub->decls = self(q);
                ret->subs.push_back(sub);
            }

            else if (auto q = i->statement())
                ret->subs.push_back(self(q));

            else
                ABORT();
        }
    }

    return ret;
}

Stmt *Ast2Asg::operator()(ast::ExpressionStatementContext *ctx)
{
    if (auto p = ctx->expression())
    {
        auto ret = make<ExprStmt>();
        ret->expr = self(p);
        return ret;
    }

    return make<NullStmt>();
}

Stmt *Ast2Asg::operator()(ast::SelectionStatementContext *ctx)
{
    if (ctx->If())
    {
        auto ifStmt = make<IfStmt>();
        ifStmt->cond = self(ctx->expression());
        ifStmt->then = self(ctx->statement(0));
        if (ctx->Else())
        {
            ifStmt->else_ = self(ctx->statement(1));
        }
        return ifStmt;
    }

    ABORT();
}

Stmt *Ast2Asg::operator()(ast::IterationStatementContext *ctx)
{
    if (ctx->While())
    {
        auto whileStmt = make<WhileStmt>();
        whileStmt->cond = self(ctx->expression());
        whileStmt->body = self(ctx->statement());
        mCurrentIter = whileStmt;
        return whileStmt;
    }

    ABORT();
}

Stmt *Ast2Asg::operator()(ast::JumpStatementContext *ctx)
{
    if (ctx->Return())
    {
        auto ret = make<ReturnStmt>();
        ret->func = mCurrentFunc;
        if (auto p = ctx->expression())
            ret->expr = self(p);
        return ret;
    }

    if (ctx->Break())
    {
        auto breakStmt = make<BreakStmt>();
        breakStmt->loop = mCurrentIter;
        return breakStmt;
    }

    if (ctx->Continue())
    {
        auto continueStmt = make<ContinueStmt>();
        continueStmt->loop = mCurrentIter;
        return continueStmt;
    }

    ABORT();
}

//==============================================================================
// 声明
//==============================================================================

std::vector<Decl *> Ast2Asg::operator()(ast::DeclarationContext *ctx)
{
    std::vector<Decl *> ret;

    auto specs = self(ctx->declarationSpecifiers());

    if (auto p = ctx->initDeclaratorList())
    {
        for (auto &&j : p->initDeclarator())
            ret.push_back(self(j, specs));
    }

    // 如果 initDeclaratorList 为空则这行声明语句无意义
    return ret;
}

FunctionDecl *Ast2Asg::operator()(ast::FunctionDefinitionContext *ctx)
{
    auto ret = make<FunctionDecl>();
    mCurrentFunc = ret;

    auto type = make<Type>();
    ret->type = type;

    auto sq = self(ctx->declarationSpecifiers());
    type->spec = sq.first, type->qual = sq.second;

    auto [funcType, name] = self(ctx->declarator(), nullptr);
    type->texp = funcType;
    ret->name = std::move(name);

    Symtbl localDecls(self);
    // 函数定义在签名之后就加入符号表，以允许递归调用
    (*mSymtbl)[ret->name] = ret;

    // 每个参数的 declarator 会被解析两次
    // 一次在解析 FunctionDefinitionContext 或 InitDeclaratorContext 时，仅获取结果的 name
    // 一次在解析 DirectDeclaratorContext 时，仅获取结果的 texp
    // TODO: 优化
    if (auto paramTypeListCtx = ctx->declarator()->directDeclarator()->parameterTypeList())
    {
        auto paramDecls = paramTypeListCtx->parameterList()->parameterDeclaration();
        auto paramDeclIter = paramDecls.begin();
        for (auto p : funcType->dcst<FunctionType>()->params)
        {
            auto paramDecl = make<VarDecl>();
            ret->params.push_back(paramDecl);
            paramDecl->type = p;

            auto [texp, name] = self((*paramDeclIter++)->declarator(), nullptr);
            paramDecl->name = std::move(name);

            (*mSymtbl)[paramDecl->name] = paramDecl;
        }
    }

    ret->body = self(ctx->compoundStatement());

    return ret;
}

Decl *Ast2Asg::operator()(ast::InitDeclaratorContext *ctx, SpecQual sq)
{
    auto [texp, name] = self(ctx->declarator(), nullptr);
    Decl *ret;

    if (auto funcType = texp->dcst<FunctionType>())
    {
        auto fdecl = make<FunctionDecl>();
        auto type = make<Type>();
        fdecl->type = type;

        type->spec = sq.first;
        type->qual = sq.second;
        type->texp = funcType;

        fdecl->name = std::move(name);

        if (auto paramTypeListCtx = ctx->declarator()->directDeclarator()->parameterTypeList())
        {
            auto paramDecls = paramTypeListCtx->parameterList()->parameterDeclaration();
            auto paramDeclIter = paramDecls.begin();
            for (auto p : funcType->params)
            {
                auto paramDecl = make<VarDecl>();
                fdecl->params.push_back(paramDecl);
                paramDecl->type = p;

                auto [texp, name] = self((*paramDeclIter++)->declarator(), nullptr);
                paramDecl->name = std::move(name);
            }
        }

        if (ctx->initializer())
            ABORT();
        fdecl->body = nullptr;

        ret = fdecl;
    }

    else
    {
        auto vdecl = make<VarDecl>();
        auto type = make<Type>();
        vdecl->type = type;

        type->spec = sq.first;
        type->qual = sq.second;
        type->texp = texp;
        vdecl->name = std::move(name);

        if (auto p = ctx->initializer())
            vdecl->init = self(p);
        else
            vdecl->init = nullptr;

        ret = vdecl;
    }

    // 这个实现允许符号重复定义，新定义会取代旧定义
    (*mSymtbl)[ret->name] = ret;
    return ret;
}

} // namespace asg