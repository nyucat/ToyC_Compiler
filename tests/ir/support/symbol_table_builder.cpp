#include "tests/ir/support/symbol_table_builder.h"

#include <stdexcept>

namespace toyc::testutil {

ast::Symbol& SymbolTableBuilder::intern(const std::string& name, ast::SymbolKind kind) {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        return *it->second;
    }

    auto symbol = std::make_unique<ast::Symbol>();
    symbol->name = name;
    symbol->kind = kind;
    ast::Symbol* raw = symbol.get();
    storage_.push_back(std::move(symbol));
    symbols_.emplace(name, raw);
    return *raw;
}

ast::Symbol& SymbolTableBuilder::addGlobalVar(const std::string& name, int initValue) {
    ast::Symbol& symbol = intern(name, ast::SymbolKind::GlobalVar);
    symbol.hasConstValue = true;
    symbol.constValue = initValue;
    return symbol;
}

ast::Symbol& SymbolTableBuilder::addGlobalConst(const std::string& name, int initValue) {
    ast::Symbol& symbol = intern(name, ast::SymbolKind::GlobalConst);
    symbol.hasConstValue = true;
    symbol.constValue = initValue;
    return symbol;
}

ast::Symbol& SymbolTableBuilder::addLocalVar(const std::string& name, int slotIndex) {
    ast::Symbol& symbol = intern(name, ast::SymbolKind::LocalVar);
    symbol.slotIndex = slotIndex;
    return symbol;
}

ast::Symbol& SymbolTableBuilder::addLocalConst(const std::string& name, int value, int slotIndex) {
    ast::Symbol& symbol = intern(name, ast::SymbolKind::LocalConst);
    symbol.hasConstValue = true;
    symbol.constValue = value;
    symbol.slotIndex = slotIndex;
    return symbol;
}

ast::Symbol& SymbolTableBuilder::addParameter(const std::string& name, int paramIndex) {
    ast::Symbol& symbol = intern(name, ast::SymbolKind::Parameter);
    symbol.paramIndex = paramIndex;
    return symbol;
}

ast::Symbol& SymbolTableBuilder::addFunction(const std::string& name, ast::ValueType returnType,
                                             const ast::FuncDefAST* funcDef) {
    ast::Symbol& symbol = intern(name, ast::SymbolKind::Function);
    symbol.returnType = returnType;
    symbol.funcDef = funcDef;
    return symbol;
}

void SymbolTableBuilder::bindDecl(ast::DeclAST& decl) {
    auto it = symbols_.find(decl.name());
    if (it == symbols_.end()) {
        throw std::runtime_error("symbol not found for decl: " + decl.name());
    }
    decl.resolvedSymbol = it->second;
}

void SymbolTableBuilder::bindIdentifier(ast::IdentifierExprAST& expr) {
    auto it = symbols_.find(expr.name());
    if (it == symbols_.end()) {
        throw std::runtime_error("symbol not found for identifier: " + expr.name());
    }
    expr.resolvedSymbol = it->second;
}

void SymbolTableBuilder::bindAssign(ast::AssignStmtAST& stmt) {
    auto it = symbols_.find(stmt.name());
    if (it == symbols_.end()) {
        throw std::runtime_error("symbol not found for assignment: " + stmt.name());
    }
    stmt.resolvedSymbol = it->second;
}

void SymbolTableBuilder::bindCall(ast::CallExprAST& expr, const std::string& callee) {
    auto it = symbols_.find(callee);
    if (it == symbols_.end()) {
        throw std::runtime_error("symbol not found for call: " + callee);
    }
    expr.resolvedSymbol = it->second;
}

void SymbolTableBuilder::bindFunction(ast::FuncDefAST& func) {
    func.resolvedSymbol = &addFunction(func.name(), func.returnType(), &func);
}

void SymbolTableBuilder::bindParam(ast::ParamAST& param) {
    auto it = symbols_.find(param.name());
    if (it == symbols_.end()) {
        throw std::runtime_error("symbol not found for parameter: " + param.name());
    }
    param.resolvedSymbol = it->second;
}

} // namespace toyc::testutil
