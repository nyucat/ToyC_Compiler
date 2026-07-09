#pragma once

#include "ast/ast.h"
#include "sema/semantic_context.h"
#include "sema/semantic_result.h"

namespace toyc::sema {

class SemanticAnalyzer {
public:
    [[nodiscard]] SemanticResult analyze(ast::CompUnitAST& program);

private:
    enum class ExprContext {
        Value,
        Statement,
    };

    void registerTopLevel(ast::CompUnitAST& program);
    void analyzeTopLevel(ast::CompUnitAST& program);
    void analyzeGlobalDecl(ast::DeclAST& decl);
    void analyzeFuncDef(ast::FuncDefAST& func);

    void analyzeBlock(ast::BlockAST& block, bool createScope);
    void analyzeStmt(ast::StmtAST& stmt);
    void analyzeDecl(ast::DeclAST& decl, bool isGlobal);
    void analyzeVarDecl(ast::VarDeclAST& decl, bool isGlobal);
    void analyzeConstDecl(ast::ConstDeclAST& decl, bool isGlobal);

    void analyzeExpr(ast::ExprAST& expr, ExprContext context);
    [[nodiscard]] ast::ValueType analyzeExprType(ast::ExprAST& expr, ExprContext context);

    [[nodiscard]] bool stmtAlwaysReturns(const ast::StmtAST& stmt) const;

    ast::Symbol* declareVariable(const std::string& name, ast::SymbolKind kind, bool isGlobal);
    ast::Symbol* declareFunction(const std::string& name, ast::ValueType returnType, ast::FuncDefAST* funcDef);
    ast::Symbol* declareParameter(const std::string& name, int paramIndex);
    [[nodiscard]] ast::Symbol* resolveName(const std::string& name) const;

    void bindConstValue(ast::Symbol& symbol, int value);
    void tryBindCompileTimeInit(ast::Symbol& symbol, ast::ExprAST& initExpr);

    class SemanticContext context_;
    ast::Symbol* mainFunction_ = nullptr;
};

} // namespace toyc::sema
