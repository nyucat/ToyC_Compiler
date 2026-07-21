#pragma once

#include "sema/symbol.h"

#include <string>
#include <unordered_map>

namespace toyc::sema {

class Scope {
public:
    explicit Scope(Scope* parent = nullptr, bool isFunctionScope = false, bool isLoopScope = false);

    bool declare(ast::Symbol* symbol);
    [[nodiscard]] ast::Symbol* lookupLocal(const std::string& name) const;
    [[nodiscard]] ast::Symbol* lookup(const std::string& name) const;

    [[nodiscard]] Scope* parent() const { return parent_; }
    [[nodiscard]] bool isFunctionScope() const { return isFunctionScope_; }
    [[nodiscard]] bool isLoopScope() const { return isLoopScope_; }

private:
    Scope* parent_;
    bool isFunctionScope_;
    bool isLoopScope_;
    std::unordered_map<std::string, ast::Symbol*> symbols_;
};

} // namespace toyc::sema
