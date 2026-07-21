#pragma once

#include "ast/ast.h"
#include "sema/scope.h"

namespace toyc::sema {

struct ConstEvalResult {
    bool success = false;
    int value = 0;
};

class ConstantEvaluator {
public:
    explicit ConstantEvaluator(Scope* scope);

    [[nodiscard]] ConstEvalResult evaluate(const ast::ExprAST& expr) const;

private:
    [[nodiscard]] ConstEvalResult evalNumber(const ast::NumberExprAST& expr) const;
    [[nodiscard]] ConstEvalResult evalIdentifier(const ast::IdentifierExprAST& expr) const;
    [[nodiscard]] ConstEvalResult evalUnary(const ast::UnaryExprAST& expr) const;
    [[nodiscard]] ConstEvalResult evalBinary(const ast::BinaryExprAST& expr) const;

    Scope* scope_;
};

} // namespace toyc::sema
