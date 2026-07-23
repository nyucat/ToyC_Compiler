#include "ir/ir_builder.h"

#include "ir/basic_block.h"
#include "sema/symbol.h"

#include <stdexcept>
#include <utility>

namespace toyc::ir {

namespace {

[[nodiscard]] bool isLogicalAnd(const std::string& op) { return op == "&&"; }
[[nodiscard]] bool isLogicalOr(const std::string& op) { return op == "||"; }

[[nodiscard]] bool isRelational(const std::string& op) {
    return op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=";
}

[[nodiscard]] IROp relationalOp(const std::string& op) {
    if (op == "<") {
        return IROp::ICmpLt;
    }
    if (op == ">") {
        return IROp::ICmpGt;
    }
    if (op == "<=") {
        return IROp::ICmpLe;
    }
    if (op == ">=") {
        return IROp::ICmpGe;
    }
    if (op == "==") {
        return IROp::ICmpEq;
    }
    if (op == "!=") {
        return IROp::ICmpNe;
    }
    throw std::runtime_error("unsupported relational operator: " + op);
}

[[nodiscard]] IROp arithmeticOp(const std::string& op) {
    if (op == "+") {
        return IROp::Add;
    }
    if (op == "-") {
        return IROp::Sub;
    }
    if (op == "*") {
        return IROp::Mul;
    }
    if (op == "/") {
        return IROp::Div;
    }
    if (op == "%") {
        return IROp::Mod;
    }
    throw std::runtime_error("unsupported arithmetic operator: " + op);
}

[[nodiscard]] ast::Symbol* requireSymbol(ast::Symbol* symbol, const std::string& context) {
    if (symbol == nullptr) {
        throw std::runtime_error("missing resolved symbol for " + context);
    }
    return symbol;
}

} // namespace

IRBuilder::IRBuilder(IRModule& module) : module_(module) {}

void IRBuilder::buildCompUnit(const ast::CompUnitAST& unit) {
    for (const auto& decl : unit.declarations()) {
        buildGlobalDecl(*decl);
    }
    for (const auto& func : unit.functions()) {
        buildFunction(*func);
    }
}

void IRBuilder::buildGlobalDecl(const ast::DeclAST& decl) {
    IRGlobal global;
    global.name = decl.name();
    global.isConst = dynamic_cast<const ast::ConstDeclAST*>(&decl) != nullptr;

    const ast::Symbol* symbol = requireSymbol(decl.resolvedSymbol, "global declaration " + decl.name());
    if (global.isConst && !symbol->hasConstValue) {
        throw std::runtime_error("global const requires compile-time value: " + decl.name());
    }
    global.initValue = symbol->hasConstValue ? symbol->constValue : 0;
    module_.globals.push_back(global);
}

void IRBuilder::buildFunction(const ast::FuncDefAST& func) {
    IRFunction function;
    function.name = func.name();
    function.returnType = func.returnType();
    for (const auto& param : func.parameters()) {
        function.paramNames.push_back(param->name());
    }

    module_.functions.push_back(std::move(function));
    currentFunction_ = &module_.functions.back();
    symbolScopeStack_.clear();
    symbolScopeStack_.emplace_back();
    loopStack_.clear();

    const std::string entryLabel = newBlock("entry");
    setInsertPoint(entryLabel);

    for (const auto& param : func.parameters()) {
        const ast::Symbol* symbol = requireSymbol(param->resolvedSymbol, "parameter " + param->name());
        IRValue slot = emitUnary(IROp::Alloca, IRValue{-1, IRType::Ptr});
        bindSymbolSlot(*symbol, slot.id);

        IRValue paramValue = newReg();
        IRInstruction loadParam;
        loadParam.op = IROp::ParamLoad;
        loadParam.result = paramValue;
        loadParam.immediate = symbol->paramIndex;
        currentBlock().addInstruction(std::move(loadParam));

        emitStore(paramValue, slot);
    }

    buildBlock(func.body(), false);

    if (!currentBlock().isTerminated()) {
        if (func.returnType() == ast::ValueType::Void) {
            emitReturn(std::nullopt);
        } else {
            emitReturn(emitConst(0));
        }
    }

    buildCFG(*currentFunction_);
    currentFunction_ = nullptr;
    insertBlockLabel_.clear();
}

void IRBuilder::buildBlock(const ast::BlockAST& block, bool createScope) {
    if (createScope) {
        pushSymbolScope();
    }

    for (const auto& stmt : block.statements()) {
        buildStatement(*stmt);
        if (currentBlock().isTerminated()) {
            break;
        }
    }

    if (createScope) {
        popSymbolScope();
    }
}

void IRBuilder::buildStatement(const ast::StmtAST& stmt) {
    if (const auto* declStmt = dynamic_cast<const ast::DeclStmtAST*>(&stmt)) {
        buildDecl(declStmt->declaration());
        return;
    }
    if (const auto* assign = dynamic_cast<const ast::AssignStmtAST*>(&stmt)) {
        buildAssign(*assign);
        return;
    }
    if (const auto* exprStmt = dynamic_cast<const ast::ExprStmtAST*>(&stmt)) {
        if (exprStmt->expression() != nullptr) {
            (void)buildExpr(*exprStmt->expression());
        }
        return;
    }
    if (const auto* block = dynamic_cast<const ast::BlockAST*>(&stmt)) {
        buildBlock(*block, true);
        return;
    }
    if (const auto* ifStmt = dynamic_cast<const ast::IfStmtAST*>(&stmt)) {
        IRValue cond = buildExpr(ifStmt->condition());
        const std::string thenLabel = newBlock("if.then");
        const std::string elseLabel =
            ifStmt->elseBranch() != nullptr ? newBlock("if.else") : std::string();
        const std::string mergeLabel = newBlock("if.end");
        const std::string falseLabel = elseLabel.empty() ? mergeLabel : elseLabel;

        emitCondBranch(cond, thenLabel, falseLabel);

        setInsertPoint(thenLabel);
        buildStatement(ifStmt->thenBranch());
        if (!currentBlock().isTerminated()) {
            emitBranch(mergeLabel);
        }

        if (!elseLabel.empty()) {
            setInsertPoint(elseLabel);
            buildStatement(*ifStmt->elseBranch());
            if (!currentBlock().isTerminated()) {
                emitBranch(mergeLabel);
            }
        }

        setInsertPoint(mergeLabel);
        return;
    }
    if (const auto* whileStmt = dynamic_cast<const ast::WhileStmtAST*>(&stmt)) {
        const std::string condLabel = newBlock("while.cond");
        const std::string bodyLabel = newBlock("while.body");
        const std::string endLabel = newBlock("while.end");

        emitBranch(condLabel);

        setInsertPoint(condLabel);
        IRValue cond = buildExpr(whileStmt->condition());
        emitCondBranch(cond, bodyLabel, endLabel);

        setInsertPoint(bodyLabel);
        loopStack_.push_back(LoopContext{condLabel, endLabel});
        buildStatement(whileStmt->body());
        loopStack_.pop_back();
        if (!currentBlock().isTerminated()) {
            emitBranch(condLabel);
        }

        setInsertPoint(endLabel);
        return;
    }
    if (dynamic_cast<const ast::BreakStmtAST*>(&stmt)) {
        if (loopStack_.empty()) {
            throw std::runtime_error("break outside loop");
        }
        emitBranch(loopStack_.back().breakLabel);
        return;
    }
    if (dynamic_cast<const ast::ContinueStmtAST*>(&stmt)) {
        if (loopStack_.empty()) {
            throw std::runtime_error("continue outside loop");
        }
        emitBranch(loopStack_.back().continueLabel);
        return;
    }
    if (const auto* ret = dynamic_cast<const ast::ReturnStmtAST*>(&stmt)) {
        if (ret->value() != nullptr) {
            emitReturn(buildExpr(*ret->value()));
        } else {
            emitReturn(std::nullopt);
        }
        return;
    }

    throw std::runtime_error("unsupported statement in IR builder");
}

void IRBuilder::buildDecl(const ast::DeclAST& decl) {
    const ast::Symbol* symbol = requireSymbol(decl.resolvedSymbol, "local declaration " + decl.name());
    IRValue init = buildExpr(decl.initializer());

    if (symbol->kind == ast::SymbolKind::LocalConst || symbol->kind == ast::SymbolKind::LocalVar) {
        IRValue slot = emitUnary(IROp::Alloca, IRValue{-1, IRType::Ptr});
        bindSymbolSlot(*symbol, slot.id);
        emitStore(init, slot);
        return;
    }

    throw std::runtime_error("unexpected symbol kind in local declaration");
}

void IRBuilder::buildAssign(const ast::AssignStmtAST& stmt) {
    const ast::Symbol* symbol = requireSymbol(stmt.resolvedSymbol, "assignment " + stmt.name());
    IRValue value = buildExpr(stmt.value());

    switch (symbol->kind) {
    case ast::SymbolKind::GlobalVar:
        emitGlobalStore(value, symbol->name);
        break;
    case ast::SymbolKind::LocalVar:
    case ast::SymbolKind::Parameter:
        emitStore(value, getAddressForSymbol(*symbol));
        break;
    default:
        throw std::runtime_error("cannot assign to " + stmt.name());
    }
}

IRValue IRBuilder::buildExpr(const ast::ExprAST& expr) {
    if (const auto* number = dynamic_cast<const ast::NumberExprAST*>(&expr)) {
        return emitConst(number->value());
    }
    if (const auto* ident = dynamic_cast<const ast::IdentifierExprAST*>(&expr)) {
        return buildIdentifierExpr(*ident);
    }
    if (const auto* unary = dynamic_cast<const ast::UnaryExprAST*>(&expr)) {
        return buildUnaryExpr(*unary);
    }
    if (const auto* binary = dynamic_cast<const ast::BinaryExprAST*>(&expr)) {
        return buildBinaryExpr(*binary);
    }
    if (const auto* call = dynamic_cast<const ast::CallExprAST*>(&expr)) {
        return buildCallExpr(*call);
    }
    throw std::runtime_error("unsupported expression in IR builder");
}

IRValue IRBuilder::buildBinaryExpr(const ast::BinaryExprAST& expr) {
    if (isLogicalAnd(expr.op())) {
        return buildLogicalAnd(expr);
    }
    if (isLogicalOr(expr.op())) {
        return buildLogicalOr(expr);
    }
    if (isRelational(expr.op())) {
        return buildRelational(expr, relationalOp(expr.op()));
    }
    return emitBinary(arithmeticOp(expr.op()), buildExpr(expr.lhs()), buildExpr(expr.rhs()));
}

IRValue IRBuilder::buildUnaryExpr(const ast::UnaryExprAST& expr) {
    if (expr.op() == "+") {
        return buildExpr(expr.operand());
    }
    if (expr.op() == "-") {
        return emitUnary(IROp::Neg, buildExpr(expr.operand()));
    }
    if (expr.op() == "!") {
        return emitUnary(IROp::Not, buildExpr(expr.operand()));
    }
    throw std::runtime_error("unsupported unary operator: " + expr.op());
}

IRValue IRBuilder::buildCallExpr(const ast::CallExprAST& expr) {
    const ast::Symbol* symbol = requireSymbol(expr.resolvedSymbol, "call " + expr.callee());
    ast::ValueType returnType = symbol->returnType;

    std::vector<IRValue> args;
    args.reserve(expr.arguments().size());
    for (const auto& arg : expr.arguments()) {
        args.push_back(buildExpr(*arg));
    }

    IRValue result = emitCall(expr.callee(), args, returnType);
    if (returnType == ast::ValueType::Void) {
        return IRValue{-1, IRType::Void};
    }
    return result;
}

IRValue IRBuilder::buildIdentifierExpr(const ast::IdentifierExprAST& expr) {
    const ast::Symbol* symbol = requireSymbol(expr.resolvedSymbol, "identifier " + expr.name());

    if (isConstSymbol(*symbol)) {
        auto constVal = getConstValue(*symbol);
        if (constVal.has_value()) {
            return emitConst(*constVal);
        }
        throw std::runtime_error("const symbol has no value: " + expr.name());
    }

    switch (symbol->kind) {
    case ast::SymbolKind::GlobalVar:
        return emitGlobalLoad(symbol->name);
    case ast::SymbolKind::LocalVar:
    case ast::SymbolKind::Parameter:
        return emitLoad(getAddressForSymbol(*symbol));
    default:
        throw std::runtime_error("identifier is not readable: " + expr.name());
    }
}

IRValue IRBuilder::buildLogicalAnd(const ast::BinaryExprAST& expr) {
    IRValue lhs = buildExpr(expr.lhs());

    const std::string rhsLabel = newBlock("land.rhs");
    const std::string falseLabel = newBlock("land.false");
    const std::string mergeLabel = newBlock("land.end");

    IRValue slot = emitUnary(IROp::Alloca, IRValue{-1, IRType::Ptr});

    emitCondBranch(lhs, rhsLabel, falseLabel);

    setInsertPoint(rhsLabel);
    IRValue rhs = buildExpr(expr.rhs());
    emitStore(rhs, slot);
    emitBranch(mergeLabel);

    setInsertPoint(falseLabel);
    emitStore(emitConst(0), slot);
    emitBranch(mergeLabel);

    setInsertPoint(mergeLabel);
    return emitLoad(slot);
}

IRValue IRBuilder::buildLogicalOr(const ast::BinaryExprAST& expr) {
    IRValue lhs = buildExpr(expr.lhs());

    const std::string trueLabel = newBlock("lor.true");
    const std::string rhsLabel = newBlock("lor.rhs");
    const std::string mergeLabel = newBlock("lor.end");

    IRValue slot = emitUnary(IROp::Alloca, IRValue{-1, IRType::Ptr});

    emitCondBranch(lhs, trueLabel, rhsLabel);

    setInsertPoint(trueLabel);
    emitStore(emitConst(1), slot);
    emitBranch(mergeLabel);

    setInsertPoint(rhsLabel);
    IRValue rhs = buildExpr(expr.rhs());
    emitStore(rhs, slot);
    emitBranch(mergeLabel);

    setInsertPoint(mergeLabel);
    return emitLoad(slot);
}

IRValue IRBuilder::buildRelational(const ast::BinaryExprAST& expr, IROp op) {
    return emitBinary(op, buildExpr(expr.lhs()), buildExpr(expr.rhs()));
}

IRValue IRBuilder::emitConst(int value) {
    IRValue reg = newReg();
    IRInstruction inst;
    inst.op = IROp::Const;
    inst.result = reg;
    inst.immediate = value;
    currentBlock().addInstruction(std::move(inst));
    return reg;
}

IRValue IRBuilder::emitBinary(IROp op, IRValue lhs, IRValue rhs) {
    IRValue reg = newReg();
    IRInstruction inst;
    inst.op = op;
    inst.result = reg;
    inst.operands = {lhs, rhs};
    currentBlock().addInstruction(std::move(inst));
    return reg;
}

IRValue IRBuilder::emitUnary(IROp op, IRValue operand) {
    IRValue reg = op == IROp::Alloca ? IRValue{newReg().id, IRType::Ptr} : newReg();
    IRInstruction inst;
    inst.op = op;
    inst.result = reg;
    if (op != IROp::Alloca) {
        inst.operands = {operand};
    }
    currentBlock().addInstruction(std::move(inst));
    return reg;
}

IRValue IRBuilder::emitLoad(IRValue address) {
    IRValue reg = newReg();
    IRInstruction inst;
    inst.op = IROp::Load;
    inst.result = reg;
    inst.operands = {address};
    currentBlock().addInstruction(std::move(inst));
    return reg;
}

void IRBuilder::emitStore(IRValue value, IRValue address) {
    IRInstruction inst;
    inst.op = IROp::Store;
    inst.operands = {value, address};
    currentBlock().addInstruction(std::move(inst));
}

IRValue IRBuilder::emitGlobalLoad(const std::string& name) {
    IRValue reg = newReg();
    IRInstruction inst;
    inst.op = IROp::GlobalLoad;
    inst.result = reg;
    inst.callee = name;
    currentBlock().addInstruction(std::move(inst));
    return reg;
}

void IRBuilder::emitGlobalStore(IRValue value, const std::string& name) {
    IRInstruction inst;
    inst.op = IROp::GlobalStore;
    inst.operands = {value};
    inst.callee = name;
    currentBlock().addInstruction(std::move(inst));
}

IRValue IRBuilder::emitCall(const std::string& callee, const std::vector<IRValue>& args,
                            ast::ValueType returnType) {
    IRInstruction inst;
    inst.op = IROp::Call;
    inst.callee = callee;
    inst.operands = args;

    if (returnType == ast::ValueType::Int) {
        IRValue reg = newReg();
        inst.result = reg;
        currentBlock().addInstruction(std::move(inst));
        return reg;
    }

    currentBlock().addInstruction(std::move(inst));
    return IRValue{-1, IRType::Void};
}

void IRBuilder::emitReturn(std::optional<IRValue> value) {
    IRInstruction inst;
    inst.op = IROp::Return;
    if (value.has_value()) {
        inst.operands = {*value};
    }
    currentBlock().addInstruction(std::move(inst));
}

void IRBuilder::emitBranch(const std::string& label) {
    IRInstruction inst;
    inst.op = IROp::Branch;
    inst.label = label;
    currentBlock().addInstruction(std::move(inst));
}

void IRBuilder::emitCondBranch(IRValue cond, const std::string& trueLabel, const std::string& falseLabel) {
    IRInstruction inst;
    inst.op = IROp::CondBranch;
    inst.operands = {cond};
    inst.trueLabel = trueLabel;
    inst.falseLabel = falseLabel;
    currentBlock().addInstruction(std::move(inst));
}

IRValue IRBuilder::newReg(IRType type) {
    if (currentFunction_ == nullptr) {
        throw std::runtime_error("cannot allocate register outside function");
    }
    IRValue value{currentFunction_->nextReg++, type};
    return value;
}

std::string IRBuilder::newBlock(const std::string& hint) {
    if (currentFunction_ == nullptr) {
        throw std::runtime_error("cannot create block outside function");
    }
    const std::string label = currentFunction_->name + "." + hint + "." +
                                std::to_string(currentFunction_->nextBlockId++);
    currentFunction_->blocks.emplace_back(label);
    return label;
}

BasicBlock& IRBuilder::currentBlock() {
    if (insertBlockLabel_.empty()) {
        throw std::runtime_error("no current basic block");
    }
    for (auto& block : currentFunction_->blocks) {
        if (block.label() == insertBlockLabel_) {
            return block;
        }
    }
    throw std::runtime_error("unknown current basic block: " + insertBlockLabel_);
}

void IRBuilder::setInsertPoint(const std::string& label) {
    if (currentFunction_ == nullptr) {
        throw std::runtime_error("cannot set insert point outside function");
    }
    for (const auto& block : currentFunction_->blocks) {
        if (block.label() == label) {
            insertBlockLabel_ = label;
            return;
        }
    }
    throw std::runtime_error("unknown basic block label: " + label);
}

IRValue IRBuilder::getAddressForSymbol(const ast::Symbol& symbol) {
    for (auto it = symbolScopeStack_.rbegin(); it != symbolScopeStack_.rend(); ++it) {
        const auto found = it->find(&symbol);
        if (found != it->end()) {
            return IRValue{found->second, IRType::Ptr};
        }
    }
    throw std::runtime_error("missing slot for symbol " + symbol.name);
}

void IRBuilder::bindSymbolSlot(const ast::Symbol& symbol, int slotId) {
    if (symbolScopeStack_.empty()) {
        throw std::runtime_error("missing symbol scope stack");
    }
    symbolScopeStack_.back()[&symbol] = slotId;
}

void IRBuilder::pushSymbolScope() {
    symbolScopeStack_.emplace_back();
}

void IRBuilder::popSymbolScope() {
    if (symbolScopeStack_.size() <= 1) {
        throw std::runtime_error("cannot pop function symbol scope");
    }
    symbolScopeStack_.pop_back();
}

bool IRBuilder::isConstSymbol(const ast::Symbol& symbol) const {
    return symbol.kind == ast::SymbolKind::GlobalConst || symbol.kind == ast::SymbolKind::LocalConst;
}

std::optional<int> IRBuilder::getConstValue(const ast::Symbol& symbol) const {
    if (symbol.hasConstValue) {
        return symbol.constValue;
    }
    return std::nullopt;
}

} // namespace toyc::ir
