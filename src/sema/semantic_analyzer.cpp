#include "sema/semantic_analyzer.h"

#include "sema/constant_evaluator.h"
#include "sema/semantic_error.h"

#include <sstream>

namespace toyc::sema {

namespace {

[[nodiscard]] bool isAssignableKind(ast::SymbolKind kind) {
    switch (kind) {
    case ast::SymbolKind::GlobalVar:
    case ast::SymbolKind::LocalVar:
    case ast::SymbolKind::Parameter:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool isReadableIdentifierKind(ast::SymbolKind kind) {
    switch (kind) {
    case ast::SymbolKind::GlobalVar:
    case ast::SymbolKind::GlobalConst:
    case ast::SymbolKind::LocalVar:
    case ast::SymbolKind::LocalConst:
    case ast::SymbolKind::Parameter:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] std::string symbolKindName(ast::SymbolKind kind) {
    switch (kind) {
    case ast::SymbolKind::GlobalVar:
        return "global variable";
    case ast::SymbolKind::GlobalConst:
        return "global constant";
    case ast::SymbolKind::LocalVar:
        return "local variable";
    case ast::SymbolKind::LocalConst:
        return "local constant";
    case ast::SymbolKind::Parameter:
        return "parameter";
    case ast::SymbolKind::Function:
        return "function";
    }
    return "symbol";
}

} // namespace

SemanticResult SemanticAnalyzer::analyze(ast::CompUnitAST& program) {
    mainFunction_ = nullptr;

    auto globalScope = std::make_unique<Scope>(nullptr, false, false);
    context_.globalScope_ = globalScope.get();
    context_.scopeStorage_.push_back(std::move(globalScope));
    context_.scopeStack_.clear();
    context_.scopeStack_.push_back(context_.globalScope_);
    context_.globalObjects_.clear();
    context_.nextSlotIndex_ = 0;

    registerTopLevel(program);
    analyzeTopLevel(program);

    if (mainFunction_ == nullptr) {
        throw SemanticError("program must define int main()");
    }

    SemanticResult result;
    result.success = true;
    result.globalObjects = context_.globalObjects();
    result.mainFunction = mainFunction_;
    return result;
}

void SemanticAnalyzer::registerTopLevel(ast::CompUnitAST& program) {
    for (const auto& decl : program.declarations()) {
        if (dynamic_cast<const ast::ConstDeclAST*>(decl.get()) != nullptr) {
            (void)declareVariable(decl->name(), ast::SymbolKind::GlobalConst, true);
        } else {
            (void)declareVariable(decl->name(), ast::SymbolKind::GlobalVar, true);
        }
    }

    for (const auto& func : program.functions()) {
        (void)declareFunction(func->name(), func->returnType(), func.get());
    }
}

void SemanticAnalyzer::analyzeTopLevel(ast::CompUnitAST& program) {
    for (auto& decl : program.declarations()) {
        analyzeGlobalDecl(*decl);
    }

    for (auto& func : program.functions()) {
        analyzeFuncDef(*func);
    }
}

void SemanticAnalyzer::analyzeGlobalDecl(ast::DeclAST& decl) {
    if (auto* varDecl = dynamic_cast<ast::VarDeclAST*>(&decl)) {
        analyzeVarDecl(*varDecl, true);
        return;
    }
    if (auto* constDecl = dynamic_cast<ast::ConstDeclAST*>(&decl)) {
        analyzeConstDecl(*constDecl, true);
        return;
    }
    throw SemanticError("unsupported global declaration");
}

void SemanticAnalyzer::analyzeFuncDef(ast::FuncDefAST& func) {
    ast::Symbol* functionSymbol = resolveName(func.name());
    if (functionSymbol == nullptr || functionSymbol->kind != ast::SymbolKind::Function) {
        throw SemanticError("function symbol missing: " + func.name());
    }

    functionSymbol->funcDef = &func;
    functionSymbol->returnType = func.returnType();
    func.resolvedSymbol = functionSymbol;

    if (func.name() == "main") {
        if (func.returnType() != ast::ValueType::Int || !func.parameters().empty()) {
            throw SemanticError("main must be declared as int main()");
        }
        mainFunction_ = functionSymbol;
    }

    context_.enterFunction(functionSymbol);
    context_.pushScope(true, false);

    int paramIndex = 0;
    for (auto& param : func.parameters()) {
        ast::Symbol* paramSymbol = declareParameter(param->name(), paramIndex++);
        param->resolvedSymbol = paramSymbol;
    }

    analyzeBlock(const_cast<ast::BlockAST&>(func.body()), false);

    if (func.returnType() == ast::ValueType::Int && !stmtAlwaysReturns(func.body())) {
        throw SemanticError("int function '" + func.name() + "' must return a value on all paths");
    }

    context_.popScope();
    context_.leaveFunction();
}

void SemanticAnalyzer::analyzeBlock(ast::BlockAST& block, bool createScope) {
    if (createScope) {
        context_.pushScope(false, false);
    }

    for (auto& stmt : block.statements()) {
        analyzeStmt(*stmt);
    }

    if (createScope) {
        context_.popScope();
    }
}

void SemanticAnalyzer::analyzeStmt(ast::StmtAST& stmt) {
    if (auto* declStmt = dynamic_cast<ast::DeclStmtAST*>(&stmt)) {
        analyzeDecl(declStmt->declaration(), false);
        return;
    }
    if (auto* assign = dynamic_cast<ast::AssignStmtAST*>(&stmt)) {
        ast::Symbol* symbol = resolveName(assign->name());
        if (symbol == nullptr) {
            throw SemanticError("use of undeclared identifier: " + assign->name());
        }
        if (symbol->kind == ast::SymbolKind::GlobalConst || symbol->kind == ast::SymbolKind::LocalConst) {
            throw SemanticError("cannot assign to constant: " + assign->name());
        }
        if (!isAssignableKind(symbol->kind)) {
            throw SemanticError("cannot assign to " + symbolKindName(symbol->kind) + ": " + assign->name());
        }

        assign->resolvedSymbol = symbol;
        (void)analyzeExprType(assign->value(), ExprContext::Value);
        return;
    }
    if (auto* exprStmt = dynamic_cast<ast::ExprStmtAST*>(&stmt)) {
        if (exprStmt->expression() != nullptr) {
            analyzeExpr(*exprStmt->expression(), ExprContext::Statement);
        }
        return;
    }
    if (auto* block = dynamic_cast<ast::BlockAST*>(&stmt)) {
        analyzeBlock(*block, true);
        return;
    }
    if (auto* ifStmt = dynamic_cast<ast::IfStmtAST*>(&stmt)) {
        (void)analyzeExprType(const_cast<ast::ExprAST&>(ifStmt->condition()), ExprContext::Value);
        analyzeStmt(const_cast<ast::StmtAST&>(ifStmt->thenBranch()));
        if (ifStmt->elseBranch() != nullptr) {
            analyzeStmt(const_cast<ast::StmtAST&>(*ifStmt->elseBranch()));
        }
        return;
    }
    if (auto* whileStmt = dynamic_cast<ast::WhileStmtAST*>(&stmt)) {
        (void)analyzeExprType(const_cast<ast::ExprAST&>(whileStmt->condition()), ExprContext::Value);

        context_.pushLoop();
        analyzeStmt(const_cast<ast::StmtAST&>(whileStmt->body()));
        context_.popLoop();
        return;
    }
    if (dynamic_cast<ast::BreakStmtAST*>(&stmt)) {
        if (!context_.inLoop()) {
            throw SemanticError("break statement not within loop");
        }
        return;
    }
    if (dynamic_cast<ast::ContinueStmtAST*>(&stmt)) {
        if (!context_.inLoop()) {
            throw SemanticError("continue statement not within loop");
        }
        return;
    }
    if (auto* ret = dynamic_cast<ast::ReturnStmtAST*>(&stmt)) {
        ast::Symbol* currentFunction = context_.currentFunction();
        if (currentFunction == nullptr) {
            throw SemanticError("return statement outside function");
        }

        if (currentFunction->returnType == ast::ValueType::Void) {
            if (ret->value() != nullptr) {
                throw SemanticError("void function cannot return a value");
            }
            return;
        }

        if (ret->value() == nullptr) {
            throw SemanticError("int function must return a value");
        }
        (void)analyzeExprType(*ret->value(), ExprContext::Value);
        return;
    }

    throw SemanticError("unsupported statement in semantic analysis");
}

void SemanticAnalyzer::analyzeDecl(ast::DeclAST& decl, bool isGlobal) {
    if (auto* varDecl = dynamic_cast<ast::VarDeclAST*>(&decl)) {
        analyzeVarDecl(*varDecl, isGlobal);
        return;
    }
    if (auto* constDecl = dynamic_cast<ast::ConstDeclAST*>(&decl)) {
        analyzeConstDecl(*constDecl, isGlobal);
        return;
    }
    throw SemanticError("unsupported declaration");
}

void SemanticAnalyzer::analyzeVarDecl(ast::VarDeclAST& decl, bool isGlobal) {
    ast::SymbolKind kind = isGlobal ? ast::SymbolKind::GlobalVar : ast::SymbolKind::LocalVar;
    ast::Symbol* symbol = resolveName(decl.name());
    if (symbol == nullptr) {
        symbol = declareVariable(decl.name(), kind, isGlobal);
    } else if (symbol->kind != kind) {
        throw SemanticError("redefinition of identifier: " + decl.name());
    }

    if (!isGlobal) {
        symbol->slotIndex = context_.allocateSlotIndex();
    }

    decl.resolvedSymbol = symbol;
    analyzeExpr(decl.initializer(), ExprContext::Value);
    tryBindCompileTimeInit(*symbol, decl.initializer());

    if (isGlobal) {
        GlobalObjectInfo info;
        info.symbol = symbol;
        info.name = symbol->name;
        info.isConst = false;
        info.initialValue = symbol->hasConstValue ? symbol->constValue : 0;
        context_.addGlobalObject(std::move(info));
    }
}

void SemanticAnalyzer::analyzeConstDecl(ast::ConstDeclAST& decl, bool isGlobal) {
    ast::SymbolKind kind = isGlobal ? ast::SymbolKind::GlobalConst : ast::SymbolKind::LocalConst;
    ast::Symbol* symbol = resolveName(decl.name());
    if (symbol == nullptr) {
        symbol = declareVariable(decl.name(), kind, isGlobal);
    } else if (symbol->kind != kind) {
        throw SemanticError("redefinition of identifier: " + decl.name());
    }

    if (!isGlobal) {
        symbol->slotIndex = context_.allocateSlotIndex();
    }

    decl.resolvedSymbol = symbol;

    ConstantEvaluator evaluator(context_.currentScope());
    const ConstEvalResult evalResult = evaluator.evaluate(decl.initializer());
    if (!evalResult.success) {
        throw SemanticError("const variable '" + decl.name() + "' requires compile-time initializer");
    }

    bindConstValue(*symbol, evalResult.value);

    if (isGlobal) {
        GlobalObjectInfo info;
        info.symbol = symbol;
        info.name = symbol->name;
        info.isConst = true;
        info.initialValue = symbol->constValue;
        context_.addGlobalObject(std::move(info));
    }
}

void SemanticAnalyzer::analyzeExpr(ast::ExprAST& expr, ExprContext context) {
    (void)analyzeExprType(expr, context);
}

ast::ValueType SemanticAnalyzer::analyzeExprType(ast::ExprAST& expr, ExprContext context) {
    if (auto* number = dynamic_cast<ast::NumberExprAST*>(&expr)) {
        (void)number;
        return ast::ValueType::Int;
    }

    if (auto* ident = dynamic_cast<ast::IdentifierExprAST*>(&expr)) {
        ast::Symbol* symbol = resolveName(ident->name());
        if (symbol == nullptr) {
            throw SemanticError("use of undeclared identifier: " + ident->name());
        }
        if (symbol->kind == ast::SymbolKind::Function) {
            throw SemanticError("function '" + ident->name() + "' used as value");
        }
        if (!isReadableIdentifierKind(symbol->kind)) {
            throw SemanticError("identifier is not readable: " + ident->name());
        }

        ident->resolvedSymbol = symbol;
        return ast::ValueType::Int;
    }

    if (auto* unary = dynamic_cast<ast::UnaryExprAST*>(&expr)) {
        (void)analyzeExprType(unary->operand(), context);
        return ast::ValueType::Int;
    }

    if (auto* binary = dynamic_cast<ast::BinaryExprAST*>(&expr)) {
        (void)analyzeExprType(binary->lhs(), context);
        (void)analyzeExprType(binary->rhs(), context);
        return ast::ValueType::Int;
    }

    if (auto* call = dynamic_cast<ast::CallExprAST*>(&expr)) {
        ast::Symbol* symbol = resolveName(call->callee());
        if (symbol == nullptr) {
            throw SemanticError("call to undeclared function: " + call->callee());
        }
        if (symbol->kind != ast::SymbolKind::Function) {
            throw SemanticError("'" + call->callee() + "' is not a function");
        }

        call->resolvedSymbol = symbol;

        if (symbol->funcDef != nullptr) {
            const std::size_t expectedParams = symbol->funcDef->parameters().size();
            if (call->arguments().size() != expectedParams) {
                std::ostringstream message;
                message << "function '" << call->callee() << "' expects " << expectedParams << " argument(s), got "
                        << call->arguments().size();
                throw SemanticError(message.str());
            }
        }

        for (const auto& arg : call->arguments()) {
            (void)analyzeExprType(const_cast<ast::ExprAST&>(*arg), ExprContext::Value);
        }

        if (symbol->returnType == ast::ValueType::Void && context == ExprContext::Value) {
            throw SemanticError("void function '" + call->callee() + "' used in value context");
        }

        return symbol->returnType;
    }

    throw SemanticError("unsupported expression in semantic analysis");
}

bool SemanticAnalyzer::stmtAlwaysReturns(const ast::StmtAST& stmt) const {
    if (dynamic_cast<const ast::ReturnStmtAST*>(&stmt)) {
        return true;
    }
    if (const auto* block = dynamic_cast<const ast::BlockAST*>(&stmt)) {
        for (const auto& inner : block->statements()) {
            if (stmtAlwaysReturns(*inner)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* ifStmt = dynamic_cast<const ast::IfStmtAST*>(&stmt)) {
        if (ifStmt->elseBranch() == nullptr) {
            return false;
        }
        return stmtAlwaysReturns(ifStmt->thenBranch()) && stmtAlwaysReturns(*ifStmt->elseBranch());
    }
    return false;
}

ast::Symbol* SemanticAnalyzer::declareVariable(const std::string& name, ast::SymbolKind kind, bool isGlobal) {
    if (context_.currentScope() == nullptr) {
        throw SemanticError("internal error: missing current scope");
    }

    ast::Symbol& symbol = context_.createSymbol();
    symbol.name = name;
    symbol.kind = kind;
    symbol.valueType = ast::ValueType::Int;

    if (!context_.currentScope()->declare(&symbol)) {
        throw SemanticError("redefinition of identifier: " + name);
    }

    if (isGlobal) {
        (void)isGlobal;
    }

    return &symbol;
}

ast::Symbol* SemanticAnalyzer::declareFunction(const std::string& name, ast::ValueType returnType,
                                               ast::FuncDefAST* funcDef) {
    if (context_.globalScope_ == nullptr) {
        throw SemanticError("internal error: missing global scope");
    }

    ast::Symbol& symbol = context_.createSymbol();
    symbol.name = name;
    symbol.kind = ast::SymbolKind::Function;
    symbol.valueType = returnType;
    symbol.returnType = returnType;
    symbol.funcDef = funcDef;

    if (!context_.globalScope_->declare(&symbol)) {
        throw SemanticError("redefinition of identifier: " + name);
    }

    return &symbol;
}

ast::Symbol* SemanticAnalyzer::declareParameter(const std::string& name, int paramIndex) {
    ast::Symbol* symbol = declareVariable(name, ast::SymbolKind::Parameter, false);
    symbol->paramIndex = paramIndex;
    return symbol;
}

ast::Symbol* SemanticAnalyzer::resolveName(const std::string& name) const {
    if (context_.currentScope() == nullptr) {
        return nullptr;
    }
    return context_.currentScope()->lookup(name);
}

void SemanticAnalyzer::bindConstValue(ast::Symbol& symbol, int value) {
    symbol.hasConstValue = true;
    symbol.constValue = value;
}

void SemanticAnalyzer::tryBindCompileTimeInit(ast::Symbol& symbol, ast::ExprAST& initExpr) {
    ConstantEvaluator evaluator(context_.currentScope());
    const ConstEvalResult evalResult = evaluator.evaluate(initExpr);
    if (evalResult.success) {
        bindConstValue(symbol, evalResult.value);
    }
}

} // namespace toyc::sema
