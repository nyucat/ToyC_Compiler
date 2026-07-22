#include "sema/semantic_context.h"

namespace toyc::sema {

int SemanticContext::allocateSymbolId() { return nextSymbolId_++; }

ast::Symbol& SemanticContext::createSymbol() {
    auto symbol = std::make_unique<ast::Symbol>();
    symbol->id = allocateSymbolId();
    ast::Symbol& ref = *symbol;
    symbolStorage_.push_back(std::move(symbol));
    return ref;
}

Scope* SemanticContext::currentScope() const {
    if (scopeStack_.empty()) {
        return globalScope_;
    }
    return scopeStack_.back();
}

void SemanticContext::pushScope(bool isFunctionScope, bool isLoopScope) {
    auto scope = std::make_unique<Scope>(currentScope(), isFunctionScope, isLoopScope);
    Scope* raw = scope.get();
    scopeStorage_.push_back(std::move(scope));
    scopeStack_.push_back(raw);
}

void SemanticContext::popScope() {
    if (scopeStack_.empty()) {
        return;
    }
    scopeStack_.pop_back();
}

void SemanticContext::pushLoop() { loopDepth_.push_back(1); }

void SemanticContext::popLoop() {
    if (!loopDepth_.empty()) {
        loopDepth_.pop_back();
    }
}

void SemanticContext::enterFunction(ast::Symbol* functionSymbol) { currentFunction_ = functionSymbol; }

void SemanticContext::leaveFunction() { currentFunction_ = nullptr; }

void SemanticContext::addGlobalObject(GlobalObjectInfo info) { globalObjects_.push_back(std::move(info)); }

} // namespace toyc::sema
