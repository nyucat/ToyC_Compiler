#include "sema/constant_evaluator.h"

#include "sema/semantic_error.h"

namespace toyc::sema {

namespace {

[[nodiscard]] bool isTruthy(int value) { return value != 0; }

[[nodiscard]] int toBool(int value) { return value != 0 ? 1 : 0; }

[[nodiscard]] bool isConstSymbol(const ast::Symbol& symbol) {
    return symbol.hasConstValue || symbol.kind == ast::SymbolKind::GlobalConst ||
           symbol.kind == ast::SymbolKind::LocalConst;
}

} // namespace

ConstantEvaluator::ConstantEvaluator(Scope* scope) : scope_(scope) {}

ConstEvalResult ConstantEvaluator::evaluate(const ast::ExprAST& expr) const {
    if (const auto* number = dynamic_cast<const ast::NumberExprAST*>(&expr)) {
        return evalNumber(*number);
    }
    if (const auto* ident = dynamic_cast<const ast::IdentifierExprAST*>(&expr)) {
        return evalIdentifier(*ident);
    }
    if (const auto* unary = dynamic_cast<const ast::UnaryExprAST*>(&expr)) {
        return evalUnary(*unary);
    }
    if (const auto* binary = dynamic_cast<const ast::BinaryExprAST*>(&expr)) {
        return evalBinary(*binary);
    }
    return {};
}

ConstEvalResult ConstantEvaluator::evalNumber(const ast::NumberExprAST& expr) const {
    return ConstEvalResult{true, expr.value()};
}

ConstEvalResult ConstantEvaluator::evalIdentifier(const ast::IdentifierExprAST& expr) const {
    if (scope_ == nullptr) {
        return {};
    }
    ast::Symbol* symbol = expr.resolvedSymbol;
    if (symbol == nullptr) {
        symbol = scope_->lookup(expr.name());
    }
    if (symbol == nullptr || !isConstSymbol(*symbol)) {
        return {};
    }
    if (!symbol->hasConstValue) {
        return {};
    }
    return ConstEvalResult{true, symbol->constValue};
}

ConstEvalResult ConstantEvaluator::evalUnary(const ast::UnaryExprAST& expr) const {
    const ConstEvalResult operand = evaluate(expr.operand());
    if (!operand.success) {
        return {};
    }

    if (expr.op() == "+") {
        return operand;
    }
    if (expr.op() == "-") {
        return ConstEvalResult{true, -operand.value};
    }
    if (expr.op() == "!") {
        return ConstEvalResult{true, toBool(!isTruthy(operand.value))};
    }
    return {};
}

ConstEvalResult ConstantEvaluator::evalBinary(const ast::BinaryExprAST& expr) const {
    const std::string& op = expr.op();

    if (op == "&&") {
        const ConstEvalResult lhs = evaluate(expr.lhs());
        if (!lhs.success) {
            return {};
        }
        if (!isTruthy(lhs.value)) {
            return ConstEvalResult{true, 0};
        }
        const ConstEvalResult rhs = evaluate(expr.rhs());
        if (!rhs.success) {
            return {};
        }
        return ConstEvalResult{true, toBool(isTruthy(rhs.value))};
    }

    if (op == "||") {
        const ConstEvalResult lhs = evaluate(expr.lhs());
        if (!lhs.success) {
            return {};
        }
        if (isTruthy(lhs.value)) {
            return ConstEvalResult{true, 1};
        }
        const ConstEvalResult rhs = evaluate(expr.rhs());
        if (!rhs.success) {
            return {};
        }
        return ConstEvalResult{true, toBool(isTruthy(rhs.value))};
    }

    const ConstEvalResult lhs = evaluate(expr.lhs());
    const ConstEvalResult rhs = evaluate(expr.rhs());
    if (!lhs.success || !rhs.success) {
        return {};
    }

    if (op == "+") {
        return ConstEvalResult{true, lhs.value + rhs.value};
    }
    if (op == "-") {
        return ConstEvalResult{true, lhs.value - rhs.value};
    }
    if (op == "*") {
        return ConstEvalResult{true, lhs.value * rhs.value};
    }
    if (op == "/") {
        if (rhs.value == 0) {
            throw SemanticError("division by zero in constant expression");
        }
        return ConstEvalResult{true, lhs.value / rhs.value};
    }
    if (op == "%") {
        if (rhs.value == 0) {
            throw SemanticError("modulo by zero in constant expression");
        }
        return ConstEvalResult{true, lhs.value % rhs.value};
    }
    if (op == "<") {
        return ConstEvalResult{true, toBool(lhs.value < rhs.value)};
    }
    if (op == ">") {
        return ConstEvalResult{true, toBool(lhs.value > rhs.value)};
    }
    if (op == "<=") {
        return ConstEvalResult{true, toBool(lhs.value <= rhs.value)};
    }
    if (op == ">=") {
        return ConstEvalResult{true, toBool(lhs.value >= rhs.value)};
    }
    if (op == "==") {
        return ConstEvalResult{true, toBool(lhs.value == rhs.value)};
    }
    if (op == "!=") {
        return ConstEvalResult{true, toBool(lhs.value != rhs.value)};
    }

    return {};
}

} // namespace toyc::sema
