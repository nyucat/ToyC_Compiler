#pragma once

#include "ast/ast.h"
#include "ir/basic_block.h"
#include "ir/ir.h"

#include <map>
#include <string>
#include <vector>

namespace toyc::ir {

struct LoopContext {
    std::string continueLabel;
    std::string breakLabel;
};

class IRBuilder {
public:
    explicit IRBuilder(IRModule& module);

    void buildCompUnit(const ast::CompUnitAST& unit);
    [[nodiscard]] const IRModule& module() const { return module_; }

private:
    void buildGlobalDecl(const ast::DeclAST& decl);
    void buildFunction(const ast::FuncDefAST& func);
    void buildBlock(const ast::BlockAST& block, bool createScope);
    void buildStatement(const ast::StmtAST& stmt);
    void buildDecl(const ast::DeclAST& decl);
    void buildAssign(const ast::AssignStmtAST& stmt);

    [[nodiscard]] IRValue buildExpr(const ast::ExprAST& expr);
    [[nodiscard]] IRValue buildBinaryExpr(const ast::BinaryExprAST& expr);
    [[nodiscard]] IRValue buildUnaryExpr(const ast::UnaryExprAST& expr);
    [[nodiscard]] IRValue buildCallExpr(const ast::CallExprAST& expr);
    [[nodiscard]] IRValue buildIdentifierExpr(const ast::IdentifierExprAST& expr);
    [[nodiscard]] IRValue buildLogicalAnd(const ast::BinaryExprAST& expr);
    [[nodiscard]] IRValue buildLogicalOr(const ast::BinaryExprAST& expr);
    [[nodiscard]] IRValue buildRelational(const ast::BinaryExprAST& expr, IROp op);

    [[nodiscard]] IRValue emitConst(int value);
    [[nodiscard]] IRValue emitBinary(IROp op, IRValue lhs, IRValue rhs);
    [[nodiscard]] IRValue emitUnary(IROp op, IRValue operand);
    [[nodiscard]] IRValue emitLoad(IRValue address);
    void emitStore(IRValue value, IRValue address);
    [[nodiscard]] IRValue emitGlobalLoad(const std::string& name);
    void emitGlobalStore(IRValue value, const std::string& name);
    [[nodiscard]] IRValue emitCall(const std::string& callee, const std::vector<IRValue>& args,
                                     ast::ValueType returnType);
    void emitReturn(std::optional<IRValue> value);
    void emitBranch(const std::string& label);
    void emitCondBranch(IRValue cond, const std::string& trueLabel, const std::string& falseLabel);

    [[nodiscard]] IRValue newReg(IRType type = IRType::I32);
    [[nodiscard]] std::string newBlock(const std::string& hint);
    [[nodiscard]] BasicBlock& currentBlock();
    void setInsertPoint(const std::string& label);

    [[nodiscard]] IRValue getAddressForSymbol(const ast::Symbol& symbol);
    void bindSymbolSlot(const ast::Symbol& symbol, int slotId);
    void pushSymbolScope();
    void popSymbolScope();
    [[nodiscard]] bool isConstSymbol(const ast::Symbol& symbol) const;
    [[nodiscard]] std::optional<int> getConstValue(const ast::Symbol& symbol) const;

    IRModule& module_;
    IRFunction* currentFunction_ = nullptr;
    std::string insertBlockLabel_;
    std::vector<LoopContext> loopStack_;
    int localSlotCounter_ = 0;
    std::vector<std::map<const ast::Symbol*, int>> symbolScopeStack_;
};

} // namespace toyc::ir
