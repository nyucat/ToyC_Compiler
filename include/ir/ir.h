#pragma once

#include "ast/ast.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace toyc::ir {

enum class IRType {
    I32,
    Void,
    Ptr,
};

enum class IROp {
    Const,
    Alloca,
    Load,
    Store,
    GlobalLoad,
    GlobalStore,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    ICmpEq,
    ICmpNe,
    ICmpLt,
    ICmpLe,
    ICmpGt,
    ICmpGe,
    Not,
    Neg,
    Call,
    ParamLoad,
    Branch,
    CondBranch,
    Return,
};

struct IRValue {
    int id = -1;
    IRType type = IRType::I32;

    [[nodiscard]] bool valid() const { return id >= 0; }
};

struct IRGlobal {
    std::string name;
    bool isConst = false;
    int initValue = 0;
};

struct IRInstruction {
    IROp op = IROp::Const;
    std::optional<IRValue> result;
    std::vector<IRValue> operands;
    int immediate = 0;
    std::string callee;
    std::string label;
    std::string trueLabel;
    std::string falseLabel;
};

class BasicBlock;

struct IRFunction {
    std::string name;
    ast::ValueType returnType = ast::ValueType::Int;
    std::vector<std::string> paramNames;
    std::vector<BasicBlock> blocks;
    int nextReg = 0;
    int nextBlockId = 0;
};

struct IRModule {
    std::vector<IRGlobal> globals;
    std::vector<IRFunction> functions;
};

[[nodiscard]] const char* toString(IROp op);
[[nodiscard]] const char* toString(IRType type);
void dumpIRModule(const IRModule& module, std::ostream& out);

} // namespace toyc::ir
