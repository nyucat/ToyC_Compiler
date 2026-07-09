#pragma once

#include "ast/ast.h"

#include <string>

namespace toyc::ast {

// 与成员 C 约定的符号接口。语义分析模块负责填充该结构。
enum class SymbolKind {
    GlobalVar,
    GlobalConst,
    LocalVar,
    LocalConst,
    Parameter,
    Function,
};

struct Symbol {
    int id = -1;
    std::string name;
    SymbolKind kind = SymbolKind::LocalVar;
    ValueType valueType = ValueType::Int;
    bool hasConstValue = false;
    int constValue = 0;
    int paramIndex = -1;
    int slotIndex = -1;
    ValueType returnType = ValueType::Int;
    const FuncDefAST* funcDef = nullptr;
};

} // namespace toyc::ast
