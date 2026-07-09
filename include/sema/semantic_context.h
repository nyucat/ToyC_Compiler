#pragma once

#include "sema/scope.h"
#include "sema/semantic_result.h"

#include <memory>
#include <vector>

namespace toyc::sema {

class SemanticAnalyzer;

class SemanticContext {
public:
    int allocateSymbolId();
    ast::Symbol& createSymbol();

    [[nodiscard]] Scope* currentScope() const;
    [[nodiscard]] Scope* globalScope() const { return globalScope_; }

    void pushScope(bool isFunctionScope = false, bool isLoopScope = false);
    void popScope();

    void pushLoop();
    void popLoop();
    [[nodiscard]] bool inLoop() const { return !loopDepth_.empty(); }

    void enterFunction(ast::Symbol* functionSymbol);
    void leaveFunction();
    [[nodiscard]] ast::Symbol* currentFunction() const { return currentFunction_; }

    void addGlobalObject(GlobalObjectInfo info);
    [[nodiscard]] const std::vector<GlobalObjectInfo>& globalObjects() const { return globalObjects_; }

private:
    int nextSymbolId_ = 0;
    int nextSlotIndex_ = 0;
    int allocateSlotIndex() { return nextSlotIndex_++; }

    std::vector<std::unique_ptr<ast::Symbol>> symbolStorage_;
    std::vector<std::unique_ptr<Scope>> scopeStorage_;
    Scope* globalScope_ = nullptr;
    std::vector<Scope*> scopeStack_;
    std::vector<int> loopDepth_;
    ast::Symbol* currentFunction_ = nullptr;
    std::vector<GlobalObjectInfo> globalObjects_;

    friend class SemanticAnalyzer;
};

} // namespace toyc::sema
