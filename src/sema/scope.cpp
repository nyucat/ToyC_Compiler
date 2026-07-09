#include "sema/scope.h"

namespace toyc::sema {

Scope::Scope(Scope* parent, bool isFunctionScope, bool isLoopScope)
    : parent_(parent), isFunctionScope_(isFunctionScope), isLoopScope_(isLoopScope) {}

bool Scope::declare(ast::Symbol* symbol) {
    if (symbol == nullptr) {
        return false;
    }
    const auto [it, inserted] = symbols_.emplace(symbol->name, symbol);
    (void)it;
    return inserted;
}

ast::Symbol* Scope::lookupLocal(const std::string& name) const {
    const auto it = symbols_.find(name);
    if (it == symbols_.end()) {
        return nullptr;
    }
    return it->second;
}

ast::Symbol* Scope::lookup(const std::string& name) const {
    if (ast::Symbol* local = lookupLocal(name)) {
        return local;
    }
    if (parent_ != nullptr) {
        return parent_->lookup(name);
    }
    return nullptr;
}

} // namespace toyc::sema
