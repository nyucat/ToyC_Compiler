#pragma once

#include "ast/ast.h"

#include <string>

namespace toyc::ast {

// 与成员 B 约定的符号接口。C 只读取该结构，不实现符号表逻辑。
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
