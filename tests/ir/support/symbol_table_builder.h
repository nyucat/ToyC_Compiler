#pragma once

#include "ast/ast.h"
#include "sema/symbol.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace toyc::testutil {

// 仅用于 tests/ir 的手工 AST 测试，不属于正式语义分析模块。
class SymbolTableBuilder {
public:
    ast::Symbol& addGlobalVar(const std::string& name, int initValue);
    ast::Symbol& addGlobalConst(const std::string& name, int initValue);
    ast::Symbol& addLocalVar(const std::string& name, int slotIndex);
    ast::Symbol& addLocalConst(const std::string& name, int value, int slotIndex);
    ast::Symbol& addParameter(const std::string& name, int paramIndex);
    ast::Symbol& addFunction(const std::string& name, ast::ValueType returnType, const ast::FuncDefAST* funcDef);

    void bindDecl(ast::DeclAST& decl);
    void bindIdentifier(ast::IdentifierExprAST& expr);
    void bindAssign(ast::AssignStmtAST& stmt);
    void bindCall(ast::CallExprAST& expr, const std::string& callee);
    void bindFunction(ast::FuncDefAST& func);
    void bindParam(ast::ParamAST& param);

private:
    ast::Symbol& intern(const std::string& name, ast::SymbolKind kind);

    std::vector<std::unique_ptr<ast::Symbol>> storage_;
    std::unordered_map<std::string, ast::Symbol*> symbols_;
};

} // namespace toyc::testutil
